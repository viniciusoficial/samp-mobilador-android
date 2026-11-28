#pragma once

#include <RakNet/BitStream.h>
#include <RakNet/StringCompressor.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace proto {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct Quat {
	float w = 1.0f;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

inline bool isFinite(float v) { return std::isfinite(v); }
inline bool isFinite(const Vec2& v) { return std::isfinite(v.x) && std::isfinite(v.y); }
inline bool isFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Thin reader over RakNet::BitStream with a sticky failure flag.
// On any bounds failure, ok() becomes false and subsequent calls are no-ops
// that leave destination fields untouched.
class Reader {
public:
	static constexpr bool kReading = true;

	explicit Reader(RakNet::BitStream& bs) : bs_(&bs) {}

	Reader(const uint8_t* data, uint32_t bitLength)
		: owned_(bitLength > 0 && bitLength <= static_cast<uint32_t>(INT_MAX - 7) && data
					 ? RakNet::BitStream(const_cast<unsigned char*>(data), BITS_TO_BYTES(bitLength), false)
					 : RakNet::BitStream()),
		  bs_(&owned_)
	{
		if (bitLength > static_cast<uint32_t>(INT_MAX - 7) || (bitLength > 0 && !data)) {
			ok_ = false;
			return;
		}
		if (bitLength > 0) {
			owned_.SetWriteOffset(static_cast<int>(bitLength));
		}
	}

	Reader(const Reader&) = delete;
	Reader& operator=(const Reader&) = delete;

	bool ok() const { return ok_; }
	int unreadBits() const { return bs_->GetNumberOfUnreadBits(); }
	size_t unreadBytes() const
	{
		const int u = bs_->GetNumberOfUnreadBits();
		return u > 0 ? static_cast<size_t>(u / 8) : 0;
	}
	int readOffset() const { return bs_->GetReadOffset(); }

	bool u8(uint8_t& v) { return readScalar(v); }
	bool i8(int8_t& v) { return readScalar(v); }
	bool u16(uint16_t& v) { return readScalar(v); }
	bool i16(int16_t& v) { return readScalar(v); }
	bool u32(uint32_t& v) { return readScalar(v); }
	bool i32(int32_t& v) { return readScalar(v); }
	bool f32(float& v) { return readScalar(v); }

	bool bit(bool& v)
	{
		if (!ok_) return false;
		bool tmp = false;
		if (!bs_->Read(tmp)) return fail();
		v = tmp;
		return true;
	}

	bool vec2(Vec2& v)
	{
		Vec2 tmp{};
		if (!f32(tmp.x) || !f32(tmp.y)) return false;
		v = tmp;
		return true;
	}

	bool vec3(Vec3& v)
	{
		Vec3 tmp{};
		if (!f32(tmp.x) || !f32(tmp.y) || !f32(tmp.z)) return false;
		v = tmp;
		return true;
	}

	bool bytes(void* buf, size_t n)
	{
		if (!ok_) return false;
		if (n == 0) return true;
		if (!buf || n > unreadBytes()) return fail();
		if (!bs_->Read(reinterpret_cast<char*>(buf), static_cast<int>(n))) return fail();
		return true;
	}

	bool dynStr8(std::string& s)
	{
		if (!ok_) return false;
		uint8_t len = 0;
		if (!u8(len)) return false;
		if (len == 0) {
			s.clear();
			return true;
		}
		char buf[256];
		if (!bytes(buf, len)) return false;
		s.assign(buf, len);
		return true;
	}

	bool dynStr16(std::string& s, uint16_t maxLen = 4096)
	{
		if (!ok_) return false;
		uint16_t len = 0;
		if (!u16(len)) return false;
		if (len > maxLen) return fail();
		if (len == 0) {
			s.clear();
			return true;
		}
		if (len > unreadBytes()) return fail();
		std::string tmp(len, '\0');
		if (!bs_->Read(tmp.data(), static_cast<int>(len))) return fail();
		s = std::move(tmp);
		return true;
	}

	bool dynStr32(std::string& s, uint32_t maxLen = 4096)
	{
		if (!ok_) return false;
		uint32_t len = 0;
		if (!u32(len)) return false;
		if (len > maxLen) return fail();
		if (len == 0) {
			s.clear();
			return true;
		}
		if (len > unreadBytes()) return fail();
		std::string tmp(len, '\0');
		if (!bs_->Read(tmp.data(), static_cast<int>(len))) return fail();
		s = std::move(tmp);
		return true;
	}

	bool fixedStr(std::string& s, size_t len)
	{
		if (!ok_) return false;
		if (len == 0) {
			s.clear();
			return true;
		}
		if (len > unreadBytes()) return fail();
		std::string tmp(len, '\0');
		if (!bs_->Read(tmp.data(), static_cast<int>(len))) return fail();
		s = std::move(tmp);
		return true;
	}

	bool compressedStr(std::string& s, size_t maxChars = 4096)
	{
		if (!ok_) return false;
		std::vector<char> buf(maxChars + 1, '\0');
		if (!StringCompressor::Instance()->DecodeString(buf.data(), static_cast<int>(buf.size()), bs_)) {
			return fail();
		}
		s.assign(buf.data());
		return true;
	}

	bool normQuat(Quat& q)
	{
		if (!ok_) return false;
		float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
		if (!bs_->ReadNormQuat(w, x, y, z)) return fail();
		q = {w, x, y, z};
		return true;
	}

	bool compressedVec(Vec3& v)
	{
		if (!ok_) return false;
		float x = 0.0f, y = 0.0f, z = 0.0f;
		if (!bs_->ReadVector(x, y, z)) return fail();
		v = {x, y, z};
		return true;
	}

	bool healthArmour(uint8_t& health, uint8_t& armour)
	{
		if (!ok_) return false;
		uint8_t packed = 0;
		if (!u8(packed)) return false;
		const uint8_t hl = static_cast<uint8_t>(packed >> 4);
		const uint8_t arm = static_cast<uint8_t>(packed & 0x0F);
		health = (hl == 0xF) ? 100 : static_cast<uint8_t>(hl * 7);
		armour = (arm == 0xF) ? 100 : static_cast<uint8_t>(arm * 7);
		return true;
	}

	// Reads optional trailing field(s) that older/shorter packets may omit.
	// If reading fails, rewinds the read offset and clears the error.
	template <typename Fn>
	bool optionalTail(Fn&& fn)
	{
		if (!ok_) return false;
		const int savedOffset = bs_->GetReadOffset();
		if (!fn(*this)) {
			bs_->SetReadOffset(savedOffset);
			ok_ = true;
		}
		return true;
	}

private:
	template <typename T>
	bool readScalar(T& v)
	{
		if (!ok_) return false;
		T tmp{};
		if (!bs_->Read(tmp)) return fail();
		v = tmp;
		return true;
	}

	bool fail()
	{
		ok_ = false;
		return false;
	}

	RakNet::BitStream owned_;
	RakNet::BitStream* bs_;
	bool ok_ = true;
};

// Symmetric writer over RakNet::BitStream, matching Reader's method names so
// each packet struct writes its field order once in serialize(S&, ProtoVersion).
class Writer {
public:
	static constexpr bool kReading = false;

	explicit Writer(RakNet::BitStream& bs) : bs_(&bs) {}

	bool ok() const { return ok_; }

	bool u8(uint8_t v) { bs_->Write(v); return true; }
	bool i8(int8_t v) { bs_->Write(v); return true; }
	bool u16(uint16_t v) { bs_->Write(v); return true; }
	bool i16(int16_t v) { bs_->Write(v); return true; }
	bool u32(uint32_t v) { bs_->Write(v); return true; }
	bool i32(int32_t v) { bs_->Write(v); return true; }
	bool f32(float v) { bs_->Write(v); return true; }
	bool bit(bool v) { bs_->Write(v); return true; }

	bool vec2(const Vec2& v)
	{
		bs_->Write(v.x);
		bs_->Write(v.y);
		return true;
	}

	bool vec3(const Vec3& v)
	{
		bs_->Write(v.x);
		bs_->Write(v.y);
		bs_->Write(v.z);
		return true;
	}

	bool bytes(const void* buf, size_t n)
	{
		if (n > 0) {
			if (!buf) {
				ok_ = false;
				return false;
			}
			bs_->Write(reinterpret_cast<const char*>(buf), static_cast<int>(n));
		}
		return true;
	}

	bool dynStr8(const std::string& s)
	{
		const uint8_t len = static_cast<uint8_t>(std::min<size_t>(s.size(), 255));
		bs_->Write(len);
		if (len > 0) bs_->Write(s.data(), len);
		return true;
	}

	bool dynStr16(const std::string& s, uint16_t maxLen = 4096)
	{
		const uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), maxLen));
		bs_->Write(len);
		if (len > 0) bs_->Write(s.data(), static_cast<int>(len));
		return true;
	}

	bool dynStr32(const std::string& s, uint32_t maxLen = 4096)
	{
		const uint32_t len = static_cast<uint32_t>(std::min<size_t>(s.size(), maxLen));
		bs_->Write(len);
		if (len > 0) bs_->Write(s.data(), static_cast<int>(len));
		return true;
	}

	bool fixedStr(const std::string& s, size_t len)
	{
		if (len == 0) return true;
		std::string padded(len, '\0');
		const size_t copyLen = std::min(len, s.size());
		if (copyLen > 0) memcpy(padded.data(), s.data(), copyLen);
		bs_->Write(padded.data(), static_cast<int>(len));
		return true;
	}

	bool compressedStr(const std::string& s, size_t maxChars = 4096)
	{
		StringCompressor::Instance()->EncodeString(s.c_str(), static_cast<int>(maxChars + 1), bs_);
		return true;
	}

	bool normQuat(const Quat& q)
	{
		bs_->WriteNormQuat(q.w, q.x, q.y, q.z);
		return true;
	}

	bool compressedVec(const Vec3& v)
	{
		bs_->WriteVector(v.x, v.y, v.z);
		return true;
	}

	bool healthArmour(uint8_t health, uint8_t armour)
	{
		const uint8_t hl = (health >= 100) ? 0xF : static_cast<uint8_t>(std::min<unsigned>(health / 7u, 0xFu));
		const uint8_t arm = (armour >= 100) ? 0xF : static_cast<uint8_t>(std::min<unsigned>(armour / 7u, 0xFu));
		const uint8_t packed = static_cast<uint8_t>((hl << 4) | (arm & 0x0F));
		bs_->Write(packed);
		return true;
	}

	template <typename Fn>
	bool optionalTail(Fn&& fn)
	{
		if (!ok_) return false;
		return fn(*this);
	}

private:
	RakNet::BitStream* bs_;
	bool ok_ = true;
};

} // namespace proto
