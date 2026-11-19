#pragma once
// Vega Sketch - sound engines: synthesized drum kits and a small poly synth.
// Plain C++17, no platform code, so the same sound runs on the phone and in the desktop tests.
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <memory>

namespace vega
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

struct Noise
{
    uint32_t s = 0x9e3779b9u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

/** State-variable filter (topology-preserving, stable at any cutoff): low / band / high pass. */
struct Svf
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1, band = 0;
    float lastHz = -1, lastQ = -1;
    void set (float hz, float reso, float sr)
    {
        hz = std::clamp (hz, 20.0f, sr * 0.48f);
        if (hz == lastHz && reso == lastQ) return;
        lastHz = hz; lastQ = reso;
        const float g = std::tan (kPi * hz / sr);
        k = 1.0f / std::max (0.5f, reso);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process (float in, int mode)  // 0 lp 1 bp 2 hp
    {
        const float v3 = in - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        band = v1;
        return mode == 0 ? v2 : mode == 1 ? v1 : in - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = band = 0; lastHz = -1; }
};

inline float softClip (float x) { return x / (1.0f + std::abs (x)); }

//============================================================================== drums
/** The 16 pads, General MIDI drum notes (they line up with Vega Productions Drums on the desktop). */
inline const std::array<int, 16>& padNotes()
{
    static const std::array<int, 16> n { 36, 38, 39, 42,   37, 40, 44, 46,   41, 45, 48, 49,   56, 70, 54, 51 };
    return n;
}
inline const char* drumName (int note)
{
    switch (note)
    {
        case 36: return "Kick";  case 37: return "Rim";    case 38: return "Snare"; case 39: return "Clap";
        case 40: return "Snare 2"; case 41: return "Low Tom"; case 42: return "Closed Hat"; case 44: return "Pedal Hat";
        case 45: return "Mid Tom"; case 46: return "Open Hat"; case 48: return "High Tom"; case 49: return "Crash";
        case 51: return "Ride";  case 54: return "Tambourine"; case 56: return "Cowbell"; case 70: return "Shaker";
        default: return "Perc";
    }
}

struct KitParams
{
    const char* name;
    float kickTune, kickDecay, kickPitch, kickClick, kickDrive;
    float snareTune, snareTone, snareDecay, snareSnap;
    float clapDecay, hatDecay, hatTone, openDecay, tomTune, cymbalDecay, drive, lofi;
};

inline const std::vector<KitParams>& kits()
{
    static const std::vector<KitParams> k {
        //  name            kTune kDec  kPitch kClick kDrive  sTune sTone sDec  sSnap  clap  hat   hatTone open  tom   cym   drive lofi
        { "House Classic", 52.f, 0.42f, 0.045f, 0.55f, 0.35f, 190.f, 0.35f, 0.16f, 0.8f, 0.16f, 0.045f, 8500.f, 0.32f, 1.0f, 1.2f, 0.15f, 0.f },
        { "Vega 808",      45.f, 0.95f, 0.030f, 0.25f, 0.20f, 175.f, 0.50f, 0.20f, 0.6f, 0.20f, 0.040f, 7800.f, 0.45f, 0.9f, 1.5f, 0.10f, 0.f },
        { "Vega 909",      56.f, 0.35f, 0.055f, 0.80f, 0.55f, 205.f, 0.30f, 0.15f, 1.0f, 0.14f, 0.035f, 9500.f, 0.30f, 1.1f, 1.1f, 0.25f, 0.f },
        { "Deep Soul",     48.f, 0.60f, 0.040f, 0.35f, 0.25f, 180.f, 0.45f, 0.22f, 0.7f, 0.22f, 0.050f, 7000.f, 0.40f, 0.85f, 1.6f, 0.10f, 0.f },
        { "Lo-Fi Dusty",   50.f, 0.45f, 0.040f, 0.40f, 0.60f, 185.f, 0.40f, 0.18f, 0.7f, 0.18f, 0.045f, 6000.f, 0.35f, 0.95f, 1.0f, 0.35f, 1.f },
    };
    return k;
}

/** Recorded drum hits, one per MIDI note (at the engine sample rate) */
struct SampleKit
{
    struct Hit { std::vector<float> l, r; };
    std::array<std::shared_ptr<const Hit>, 128> byNote {};
};

class DrumKit
{
public:
    void prepare (float sampleRate) { sr = sampleRate; }
    void setKit (int k) { kit = std::clamp (k, 0, (int) kits().size() - 1); }
    int getKit() const { return kit; }
    void setSampleKit (std::shared_ptr<const SampleKit> k) { if (k != samples) { for (auto& v : sampleVoices) v.hit.reset(); samples = std::move (k); } }
    const std::shared_ptr<const SampleKit>& getSampleKit() const { return samples; }

    void trigger (int note, int velocity)
    {
        if (samples != nullptr)
        {
            if (note == 42 || note == 44) for (auto& v : sampleVoices) if (v.hit && v.note == 46) v.choke = true;
            auto hit = samples->byNote[(size_t) std::clamp (note, 0, 127)];
            if (hit == nullptr || hit->l.empty()) return;
            SampleVoice* v = nullptr;
            for (auto& c : sampleVoices) if (! c.hit) { v = &c; break; }
            if (v == nullptr) { v = &sampleVoices[0]; for (auto& c : sampleVoices) if (c.pos > v->pos) v = &c; }
            const float vel = std::clamp (velocity, 1, 127) / 127.0f;
            *v = SampleVoice(); v->hit = hit; v->note = note; v->gain = 0.25f + 0.75f * vel * vel;
            return;
        }
        if (note == 42 || note == 44) for (auto& v : voices) if (v.active && v.note == 46) v.choke = true;   // closed hat chokes the open hat
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.t > v->t) v = &c; }   // steal the oldest
        *v = Voice();
        v->active = true; v->note = note; v->vel = std::clamp (velocity, 1, 127) / 127.0f;
        v->noise.s = 0x1234567u + (uint32_t) note * 7919u + seed++;
        const auto& p = kits()[(size_t) kit];
        auto tomBase = [&p] (int n) { return (n == 41 ? 92.f : n == 45 ? 128.f : 172.f) * p.tomTune; };
        if (note == 41 || note == 45 || note == 48) v->f0 = tomBase (note);
        v->f1.reset(); v->f2.reset();
    }

    /** Adds into the buffers */
    void render (float* l, float* r, int n)
    {
        for (auto& v : sampleVoices)
        {
            if (! v.hit) continue;
            const auto& h = *v.hit;
            const size_t len = h.l.size();
            for (int i = 0; i < n; ++i)
            {
                if (v.pos >= len) { v.hit.reset(); break; }
                float g = v.gain;
                if (v.choke) { v.chokeGain *= 0.993f; g *= v.chokeGain; if (v.chokeGain < 0.001f) { v.hit.reset(); break; } }
                l[i] += h.l[v.pos] * g; r[i] += (h.r.empty() ? h.l[v.pos] : h.r[v.pos]) * g;
                ++v.pos;
            }
        }
        if (samples != nullptr) return;
        const auto& p = kits()[(size_t) kit];
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const float pan = panFor (v.note), gl = std::sqrt (1.0f - pan) * 0.9f, gr = std::sqrt (pan) * 0.9f;
            for (int i = 0; i < n; ++i)
            {
                const float s = voiceSample (v, p) * v.vel;
                l[i] += s * gl; r[i] += s * gr;
                v.t += 1.0f / sr;
                if (v.choke) { v.chokeGain *= 0.995f; if (v.chokeGain < 0.001f) { v.active = false; break; } }
                if (v.t > v.maxTime) { v.active = false; break; }
            }
        }
    }
    void allOff() { for (auto& v : voices) v.active = false; for (auto& v : sampleVoices) v.hit.reset(); }

private:
    struct SampleVoice { std::shared_ptr<const SampleKit::Hit> hit; int note = 0; size_t pos = 0; float gain = 1, chokeGain = 1; bool choke = false; };
    std::shared_ptr<const SampleKit> samples;
    std::array<SampleVoice, 24> sampleVoices {};
    struct Voice
    {
        bool active = false, choke = false; int note = 0; float vel = 1, t = 0, phase = 0, phase2 = 0, f0 = 0, maxTime = 3.0f, chokeGain = 1.0f;
        float metal[6] {}; Svf f1, f2; Noise noise; float lofiHold = 0; int lofiCount = 0;
    };
    float panFor (int note) const
    {
        switch (note) { case 42: case 44: case 46: return 0.62f; case 41: return 0.38f; case 48: return 0.62f; case 49: return 0.40f; case 51: return 0.60f;
                        case 70: return 0.66f; case 54: return 0.34f; case 56: return 0.45f; default: return 0.5f; }
    }
    float metallic (Voice& v, float scale)
    {   // six detuned square waves, the classic analogue cymbal / hat source
        static const float ratios[6] { 1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f };
        float s = 0;
        for (int i = 0; i < 6; ++i) { v.metal[i] += 205.3f * scale * ratios[i] / sr; v.metal[i] -= std::floor (v.metal[i]); s += v.metal[i] < 0.5f ? 1.0f : -1.0f; }
        return s / 6.0f;
    }
    float voiceSample (Voice& v, const KitParams& p)
    {
        const float t = v.t;
        float out = 0;
        switch (v.note)
        {
            case 36:    // kick: sine with a fast pitch drop, a click, a little drive
            {
                const float f = p.kickTune * (1.0f + 4.0f * std::exp (-t / p.kickPitch));
                v.phase += f / sr; v.phase -= std::floor (v.phase);
                const float body = std::sin (kTwoPi * v.phase) * std::exp (-t / p.kickDecay);
                const float click = v.noise.next() * std::exp (-t / 0.0015f) * p.kickClick;
                out = std::tanh ((body + click) * (1.0f + p.kickDrive * 3.0f)) * 0.95f;
                v.maxTime = p.kickDecay * 6;
                break;
            }
            case 38: case 40:   // snare: two tones plus band-passed noise
            {
                const float tune = p.snareTune * (v.note == 40 ? 1.12f : 1.0f);
                v.phase += tune * (1.0f + 0.5f * std::exp (-t / 0.01f)) / sr; v.phase -= std::floor (v.phase);
                v.phase2 += tune * 1.62f / sr; v.phase2 -= std::floor (v.phase2);
                const float tone = (std::sin (kTwoPi * v.phase) + 0.5f * std::sin (kTwoPi * v.phase2)) * std::exp (-t / 0.06f) * p.snareTone;
                v.f1.set (v.note == 40 ? 2600.f : 1900.f, 0.8f, sr);
                const float nz = v.f1.process (v.noise.next(), 2) * std::exp (-t / p.snareDecay) * p.snareSnap;
                out = (tone + nz * 1.4f) * 0.8f;
                v.maxTime = p.snareDecay * 7;
                break;
            }
            case 39:    // clap: three quick bursts then a tail
            {
                float env = 0;
                for (int b = 0; b < 3; ++b) { const float tb = t - b * 0.011f; if (tb >= 0) env = std::max (env, std::exp (-tb / 0.006f)); }
                env = std::max (env, 0.55f * std::exp (-std::max (0.0f, t - 0.022f) / p.clapDecay) * (t > 0.022f ? 1.0f : 0.0f));
                v.f1.set (1250.f, 2.2f, sr);
                out = v.f1.process (v.noise.next(), 1) * env * 2.8f;
                v.maxTime = p.clapDecay * 7;
                break;
            }
            case 42: case 44: case 46:  // hats: metallic squares + noise, high-passed
            {
                const float dec = v.note == 46 ? p.openDecay : v.note == 44 ? p.hatDecay * 0.6f : p.hatDecay;
                v.f1.set (p.hatTone, 1.1f, sr);
                const float src = metallic (v, 2.0f) * 0.6f + v.noise.next() * 0.5f;
                const float hp = v.f1.process (src, 2);
                const float env = std::exp (-t / dec) * (v.note == 44 ? std::min (1.0f, t / 0.003f) : 1.0f);
                out = hp * env * 0.75f * v.chokeGain;
                v.maxTime = dec * 7;
                break;
            }
            case 41: case 45: case 48:  // toms
            {
                const float f = v.f0 * (1.0f + 0.7f * std::exp (-t / 0.04f));
                v.phase += f / sr; v.phase -= std::floor (v.phase);
                out = (std::sin (kTwoPi * v.phase) + 0.08f * v.noise.next() * std::exp (-t / 0.01f)) * std::exp (-t / 0.32f) * 0.85f;
                v.maxTime = 2.0f;
                break;
            }
            case 49: case 51:   // crash / ride
            {
                const bool crash = v.note == 49;
                v.f1.set (crash ? 4800.f : 3200.f, 0.9f, sr);
                v.f2.set (crash ? 9000.f : 6500.f, 1.5f, sr);
                const float src = metallic (v, crash ? 1.6f : 2.4f) * (crash ? 0.5f : 0.8f) + v.noise.next() * (crash ? 0.7f : 0.25f);
                const float s = v.f2.process (v.f1.process (src, 2), 1) + (crash ? 0.0f : v.f1.band * 0.2f);
                const float dec = crash ? p.cymbalDecay : p.cymbalDecay * 0.8f;
                out = s * std::exp (-t / dec) * (crash ? 1.3f : 0.8f);
                v.maxTime = dec * 6;
                break;
            }
            case 56:    // cowbell: two squares, band-passed
            {
                v.phase += 540.f / sr; v.phase -= std::floor (v.phase);
                v.phase2 += 800.f / sr; v.phase2 -= std::floor (v.phase2);
                const float sq = (v.phase < 0.5f ? 1.f : -1.f) + (v.phase2 < 0.5f ? 1.f : -1.f);
                v.f1.set (900.f, 3.0f, sr);
                out = v.f1.process (sq, 1) * (0.25f + 0.75f * std::exp (-t / 0.03f)) * std::exp (-t / 0.22f) * 0.9f;
                v.maxTime = 1.2f;
                break;
            }
            case 70: case 54:   // shaker / tambourine
            {
                const bool tamb = v.note == 54;
                v.f1.set (tamb ? 7200.f : 6000.f, 1.0f, sr);
                float s = v.f1.process (v.noise.next(), 2);
                if (tamb) s += metallic (v, 3.2f) * 0.25f * std::exp (-t / 0.05f);
                const float att = std::min (1.0f, t / (tamb ? 0.002f : 0.008f));
                out = s * att * std::exp (-t / (tamb ? 0.11f : 0.055f)) * 0.8f;
                v.maxTime = 0.8f;
                break;
            }
            case 37:    // rim
            {
                v.phase += 1700.f / sr; v.phase -= std::floor (v.phase);
                out = (std::sin (kTwoPi * v.phase) * 0.7f + v.noise.next() * 0.6f * std::exp (-t / 0.002f)) * std::exp (-t / 0.018f);
                v.maxTime = 0.25f;
                break;
            }
            default:    // clave-ish
            {
                v.phase += 2500.f / sr; v.phase -= std::floor (v.phase);
                out = std::sin (kTwoPi * v.phase) * std::exp (-t / 0.03f) * 0.7f;
                v.maxTime = 0.3f;
            }
        }
        out = softClip (out * (1.0f + p.drive)) * (1.0f + p.drive * 0.5f);
        if (p.lofi > 0)
        {   // sample-rate + bit crush for the dusty kit
            if (v.lofiCount++ % 3 == 0) v.lofiHold = std::round (out * 48.0f) / 48.0f;
            out = v.lofiHold;
        }
        return out;
    }
    float sr = 48000.f;
    int kit = 0;
    uint32_t seed = 1;
    std::array<Voice, 24> voices {};
};

//============================================================================== warmth
/** Tape-style colour: gentle asymmetric saturation, a softer top end and a touch more low end. 0 = clean. */
struct Warmth
{
    float amount = 0, lpL = 0, lpR = 0, lsL = 0, lsR = 0, coeff = 1, lowCoeff = 0.01f, sr = 48000;
    void prepare (float sampleRate) { sr = sampleRate; set (amount); }
    void set (float a)
    {
        amount = std::clamp (a, 0.0f, 1.0f);
        const float cutoff = 20000.0f - amount * 12500.0f;            // 20 kHz -> 7.5 kHz
        coeff = 1.0f - std::exp (-kTwoPi * cutoff / sr);
        lowCoeff = 1.0f - std::exp (-kTwoPi * 120.0f / sr);
    }
    inline float sat (float x) const
    {
        // unity gain for quiet signals, so warmth changes the colour (and rounds off peaks), not the level
        const float d = 1.0f + amount * 2.2f, bias = 0.12f * amount, tb = std::tanh (bias);
        return (std::tanh (d * x + bias) - tb) / (d * (1.0f - tb * tb));
    }
    void process (float* l, float* r, int n)
    {
        if (amount < 0.001f) return;
        const float lowBoost = amount * 0.35f;
        for (int i = 0; i < n; ++i)
        {
            lsL += lowCoeff * (l[i] - lsL); lsR += lowCoeff * (r[i] - lsR);
            const float xl = sat (l[i] + lsL * lowBoost), xr = sat (r[i] + lsR * lowBoost);
            lpL += coeff * (xl - lpL); lpR += coeff * (xr - lpR);
            l[i] = lpL; r[i] = lpR;
        }
    }
};

//============================================================================== synth
enum class Wave { sine, saw, square, triangle, fm, organ };

struct SynthPreset
{
    const char* name;
    Wave w1, w2; float osc2Semi, detuneCents, mix;      // mix = osc2 level
    float fmRatio, fmIndex, fmDecay;                    // for Wave::fm (electric piano)
    float cutoff, reso, envAmt, keyTrack;
    float a, d, s, r;                                   // amp envelope
    float fa, fd, fs, fr;                               // filter envelope
    float tremolo, gain;
};

inline const std::vector<SynthPreset>& presets()
{
    static const std::vector<SynthPreset> p {
        //  name            w1           w2             semi  det    mix   fmR  fmI   fmD   cut     res   env     key   a      d     s     r      fa    fd    fs    fr    trem gain
        { "Soul Keys",     Wave::fm,     Wave::sine,    0.f,  0.f,   0.0f, 1.0f, 2.2f, 0.9f, 9000.f, 0.6f, 0.f,    0.f, 0.002f, 2.2f, 0.25f, 0.45f, 0.01f, 0.5f, 1.0f, 0.5f, 0.25f, 0.55f },
        { "Warm Pad",      Wave::saw,    Wave::saw,     0.f,  14.f,  0.9f, 1.f,  0.f,  1.f,  900.f,  0.9f, 1400.f, 0.3f, 0.70f, 1.2f, 0.80f, 1.40f, 1.20f, 2.0f, 0.4f, 1.5f, 0.0f, 0.30f },
        { "Deep Bass",     Wave::square, Wave::sine,   -12.f, 0.f,   0.8f, 1.f,  0.f,  1.f,  320.f,  1.4f, 1500.f, 0.5f, 0.002f, 0.4f, 0.70f, 0.08f, 0.001f, 0.18f, 0.0f, 0.1f, 0.0f, 0.55f },
        { "House Organ",   Wave::organ,  Wave::sine,    0.f,  0.f,   0.0f, 1.f,  0.f,  1.f,  7000.f, 0.6f, 0.f,    0.f, 0.004f, 0.1f, 1.00f, 0.08f, 0.01f, 0.1f, 1.0f, 0.1f, 0.35f, 0.40f },
        { "Pluck",         Wave::saw,    Wave::square,  12.f, 5.f,   0.3f, 1.f,  0.f,  1.f,  500.f,  1.2f, 4500.f, 0.6f, 0.001f, 0.45f, 0.0f, 0.25f, 0.001f, 0.22f, 0.0f, 0.2f, 0.0f, 0.45f },
        { "Strings",       Wave::saw,    Wave::saw,     0.f,  20.f,  1.0f, 1.f,  0.f,  1.f,  2600.f, 0.7f, 600.f,  0.3f, 0.35f, 1.0f, 0.85f, 0.90f, 0.5f, 1.0f, 0.6f, 1.0f, 0.0f, 0.28f },
        { "Bright Lead",   Wave::saw,    Wave::square,  0.f,  8.f,   0.6f, 1.f,  0.f,  1.f,  2400.f, 1.6f, 2500.f, 0.6f, 0.005f, 0.4f, 0.75f, 0.25f, 0.005f, 0.4f, 0.4f, 0.3f, 0.0f, 0.32f },
        { "Garage Bass",   Wave::saw,    Wave::square, -12.f, 4.f,   0.5f, 1.f,  0.f,  1.f,  420.f,  2.2f, 2200.f, 0.4f, 0.002f, 0.3f, 0.60f, 0.10f, 0.001f, 0.25f, 0.1f, 0.1f, 0.0f, 0.45f },
    };
    return p;
}

class PolySynth
{
public:
    void prepare (float sampleRate) { sr = sampleRate; }
    void setPreset (int p) { preset = std::clamp (p, 0, (int) presets().size() - 1); }
    int getPreset() const { return preset; }

    void noteOn (int note, int velocity)
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (c.active && c.note == note) { v = &c; break; }      // re-trigger the same key
        if (v == nullptr) for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.age > v->age) v = &c; }
        const float keep1 = v->ph1, keep2 = v->ph2;
        *v = Voice();
        v->ph1 = keep1; v->ph2 = keep2;
        v->active = true; v->note = note; v->vel = std::clamp (velocity, 1, 127) / 127.0f;
        v->freq = 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
    }
    void noteOff (int note) { for (auto& v : voices) if (v.active && v.note == note && ! v.released) { v.released = true; v.relLevel = v.amp; v.relF = v.fenv; v.rt = 0; } }
    void allOff() { for (auto& v : voices) v.active = false; }

    void render (float* l, float* r, int n)
    {
        const auto& p = presets()[(size_t) preset];
        const float det = std::pow (2.0f, p.detuneCents / 1200.0f), semi = std::pow (2.0f, p.osc2Semi / 12.0f);
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const float panL = 0.5f + 0.15f * ((v.note % 5) - 2) / 2.0f;
            for (int i = 0; i < n; ++i)
            {
                // envelopes
                const float dt = 1.0f / sr;
                if (! v.released)
                {
                    v.amp = v.t < p.a ? v.t / p.a : p.s + (1.0f - p.s) * std::exp (-(v.t - p.a) / std::max (0.01f, p.d * 0.35f));
                    v.fenv = v.t < p.fa ? v.t / p.fa : p.fs + (1.0f - p.fs) * std::exp (-(v.t - p.fa) / std::max (0.01f, p.fd * 0.35f));
                }
                else
                {
                    v.amp = v.relLevel * std::exp (-v.rt / std::max (0.005f, p.r * 0.3f));
                    v.fenv = v.relF * std::exp (-v.rt / std::max (0.005f, p.fr * 0.3f));
                    v.rt += dt;
                    if (v.amp < 0.0005f) { v.active = false; break; }
                }
                v.t += dt; v.age++;
                // oscillators
                float s = osc (p.w1, v, v.freq, v.ph1, v.fm, p) * (1.0f - (p.w1 == Wave::fm || p.w1 == Wave::organ ? 0.0f : p.mix * 0.5f));
                if (p.mix > 0 && p.w1 != Wave::fm && p.w1 != Wave::organ) s += osc (p.w2, v, v.freq * semi * det, v.ph2, v.fm, p) * p.mix * 0.5f;
                // filter
                const float cut = p.cutoff * std::pow (v.freq / 261.6f, p.keyTrack) + p.envAmt * v.fenv;
                v.filt.set (std::clamp (cut, 40.0f, sr * 0.45f), p.reso, sr);
                s = v.filt.process (s, 0);
                if (p.tremolo > 0) s *= 1.0f - p.tremolo * 0.5f * (1.0f + std::sin (kTwoPi * 4.8f * v.t));
                s *= v.amp * (0.4f + 0.6f * v.vel) * p.gain;
                l[i] += s * std::sqrt (1.0f - panL) * 1.3f; r[i] += s * std::sqrt (panL) * 1.3f;
            }
        }
    }

private:
    struct Voice
    {
        bool active = false, released = false; int note = 0; float vel = 1, freq = 440, t = 0, rt = 0, amp = 0, fenv = 0, relLevel = 0, relF = 0;
        float ph1 = 0, ph2 = 0, fm = 0; uint64_t age = 0; Svf filt;
    };
    static float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    float osc (Wave w, Voice& v, float f, float& ph, float& fmPh, const SynthPreset& p)
    {
        const float dt = f / sr;
        float out = 0;
        switch (w)
        {
            case Wave::sine: out = std::sin (kTwoPi * ph); break;
            case Wave::saw: out = 2.0f * ph - 1.0f - polyBlep (ph, dt); break;
            case Wave::square: { out = ph < 0.5f ? 1.0f : -1.0f; out += polyBlep (ph, dt); float t2 = ph + 0.5f; t2 -= std::floor (t2); out -= polyBlep (t2, dt); break; }
            case Wave::triangle: out = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
            case Wave::fm:
            {   // two-operator electric piano: a bright "tine" that mellows as it rings
                fmPh += f * p.fmRatio / sr; fmPh -= std::floor (fmPh);
                const float index = p.fmIndex * (0.25f + 0.75f * std::exp (-v.t / p.fmDecay)) * (0.5f + 0.5f * v.vel);
                out = std::sin (kTwoPi * ph + index * std::sin (kTwoPi * fmPh));
                out += 0.18f * std::sin (kTwoPi * fmPh * 14.0f) * std::exp (-v.t / 0.02f) * v.vel;     // bark of the tine
                break;
            }
            case Wave::organ:
            {   // drawbars 8' 4' 2 2/3' 2' 1'
                out = std::sin (kTwoPi * ph) + 0.7f * std::sin (kTwoPi * ph * 2) + 0.45f * std::sin (kTwoPi * ph * 3) + 0.35f * std::sin (kTwoPi * ph * 4) + 0.2f * std::sin (kTwoPi * ph * 8);
                out *= 0.38f;
                out += 0.15f * std::exp (-v.t / 0.008f) * std::sin (kTwoPi * ph * 5);                   // key click
                break;
            }
        }
        ph += dt; ph -= std::floor (ph);
        return out;
    }
    float sr = 48000.f;
    int preset = 0;
    std::array<Voice, 12> voices {};
};
}
