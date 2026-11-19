#pragma once
// Minimal WAV reading / writing (16-bit, 24-bit and 32-bit float PCM), with resampling on read.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vega
{
inline bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;
    const uint32_t frames = (uint32_t) l.size(), dataBytes = frames * 2 * 3;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sampleRate); u32 ((uint32_t) sampleRate * 6); u16 (6); u16 (24);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    std::vector<uint8_t> buf; buf.reserve ((size_t) frames * 6);
    for (uint32_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float s = std::clamp (ch == 0 ? l[i] : (r.empty() ? l[i] : r[i]), -1.0f, 1.0f);
            const int32_t v = (int32_t) std::lround (s * 8388607.0f);
            buf.push_back ((uint8_t) (v & 0xff)); buf.push_back ((uint8_t) ((v >> 8) & 0xff)); buf.push_back ((uint8_t) ((v >> 16) & 0xff));
        }
    std::fwrite (buf.data(), 1, buf.size(), f);
    std::fclose (f);
    return true;
}

inline bool readWav (const std::string& path, int targetRate, std::vector<float>& l, std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) return false;
    std::vector<uint8_t> d;
    std::fseek (f, 0, SEEK_END); const long size = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    d.resize ((size_t) std::max (0L, size));
    const size_t got = std::fread (d.data(), 1, d.size(), f); std::fclose (f); d.resize (got);
    if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0) return false;
    int channels = 0, rate = 0, bits = 0, format = 0; size_t dataPos = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= d.size();)
    {
        uint32_t len; std::memcpy (&len, d.data() + p + 4, 4);
        if (std::memcmp (d.data() + p, "fmt ", 4) == 0 && p + 24 <= d.size())
        {
            uint16_t fmt, ch, bps; uint32_t sr;
            std::memcpy (&fmt, d.data() + p + 8, 2); std::memcpy (&ch, d.data() + p + 10, 2); std::memcpy (&sr, d.data() + p + 12, 4); std::memcpy (&bps, d.data() + p + 22, 2);
            format = fmt; channels = ch; rate = (int) sr; bits = bps;
            if (fmt == 0xfffe && p + 34 <= d.size()) { uint16_t sub; std::memcpy (&sub, d.data() + p + 32, 2); format = sub; }
        }
        else if (std::memcmp (d.data() + p, "data", 4) == 0) { dataPos = p + 8; dataLen = std::min<size_t> (len, d.size() - dataPos); break; }
        p += 8 + len + (len & 1);
    }
    if (channels < 1 || rate <= 0 || dataPos == 0 || (bits != 16 && bits != 24 && bits != 32)) return false;
    const size_t bytes = (size_t) bits / 8, frames = dataLen / (bytes * (size_t) channels);
    std::vector<float> sl (frames), sr2 (frames);
    for (size_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < std::min (channels, 2); ++ch)
        {
            const uint8_t* s = d.data() + dataPos + (i * (size_t) channels + (size_t) ch) * bytes;
            float v = 0;
            if (bits == 16) { int16_t x; std::memcpy (&x, s, 2); v = x / 32768.0f; }
            else if (bits == 24) { int32_t x = (s[0] | (s[1] << 8) | (s[2] << 16)); if (x & 0x800000) x |= ~0xffffff; v = x / 8388608.0f; }
            else if (format == 3) std::memcpy (&v, s, 4);
            else { int32_t x; std::memcpy (&x, s, 4); v = x / 2147483648.0f; }
            (ch == 0 ? sl : sr2)[i] = v;
        }
    if (channels == 1) sr2 = sl;
    if (rate == targetRate) { l = std::move (sl); r = std::move (sr2); return true; }
    const double ratio = (double) rate / targetRate;
    const size_t out = (size_t) (frames / ratio);
    l.resize (out); r.resize (out);
    for (size_t i = 0; i < out; ++i)
    {
        const double p = i * ratio; const size_t a = (size_t) p; const size_t b = std::min (a + 1, frames - 1); const float t = (float) (p - a);
        l[i] = sl[a] + (sl[b] - sl[a]) * t; r[i] = sr2[a] + (sr2[b] - sr2[a]) * t;
    }
    return true;
}
}
