#include "CefCrypto.h"

#include <array>
#include <cstring>

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {
#include "thirdparty/tweetnacl.h"
#include "thirdparty/blake2.h"
#include "thirdparty/aes.h"
}

#include "thirdparty/picosha2.h"

// TweetNaCl expects the application to provide this symbol.
extern "C" void randombytes(unsigned char* out, unsigned long long n)
{
    cefcrypto::RandomBytes(out, static_cast<size_t>(n));
}

namespace cefcrypto
{

namespace
{

bool IsAllZero(const uint8_t* p, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i)
        acc |= p[i];
    return acc == 0;
}

} // namespace

void RandomBytes(uint8_t* out, size_t n)
{
    // minSdk 24: getrandom() itself is API 28, but the syscall exists on every
    // kernel Android 7+ ships; fall back to /dev/urandom if it is missing.
    size_t done = 0;
    while (done < n)
    {
        long r = syscall(SYS_getrandom, out + done, n - done, 0);
        if (r > 0)
        {
            done += static_cast<size_t>(r);
            continue;
        }
        if (r < 0 && errno != EINTR)
            break;
    }
    if (done == n)
        return;

    // Fallback: /dev/urandom (getrandom can fail on very old kernels).
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return;
    while (done < n)
    {
        ssize_t r = read(fd, out + done, n - done);
        if (r <= 0)
            break;
        done += static_cast<size_t>(r);
    }
    close(fd);
}

void GenerateKeyPair(KeyPair& out)
{
    // crypto_box_keypair == random sk + X25519_base, identical construction
    // to libsodium's crypto_kx_keypair.
    crypto_box_keypair(out.publicKey, out.secretKey);
}

void DerivePublicKey(const uint8_t secretKey[KX_SECRET_KEY_BYTES],
                     uint8_t publicKey[KX_PUBLIC_KEY_BYTES])
{
    crypto_scalarmult_base(publicKey, secretKey);
}

bool GenerateClientSessionKeys(const KeyPair& client,
                               const uint8_t serverPublicKey[KX_PUBLIC_KEY_BYTES],
                               SessionKeys& out)
{
    uint8_t q[32];
    crypto_scalarmult(q, client.secretKey, serverPublicKey);

    // libsodium returns -1 when the shared secret is the all-zero value
    // (low-order point); the PC client treats empty keys as fatal.
    if (IsAllZero(q, sizeof(q)))
        return false;

    // libsodium crypto_kx_client_session_keys:
    //   keys = BLAKE2b-512( q || client_pk || server_pk )   (unkeyed, no salt/person)
    //   rx   = keys[0..31], tx = keys[32..63]
    blake2b_state st;
    uint8_t keys[64];
    blake2b_init(&st, sizeof(keys));
    blake2b_update(&st, q, sizeof(q));
    blake2b_update(&st, client.publicKey, KX_PUBLIC_KEY_BYTES);
    blake2b_update(&st, serverPublicKey, KX_PUBLIC_KEY_BYTES);
    blake2b_final(&st, keys, sizeof(keys));

    std::memcpy(out.rx, keys, SESSION_KEY_BYTES);
    std::memcpy(out.tx, keys + SESSION_KEY_BYTES, SESSION_KEY_BYTES);

    std::memset(q, 0, sizeof(q));
    std::memset(keys, 0, sizeof(keys));
    std::memset(&st, 0, sizeof(st));
    return true;
}

std::vector<uint8_t> EncryptPacket(const uint8_t* plaintext, size_t size,
                                   const uint8_t key[SESSION_KEY_BYTES])
{
    if (!plaintext && size > 0)
        return {};

    uint8_t nonce[PACKET_NONCE_BYTES];
    RandomBytes(nonce, sizeof(nonce));

    // NaCl padding convention: 32 zero bytes before the message,
    // 16 zero bytes before the ciphertext; the libsodium "easy" output is the
    // NaCl output without its 16 leading zero bytes (= mac || ciphertext).
    std::vector<uint8_t> padded(size + 32, 0);
    if (size > 0)
        std::memcpy(padded.data() + 32, plaintext, size);

    std::vector<uint8_t> boxed(padded.size(), 0);
    crypto_secretbox(boxed.data(), padded.data(),
                     static_cast<unsigned long long>(padded.size()), nonce, key);

    std::vector<uint8_t> out;
    out.reserve(sizeof(nonce) + boxed.size() - 16);
    out.insert(out.end(), nonce, nonce + sizeof(nonce));
    out.insert(out.end(), boxed.begin() + 16, boxed.end());
    return out;
}

std::vector<uint8_t> DecryptPacket(const uint8_t* data, size_t size,
                                   const uint8_t key[SESSION_KEY_BYTES])
{
    if (!data || size < PACKET_NONCE_BYTES + PACKET_MAC_BYTES)
        return {};

    const uint8_t* nonce = data;
    const uint8_t* ciphertext = data + PACKET_NONCE_BYTES;
    const size_t ciphertextLen = size - PACKET_NONCE_BYTES;

    std::vector<uint8_t> boxed(ciphertextLen + 16, 0);
    std::memcpy(boxed.data() + 16, ciphertext, ciphertextLen);

    std::vector<uint8_t> padded(boxed.size(), 0);
    if (crypto_secretbox_open(padded.data(), boxed.data(),
                              static_cast<unsigned long long>(boxed.size()),
                              nonce, key) != 0)
        return {};

    return std::vector<uint8_t>(padded.begin() + 32, padded.end());
}

std::string Sha256Hex(const uint8_t* data, size_t size)
{
    std::array<uint8_t, picosha2::k_digest_size> hash;
    picosha2::hash256(data, data + size, hash.begin(), hash.end());
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
}

namespace
{

void PadDataTo16(std::vector<uint8_t>& data)
{
    const size_t remainder = data.size() % AES_BLOCKLEN;
    const size_t padding = (remainder == 0) ? AES_BLOCKLEN : (AES_BLOCKLEN - remainder);
    for (size_t i = 0; i < padding; ++i)
        data.push_back(static_cast<uint8_t>(padding));
}

bool UnpadData(std::vector<uint8_t>& data)
{
    if (data.empty()) return false;
    const uint8_t padding = data.back();
    if (padding == 0 || padding > AES_BLOCKLEN || padding > data.size()) return false;
    for (size_t i = 0; i < padding; ++i)
    {
        if (data[data.size() - 1 - i] != padding) return false;
    }
    data.resize(data.size() - padding);
    return true;
}

std::vector<uint8_t> AesCbcDecrypt(const std::vector<uint8_t>& encrypted,
                                   const std::vector<uint8_t>& key,
                                   const std::array<uint8_t, AES_IV_BYTES>& iv)
{
    // 密钥长度必须正好是 AES_KEYLEN：AES_init_ctx_iv 按 AES_KEYLEN 读 key.data()，
    // 只检查 empty() 时，恶意服下发一个短 master_resource_key 会让它读到缓冲区之外
    // （堆越界读，读到什么就当密钥扩展材料）。
    if (key.size() != AES_KEYLEN || encrypted.empty() || encrypted.size() % AES_BLOCKLEN != 0)
        return {};
    AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key.data(), iv.data());
    std::vector<uint8_t> data = encrypted;
    AES_CBC_decrypt_buffer(&ctx, data.data(), data.size());
    if (!UnpadData(data)) return {};
    return data;
}

constexpr std::array<uint8_t, 5> kResourceMagic = { 'O', 'C', 'E', 'F', '2' };
constexpr uint8_t kFlagEncrypted = 1 << 0;
constexpr size_t kHeaderBytes = kResourceMagic.size() + 1;

bool DecodeLegacy(const std::vector<uint8_t>& input,
                  const std::vector<uint8_t>& masterKey,
                  std::vector<uint8_t>& output)
{
    output.clear();
    if (masterKey.empty() || input.size() < AES_IV_BYTES)
        return false;

    std::array<uint8_t, AES_IV_BYTES> iv{};
    std::copy_n(input.begin(), AES_IV_BYTES, iv.begin());
    std::vector<uint8_t> ciphertext(input.begin() + AES_IV_BYTES, input.end());
    output = AesCbcDecrypt(ciphertext, masterKey, iv);
    // 只有解出非空明文才算成功。原来还允许「密文恰好一个 block 且解出空」，
    // 那是把 padding 校验失败（AesCbcDecrypt 返回 {}）误判成"空文件，成功"，
    // 上层会拿着空 vector 继续走解包流程。
    return !output.empty();
}

} // namespace

bool DecodeResourceEntry(const std::vector<uint8_t>& input,
                         const std::vector<uint8_t>& masterKey,
                         std::vector<uint8_t>& output)
{
    output.clear();

    const bool hasMagic = input.size() >= kHeaderBytes &&
        std::equal(kResourceMagic.begin(), kResourceMagic.end(), input.begin());

    if (!hasMagic)
        return DecodeLegacy(input, masterKey, output);

    const uint8_t flags = input[kResourceMagic.size()];

    if ((flags & kFlagEncrypted) == 0)
    {
        output.assign(input.begin() + kHeaderBytes, input.end());
        return true;
    }

    if (masterKey.empty() || input.size() < kHeaderBytes + AES_IV_BYTES)
        return false;

    std::array<uint8_t, AES_IV_BYTES> iv{};
    std::copy_n(input.begin() + kHeaderBytes, AES_IV_BYTES, iv.begin());
    std::vector<uint8_t> ciphertext(input.begin() + kHeaderBytes + AES_IV_BYTES, input.end());

    output = AesCbcDecrypt(ciphertext, masterKey, iv);
    // 只有解出非空明文才算成功。原来还允许「密文恰好一个 block 且解出空」，
    // 那是把 padding 校验失败（AesCbcDecrypt 返回 {}）误判成"空文件，成功"，
    // 上层会拿着空 vector 继续走解包流程。
    return !output.empty();
}


std::string Sha256FileHex(const char* path, size_t maxBytes)
{
    if (!path)
        return {};

    FILE* f = fopen(path, "rb");
    if (!f)
        return {};

    // 流式哈希：1MB 缓冲，内存峰值与文件大小无关。
    // 原来的路径是 ReadFile 整块读入 + Sha256Hex，几十 MB 的包在游戏主线程
    // 上会造成明显掉帧，且同时占用两份内存（文件 + 哈希拷贝）。
    picosha2::hash256_one_by_one hasher;
    std::vector<uint8_t> buf(1024 * 1024);
    size_t total = 0;
    bool tooBig = false;

    for (;;)
    {
        const size_t n = fread(buf.data(), 1, buf.size(), f);
        if (n == 0)
            break;
        total += n;
        if (total > maxBytes)
        {
            tooBig = true;
            break;
        }
        hasher.process(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(n));
    }
    fclose(f);

    if (tooBig || total == 0)
        return {};

    hasher.finish();
    std::array<uint8_t, picosha2::k_digest_size> hash{};
    hasher.get_hash_bytes(hash.begin(), hash.end());
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
}

} // namespace cefcrypto
