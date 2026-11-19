// Desktop test of the phone engine: g++ -std=c++17 -O2 -I../../main/cpp engine_test.cpp ../../main/cpp/SketchEngine.cpp
#include "SketchEngine.h"
#include "Wav.h"
#include <cstdio>
#include <string>

using namespace vega;
static int failures = 0;
static void check (bool ok, const std::string& what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (! ok) ++failures; }

static float peakOf (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float p = 0; for (size_t i = from; i < std::min (to, v.size()); ++i) p = std::max (p, std::abs (v[i])); return p;
}
static void run (SketchEngine& e, double seconds, std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr, const std::vector<float>* micIn = nullptr)
{
    const int block = 192; const size_t total = (size_t) (seconds * e.sampleRate());
    std::vector<float> l (block), r (block);
    for (size_t done = 0; done < total; done += block)
    {
        if (micIn != nullptr) e.pushInput (micIn->data() + (done % (micIn->size() - block)), block);
        e.render (l.data(), r.data(), block);
        if (outL) { outL->insert (outL->end(), l.begin(), l.end()); outR->insert (outR->end(), r.begin(), r.end()); }
    }
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    // every drum sound and every synth preset makes a sound and stays in range
    {
        DrumKit k; k.prepare (48000);
        for (int kit = 0; kit < (int) kits().size(); ++kit)
        {
            k.setKit (kit);
            float worst = 1e9f, loudest = 0;
            for (int note : padNotes())
            {
                std::vector<float> l (48000), r (48000);
                k.allOff(); k.trigger (note, 110); k.render (l.data(), r.data(), 48000);
                const float p = peakOf (l);
                worst = std::min (worst, p); loudest = std::max (loudest, p);
            }
            check (worst > 0.02f && loudest < 2.0f, std::string ("kit ") + kits()[(size_t) kit].name + ": all 16 pads sound (quietest " + std::to_string (worst) + ", loudest " + std::to_string (loudest) + ")");
        }
        PolySynth s; s.prepare (48000);
        for (int pr = 0; pr < (int) presets().size(); ++pr)
        {
            s.setPreset (pr); s.allOff();
            std::vector<float> l (96000), r (96000);
            s.noteOn (48, 100); s.noteOn (55, 100); s.noteOn (64, 100);
            s.render (l.data(), r.data(), 48000);
            s.noteOff (48); s.noteOff (55); s.noteOff (64);
            s.render (l.data() + 48000, r.data() + 48000, 48000);
            const float held = peakOf (l, 2000, 40000), tail = peakOf (l, 90000);
            check (held > 0.02f && held < 2.0f && tail < held * 0.2f, std::string ("preset ") + presets()[(size_t) pr].name + " plays a chord and releases (" + std::to_string (held) + ")");
        }
    }
    // the launcher: a 1-bar beat, kick on every beat, at 120 BPM
    {
        SketchEngine e; e.prepare (48000); e.setTempo (120);
        e.setTrack (0, TrackType::drums, 0, 1.0f, 0.0f, false, false);
        std::vector<NoteEv> beat { { 36, 0, 0.25, 120 }, { 36, 1, 0.25, 120 }, { 36, 2, 0.25, 120 }, { 36, 3, 0.25, 120 } };
        e.setClipNotes (0, 0, 4.0, beat);
        e.launchClip (0, 0);
        std::vector<float> l, r; run (e, 4.0, &l, &r);
        // a kick every 0.5 s: check energy right after each beat, and quiet just before the next one
        int hits = 0;
        for (int b = 0; b < 8; ++b)
        {
            const size_t at = (size_t) (b * 24000);
            if (peakOf (l, at, at + 2400) > 0.2f) ++hits;
        }
        check (hits == 8, "clip loops: 8 kicks in 4 seconds at 120 BPM (" + std::to_string (hits) + ")");
        writeWav (dir + "/sketch_beat.wav", l, r, 48000);
        auto st = e.state();
        check (st[1] == 1.0 && (int) st[6] == 0, "state reports track 1 playing scene 1");
        // launching another clip waits for the bar line
        e.setClipNotes (0, 1, 4.0, { { 38, 0, 0.25, 120 } });
        e.launchClip (0, 1);
        st = e.state();
        check ((int) st[6] == 0 && (int) st[7] == 1, "a new launch is queued until the next bar");
        run (e, 2.1);
        st = e.state();
        check ((int) st[6] == 1, "... and plays from the bar line");
        e.stopTrack (0); run (e, 2.1);
        check ((int) e.state()[6] == -1, "stop track (at the bar line)");
    }
    // recording MIDI into a slot with a count-in, quantised, looping overdub
    {
        SketchEngine e; e.prepare (48000); e.setTempo (120);
        e.setTrack (1, TrackType::keys, 0, 0.8f, 0.0f, false, false);
        e.setQuantiseRecording (0.25);
        e.startRecording (1, 2, 1, false);
        check (e.state()[3] == 1.0, "count-in before recording");
        run (e, 2.0);                            // the 1-bar count-in at 120 = 2 s
        run (e, 0.26);                           // a bit after the downbeat (beat ~0.5)
        e.noteOn (1, 60, 100); run (e, 0.2); e.noteOff (1, 60);
        run (e, 1.0);
        e.noteOn (1, 64, 90); run (e, 0.3); e.noteOff (1, 64);
        run (e, 1.0);                            // wraps into the second pass
        e.noteOn (1, 67, 80); run (e, 0.2); e.noteOff (1, 67);   // overdub on the 2nd pass
        e.stopRecording();
        auto notes = e.clipNotes (1, 2);
        bool grid = true; for (auto& n : notes) if (std::abs (n.start * 4 - std::round (n.start * 4)) > 1e-6 || n.start >= 4.0) grid = false;
        check (notes.size() == 3 && grid, "recorded 3 notes over two passes, on the 1/16 grid, inside the 1-bar loop (" + std::to_string (notes.size()) + ")");
        if (notes.size() == 3) check (notes[0].note == 60 && std::abs (notes[0].start - 0.5) < 0.01, "first note lands at beat 1.5 (" + std::to_string (notes[0].start) + ")");
        check ((int) e.state()[6 + 4] == 2, "the recorded slot keeps playing after recording");
    }
    // recording audio from the mic into a slot
    {
        SketchEngine e; e.prepare (48000); e.setTempo (120);
        e.setTrack (2, TrackType::audio, 0, 1.0f, 0.0f, false, false);
        std::vector<float> mic (48000);
        for (size_t i = 0; i < mic.size(); ++i) mic[i] = 0.4f * std::sin (2 * kPi * 330.0f * i / 48000.0f);
        e.startRecording (2, 0, 1, false);
        run (e, 4.5, nullptr, nullptr, &mic);    // count-in + 1 bar + a little
        check (e.state()[5] == 1.0, "the audio take is ready after one bar");
        std::vector<float> l, r;
        check (e.takeRecordedAudio (2, 0, l, r) && l.size() == 96000 && peakOf (l) > 0.3f, "audio take: 1 bar (2 s) of the mic");
        e.launchClip (2, 0);
        std::vector<float> ol, orr; run (e, 2.5, &ol, &orr);
        check (peakOf (ol, 48000 * 2 + 100) > 0.2f, "the take plays back in its slot");
        writeWav (dir + "/sketch_audio.wav", l, r, 48000);
        std::vector<float> rl, rr;
        check (readWav (dir + "/sketch_audio.wav", 44100, rl, rr) && std::abs ((long) rl.size() - 88200) < 4 && peakOf (rl) > 0.3f, "WAV write / read with resampling");
    }
    // scenes + offline render (the prints the desktop imports)
    {
        SketchEngine e; e.prepare (48000); e.setTempo (124);
        e.setTrack (0, TrackType::drums, 1, 1.0f, 0.0f, false, false);
        e.setTrack (1, TrackType::keys, 0, 1.0f, 0.0f, false, false);
        e.setClipNotes (0, 3, 4.0, { { 36, 0, 0.25, 120 }, { 39, 1, 0.25, 100 }, { 36, 2, 0.25, 120 }, { 39, 3, 0.25, 100 } });
        e.setClipNotes (1, 3, 8.0, { { 60, 0, 3.5, 100 }, { 63, 0, 3.5, 90 }, { 67, 0, 3.5, 90 }, { 58, 4, 3.5, 100 } });
        e.launchScene (3);
        auto st = e.state();
        check ((int) st[6] == 3 && (int) st[10] == 3, "launch scene 4: both tracks play it");
        std::vector<float> l, r;
        e.renderClipOffline (1, 3, 2, l, r);
        const size_t expect = (size_t) (8.0 * 2 * 48000 * 60.0 / 124.0);
        check (l.size() == expect && peakOf (l) > 0.05f, "offline render of a 2-bar keys clip, twice (" + std::to_string (l.size()) + " frames)");
        writeWav (dir + "/sketch_keys_print.wav", l, r, 48000);
    }
    // sample kits (the bundled vintage machines) and warmth
    {
        const std::string kits = argc > 2 ? argv[2] : "../../main/assets/kits";
        SketchEngine e; e.prepare (48000); e.setTempo (120);
        std::vector<int> notes { 36, 38, 39, 42, 46 };
        std::vector<std::string> paths;
        for (int n : notes) paths.push_back (kits + "/linndrum/" + std::to_string (n) + ".wav");
        check (e.loadSampleKit (0, notes, paths), "LinnDrum kit loads (44.1 kHz WAVs resampled to 48 kHz)");
        e.setTrack (0, TrackType::drums, 0, 1.0f, 0.0f, false, false);
        e.setTrackSampleKit (0, 0);
        e.setClipNotes (0, 0, 4.0, { { 36, 0, 0.25, 120 }, { 38, 1, 0.25, 120 }, { 42, 2, 0.25, 100 }, { 39, 3, 0.25, 110 } });
        e.launchClip (0, 0);
        std::vector<float> l, r; run (e, 2.0, &l, &r);
        bool all = true;
        for (int b = 0; b < 4; ++b) if (peakOf (l, (size_t) (b * 24000), (size_t) (b * 24000 + 2400)) < 0.1f) all = false;
        check (all, "the sample kit plays all four hits");
        writeWav (dir + "/sketch_linndrum.wav", l, r, 48000);
        // warmth darkens the top end: compare high-frequency energy of the hat
        auto hiEnergy = [] (const std::vector<float>& v, size_t a, size_t b) { double e2 = 0; for (size_t i = a + 1; i < b && i < v.size(); ++i) { const double d = v[i] - v[i - 1]; e2 += d * d; } return e2; };
        const double cleanHi = hiEnergy (l, 48000, 60000);
        e.stop(); e.setTrackWarmth (0, 1.0f); e.launchClip (0, 0);
        std::vector<float> l2, r2; run (e, 2.0, &l2, &r2);
        const double warmHi = hiEnergy (l2, 48000, 60000);
        check (warmHi < cleanHi * 0.8 && peakOf (l2) > 0.1f, "warmth softens the highs (" + std::to_string (warmHi / cleanHi) + "x)");
        writeWav (dir + "/sketch_linndrum_warm.wav", l2, r2, 48000);
        std::vector<float> ol, orr; e.renderClipOffline (0, 0, 1, ol, orr);
        check (peakOf (ol, 0, 2400) > 0.1f, "offline print uses the sample kit");
    }
    std::printf (failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
