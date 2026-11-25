#include "encoding.h"

#include <cstdint>
#include <cstring>
#include <vector>

constexpr Encoding::Letter Encoding::m_letters[];

std::string Encoding::cp2utf(const std::string& s)
{
    int c, i;
    int len = s.size();
    std::string ns;
    for (i = 0; i < len; i++)
    {
        c = s[i];
        char buf[4]{};
        char in[2] = { 0, 0 };
        *in = c;
        convert_windows1251_to_utf8(buf, in, strlen(in));
        ns += std::string(buf);
    }
    return ns;
}

std::string Encoding::utf2cp(const std::string& s)
{
    std::vector<char> output(s.size() + 1, '\0');
    if (!convert_utf8_to_windows1251(s.c_str(), output.data(), s.size()))
        return {};
    return std::string(output.data());
}

bool Encoding::convert_utf8_to_windows1251(const char* utf8, char* windows1251, size_t n)
{
    size_t i = 0;
    size_t j = 0;
    while (i < n)
    {
        const auto first = static_cast<unsigned char>(utf8[i]);
        uint32_t codePoint = 0;
        size_t sequenceLength = 0;
        if (first <= 0x7F)
        {
            codePoint = first;
            sequenceLength = 1;
        }
        else if (first >= 0xC2 && first <= 0xDF)
        {
            codePoint = first & 0x1F;
            sequenceLength = 2;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            codePoint = first & 0x0F;
            sequenceLength = 3;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            codePoint = first & 0x07;
            sequenceLength = 4;
        }
        else
        {
            return false;
        }

        if (i + sequenceLength > n)
            return false;

        for (size_t jByte = 1; jByte < sequenceLength; ++jByte)
        {
            const auto next = static_cast<unsigned char>(utf8[i + jByte]);
            if ((next & 0xC0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3F);
        }

        if ((sequenceLength == 2 && codePoint < 0x80) ||
            (sequenceLength == 3 && codePoint < 0x800) ||
            (sequenceLength == 4 && codePoint < 0x10000) ||
            codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        {
            return false;
        }

        if (codePoint <= 0x7F)
        {
            windows1251[j++] = static_cast<char>(codePoint);
        }
        else if (codePoint >= 0x0410 && codePoint <= 0x044F)
        {
            windows1251[j++] = static_cast<char>(codePoint - 0x0350);
        }
        else if (codePoint >= 0x80 && codePoint <= 0xFF)
        {
            windows1251[j++] = static_cast<char>(codePoint);
        }
        else if (codePoint >= 0x0402 && codePoint <= 0x0403)
        {
            windows1251[j++] = static_cast<char>(codePoint - 0x0382);
        }
        else
        {
            bool converted = false;
            for (const Letter& letter : m_letters)
            {
                if (codePoint == static_cast<uint32_t>(letter.unicode))
                {
                    windows1251[j++] = static_cast<char>(letter.win1251);
                    converted = true;
                    break;
                }
            }
            if (!converted)
                return false;
        }

        i += sequenceLength;
    }

    windows1251[j] = '\0';
    return true;
}

void Encoding::convert_windows1251_to_utf8(char* windows1251, const char* utf8, unsigned int len)
{
    static const int table[128] = {

            0x82D0,    0x83D0,    0x9A80E2,  0x93D1,    0x9E80E2,  0xA680E2,  0xA080E2,  0xA180E2,
            0xAC82E2,  0xB080E2,  0x89D0,    0xB980E2,  0x8AD0,    0x8CD0,    0x8BD0,    0x8FD0,
            0x92D1,    0x9880E2,  0x9980E2,  0x9C80E2,  0x9D80E2,  0xA280E2,  0x9380E2,  0x9480E2,
            0,      0xA284E2,  0x99D1,    0xBA80E2,  0x9AD1,    0x9CD1,    0x9BD1,    0x9FD1,
            0xA0C2,    0x8ED0,    0x9ED1,    0x88D0,    0xA4C2,    0x90D2,    0xA6C2,    0xA7C2,
            0x81D0,    0xA9C2,    0x84D0,    0xABC2,    0xACC2,    0xADC2,    0xAEC2,    0x87D0,
            0xB0C2,    0xB1C2,    0x86D0,    0x96D1,    0x91D2,    0xB5C2,    0xB6C2,    0xB7C2,
            0x91D1,    0x9684E2,  0x94D1,    0xBBC2,    0x98D1,    0x85D0,    0x95D1,    0x97D1,
            0x90D0,    0x91D0,    0x92D0,    0x93D0,    0x94D0,    0x95D0,    0x96D0,    0x97D0,
            0x98D0,    0x99D0,    0x9AD0,    0x9BD0,    0x9CD0,    0x9DD0,    0x9ED0,    0x9FD0,
            0xA0D0,    0xA1D0,    0xA2D0,    0xA3D0,    0xA4D0,    0xA5D0,    0xA6D0,    0xA7D0,
            0xA8D0,    0xA9D0,    0xAAD0,    0xABD0,    0xACD0,    0xADD0,    0xAED0,    0xAFD0,
            0xB0D0,    0xB1D0,    0xB2D0,    0xB3D0,    0xB4D0,    0xB5D0,    0xB6D0,    0xB7D0,
            0xB8D0,    0xB9D0,    0xBAD0,    0xBBD0,    0xBCD0,    0xBDD0,    0xBED0,    0xBFD0,
            0x80D1,    0x81D1,    0x82D1,    0x83D1,    0x84D1,    0x85D1,    0x86D1,    0x87D1,
            0x88D1,    0x89D1,    0x8AD1,    0x8BD1,    0x8CD1,    0x8DD1,    0x8ED1,    0x8FD1

    };

    int count = 0;

    while (*utf8)
    {
        if(len && (count >= len)) break;

        if (*utf8 & 0x80)
        {
            int v = table[(int)(0x7f & *utf8++)];
            if (!v) continue;
            *windows1251++ = (char)v;
            *windows1251++ = (char)(v >> 8);
            if (v >>= 16) {
                *windows1251++ = (char)v;
            }
        }
        else
        {
            *windows1251++ = *utf8++;
        }

        count++;
    }

    *windows1251 = 0;
}