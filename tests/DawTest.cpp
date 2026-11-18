#include "../Source/DawSession.h"
#include "../Source/HuiSurface.h"
#include "../Source/StemSeparator.h"
#include "../Source/DawUI.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }

static float peakOf (const juce::File& f, te::Engine& e)
{
    std::unique_ptr<juce::AudioFormatReader> r (e.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
    if (r == nullptr) return -1;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    return b.getMagnitude (0, b.getNumSamples());
}


// level (dB) of a sine at frequency f in the middle of a rendered file
static float toneDb (const juce::File& f, te::Engine& e, double hz)
{
    std::unique_ptr<juce::AudioFormatReader> r (e.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
    if (r == nullptr) return -999;
    const int n = 48000, start = (int) (r->sampleRate * 1.0);
    juce::AudioBuffer<float> b (1, n);
    r->read (&b, 0, n, start, true, false);
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / n), ph = 2 * juce::MathConstants<double>::pi * hz * i / r->sampleRate;
        re += b.getSample (0, i) * w * std::cos (ph); im += b.getSample (0, i) * w * std::sin (ph);
    }
    return (float) juce::Decibels::gainToDecibels (std::sqrt (re * re + im * im) * 4.0 / n, -200.0);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine ("VegaDAWTest", std::make_unique<HeadlessUIBehaviour>(), nullptr);
    DawSession s (engine);
    {
        auto tracks = s.getTracks();
        check (tracks.size() == 2 && DawSession::kindOf (*tracks[0]) == DawSession::TrackKind::audio && DawSession::kindOf (*tracks[1]) == DawSession::TrackKind::midi,
               "new project: an audio track and a synth track");
        check (tracks[1]->pluginList[0] != nullptr && tracks[1]->pluginList[0]->isSynth(), "synth track has the built-in instrument");
    }
    s.setTempo (120.0);
    auto* synth = s.getTracks()[1];
    auto* bus = s.addTrack (DawSession::TrackKind::bus, "Reverb Bus");
    auto* drums = s.addTrack (DawSession::TrackKind::bus, "Drum Bus");
    check (bus != nullptr && s.getBuses().size() == 2 && s.busNumberFor (*bus) != s.busNumberFor (*drums), "buses get unique numbers");
    // routing
    auto* audio = s.getTracks()[0];
    s.routeToBus (*audio, drums);
    check (s.getOutputBus (*audio) == drums, "track output routed to a bus");
    s.setSend (*synth, *bus, -6.0f);
    check (s.findSend (*synth, *bus) != nullptr && std::abs (s.findSend (*synth, *bus)->getGainDb() + 6.0f) < 0.1f, "aux send at -6 dB");
    // plugins
    auto rev = s.addPlugin (*bus, te::ReverbPlugin::xmlTypeName);
    check (rev != nullptr && s.getInsertPlugins (*bus).size() == 1, "insert a reverb on the bus (send/return hidden from the insert list)");
    // MIDI clip with a chord
    auto clip = s.insertMidiClip (*synth, te::TimePosition(), s.barLength());
    check (clip != nullptr, "MIDI clip created");
    for (int n : { 60, 64, 67 }) clip->getSequence().addNote (n, te::BeatPosition::fromBeats (0), te::BeatDuration::fromBeats (2), 100, 0, nullptr);
    check (clip->getSequence().getNumNotes() == 3, "notes added");
    // split
    s.splitClip (*clip, te::TimePosition::fromSeconds (1.0));
    check (synth->getClips().size() == 2, "split a clip into two");
    // render
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_daw_test.wav");
    const bool ok = s.exportWav (out);
    const float pk = peakOf (out, engine);
    check (ok && pk > 0.01f && pk < 4.0f, "export WAV renders the synth (peak " + juce::String (pk, 3) + ")");
    // save / load round trip
    auto proj = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_daw_test.vegaproj");
    check (s.saveProjectAs (proj), "save project");
    DawSession s2 (engine);
    check (s2.openProject (proj), "open project");
    {
        auto t = s2.getTracks();
        te::AudioTrack* bus2 = nullptr; te::AudioTrack* drums2 = nullptr; te::AudioTrack* synth2 = nullptr; te::AudioTrack* audio2 = nullptr;
        for (auto* x : t) { if (x->getName() == "Reverb Bus") bus2 = x; if (x->getName() == "Drum Bus") drums2 = x; if (x->getName() == "Synth 1") synth2 = x; if (x->getName() == "Audio 1") audio2 = x; }
        check (t.size() == 4 && bus2 && drums2 && synth2 && audio2, "all 4 tracks restored");
        if (bus2 && synth2 && audio2 && drums2)
        {
            check (DawSession::kindOf (*bus2) == DawSession::TrackKind::bus && s2.findSend (*synth2, *bus2) != nullptr, "bus kind and send restored");
            check (s2.getOutputBus (*audio2) == drums2, "output routing restored");
            int notes = 0; for (auto* c : synth2->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) notes += mc->getSequence().getNumNotes();
            check (notes >= 3, "MIDI notes restored (" + juce::String (notes) + ")");
        }
    }
    // audio import + fades + warp
    {
        auto* t = s.getTracks()[0];
        auto clip2 = s.insertAudioFile (*t, out, te::TimePosition::fromSeconds (4));
        check (clip2 != nullptr && std::abs (clip2->getPosition().getStart().inSeconds() - 4.0) < 1e-6, "audio file imported at 4 s");
        if (clip2) { DawSession::setClipFades (*clip2, 0.05s, 0.5s); DawSession::setClipWarp (*clip2, true, 3.0f); check (clip2->getPitchChange() == 3.0f && clip2->getAutoTempo(), "clip warp + pitch set"); }
    }
    // ---- HUI control surface (Novation FLkey 2 DAW port)
    {
        DawSession h (engine);
        for (int i = 0; i < 10; ++i) h.addTrack (DawSession::TrackKind::audio);     // 12 tracks total
        h.addTrack (DawSession::TrackKind::bus, "FX");
        HuiSurface hui (h);
        std::vector<juce::MidiMessage> sent; hui.send = [&] (const juce::MidiMessage& m) { sent.push_back (m); };
        te::AudioTrack* selected = nullptr; hui.onSelectTrack = [&] (te::AudioTrack* t) { selected = t; };
        auto cc = [&] (int n, int v) { hui.handleIncoming (juce::MidiMessage::controllerEvent (1, n, v)); };
        auto press = [&] (int zone, int port) { cc (0x0f, zone); cc (0x2f, 0x40 | port); cc (0x0f, zone); cc (0x2f, port); };
        auto tracks = h.getTracks (true);
        // fader 3 to the 0 dB position
        const int pos = juce::roundToInt (HuiSurface::faderPosFromDb (0.0f) * 16383);
        cc (0x02, pos >> 7); cc (0x22, pos & 0x7f);
        check (std::abs (tracks[2]->getVolumePlugin()->getVolumeDb()) < 0.3f, "HUI fader 3 sets track 3 volume (" + juce::String (tracks[2]->getVolumePlugin()->getVolumeDb(), 2) + " dB)");
        cc (0x02, 0); cc (0x22, 0);
        check (tracks[2]->getVolumePlugin()->getVolumeDb() < -60.0f, "HUI fader down = silent");
        press (1, 2);  check (tracks[1]->isMuted (false), "HUI pad: mute strip 2");
        press (1, 3);  check (tracks[1]->isSolo (false), "HUI pad: solo strip 2");
        press (4, 1);  check (selected == tracks[4], "HUI fader button selects track 5");
        cc (0x40, 0x45); check (tracks[0]->getVolumePlugin()->getPan() > 0.05f, "HUI encoder 1 pans right (" + juce::String (tracks[0]->getVolumePlugin()->getPan(), 2) + ")");
        press (0x0a, 3); check (hui.bankStart == 8 && hui.trackForStrip (0) == tracks[8], "HUI bank right shows tracks 9-16");
        cc (0x00, pos >> 7); cc (0x20, pos & 0x7f);
        check (std::abs (tracks[8]->getVolumePlugin()->getVolumeDb()) < 0.3f, "after banking, fader 1 controls track 9");
        press (0x0a, 1); check (hui.bankStart == 0, "HUI bank left back to tracks 1-8");
        press (0x0b, 7); auto* bus = h.getBuses()[0];
        for (int k = 0; k < 10; ++k) cc (0x40, 0x48);
        check (h.findSend (*tracks[0], *bus) != nullptr && h.findSend (*tracks[0], *bus)->getGainDb() > -30.0f, "Send A mode: encoder creates and raises a send to the first bus");
        press (0x0b, 2);
        press (0x0f, 3); check (h.edit().getTransport().looping.get(), "HUI loop button toggles loop");
        press (0x0e, 2); check (h.edit().getTransport().getPosition().inSeconds() >= h.barLength().inSeconds() - 0.01, "HUI fast-forward jumps a bar");
        press (0x0f, 0); check (h.edit().getTransport().getPosition().inSeconds() < 0.01, "HUI return-to-zero");
        // feedback
        sent.clear(); hui.updateFeedback (true);
        bool faderFb = false, muteLed = false, nameSx = false;
        for (size_t i = 0; i + 1 < sent.size(); ++i)
        {
            auto& m = sent[i];
            if (m.isController() && m.getControllerNumber() == 0x0c && m.getControllerValue() == 1 && sent[i + 1].isController() && sent[i + 1].getControllerValue() == (0x40 | 2)) muteLed = true;
            if (m.isController() && m.getControllerNumber() == 0x01 && sent[i + 1].isController() && sent[i + 1].getControllerNumber() == 0x21) faderFb = true;
            if (m.isSysEx() && m.getSysExDataSize() == 11 && m.getSysExData()[5] == 0x10) nameSx = true;
        }
        check (faderFb && muteLed && nameSx, "HUI feedback: motor-fader positions, mute LED and track names are sent");
        sent.clear(); hui.updateFeedback();
        check (sent.empty(), "HUI feedback only sends changes (" + juce::String ((int) sent.size()) + " msgs when idle)");
    }
    {   // DJ EQ / filter on every channel
        DawSession d (engine);
        auto tone = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_dj_tones.wav");
        {
            tone.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tone), 48000.0, 2, 24, {}, 0));
            juce::AudioBuffer<float> b (2, 48000 * 3);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / 48000.0;
                const float v = (float) (0.2 * std::sin (2 * juce::MathConstants<double>::pi * 80 * t) + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 1000 * t) + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 8000 * t));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        auto* t = d.getTracks()[0];
        d.insertAudioFile (*t, tone, te::TimePosition());
        auto* eq = d.djEq (*t, false);
        check (eq != nullptr && d.getInsertPlugins (*t).isEmpty(), "every channel has a DJ EQ, hidden from the insert list");
        auto render = [&] (float hi, float mid, float low, float filt, const char* name)
        {
            eq->band[0]->setParameter (hi, juce::sendNotificationSync); eq->band[1]->setParameter (mid, juce::sendNotificationSync);
            eq->band[2]->setParameter (low, juce::sendNotificationSync); eq->filter->setParameter (filt, juce::sendNotificationSync);
            auto o = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("vega_dj_") + name + ".wav");
            d.exportWav (o, te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (2.5)));
            std::array<float, 3> r { toneDb (o, engine, 80), toneDb (o, engine, 1000), toneDb (o, engine, 8000) };
            o.deleteFile();
            return r;
        };
        const auto flat = render (0, 0, 0, 0, "flat");
        check (std::abs (flat[0] - flat[1]) < 1.0f && std::abs (flat[2] - flat[1]) < 1.0f && flat[1] > -20.0f,
               "DJ EQ flat at 0 dB (80 Hz " + juce::String (flat[0], 1) + ", 1 kHz " + juce::String (flat[1], 1) + ", 8 kHz " + juce::String (flat[2], 1) + " dB)");
        auto lowKill = render (0, 0, VegaDJEQPlugin::killDb, 0, "lowkill");
        check (lowKill[0] < flat[0] - 30 && std::abs (lowKill[1] - flat[1]) < 1.5f && std::abs (lowKill[2] - flat[2]) < 1.0f, "LOW kill removes the bass (" + juce::String (lowKill[0] - flat[0], 1) + " dB), mids/highs stay");
        auto midKill = render (0, VegaDJEQPlugin::killDb, 0, 0, "midkill");
        check (midKill[1] < flat[1] - 25 && std::abs (midKill[0] - flat[0]) < 1.5f && std::abs (midKill[2] - flat[2]) < 1.5f, "MID kill (" + juce::String (midKill[1] - flat[1], 1) + " dB)");
        auto hiKill = render (VegaDJEQPlugin::killDb, 0, 0, 0, "hikill");
        check (hiKill[2] < flat[2] - 30 && std::abs (hiKill[0] - flat[0]) < 1.0f, "HI kill removes the highs (" + juce::String (hiKill[2] - flat[2], 1) + " dB)");
        auto boost = render (6, 0, 0, 0, "boost");
        check (std::abs (boost[2] - flat[2] - 6.0f) < 1.0f, "HI +6 dB boost (" + juce::String (boost[2] - flat[2], 1) + " dB)");
        auto lpf = render (0, 0, 0, -0.6f, "lpf");
        check (lpf[2] < flat[2] - 20 && std::abs (lpf[0] - flat[0]) < 1.5f, "filter left = low-pass: 8 kHz " + juce::String (lpf[2] - flat[2], 1) + " dB, bass kept");
        auto hpf = render (0, 0, 0, 0.6f, "hpf");
        check (hpf[0] < flat[0] - 20 && std::abs (hpf[2] - flat[2]) < 1.5f, "filter right = high-pass: 80 Hz " + juce::String (hpf[0] - flat[0], 1) + " dB, highs kept");
        // settings are saved with the project
        eq->band[2]->setParameter (-12.0f, juce::sendNotificationSync); eq->filter->setParameter (0.4f, juce::sendNotificationSync);
        auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_dj.vegaproj");
        d.saveProjectAs (pf);
        DawSession d2 (engine); d2.openProject (pf);
        auto* eq2 = d2.djEq (*d2.getTracks()[0], false);
        check (eq2 != nullptr && std::abs (eq2->band[2]->getCurrentValue() + 12.0f) < 0.1f && std::abs (eq2->filter->getCurrentValue() - 0.4f) < 0.01f, "DJ EQ and filter settings restored from the project");
        check (d2.getInsertPlugins (*d2.getTracks()[0]).isEmpty(), "reopened project doesn't get a second DJ EQ");
        int count = 0; for (auto* p : d2.getTracks()[0]->pluginList.getPlugins()) if (dynamic_cast<VegaDJEQPlugin*> (p)) ++count;
        check (count == 1, "exactly one DJ EQ per channel after reopening");
        pf.deleteFile(); tone.deleteFile();
    }
    {   // master strip: EQ, filters, compressor, gate on the master bus
        DawSession m (engine);
        auto* ms = m.masterStrip (false);
        check (ms != nullptr, "master bus has the master strip");
        auto tone = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_ms_tones.wav");
        {
            tone.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tone), 48000.0, 2, 24, {}, 0));
            juce::AudioBuffer<float> b (2, 48000 * 3);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / 48000.0;
                const float v = (float) (0.2 * std::sin (2 * juce::MathConstants<double>::pi * 50 * t) + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 1000 * t) + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 12000 * t));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        m.insertAudioFile (*m.getTracks()[0], tone, te::TimePosition());
        auto render = [&] (std::function<void()> setup, const char* name)
        {
            for (int i = 0; i < VegaMasterStripPlugin::numParams; ++i) ms->params[i]->setParameter (VegaMasterStripPlugin::def (i).def, juce::sendNotificationSync);
            setup();
            auto o = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("vega_ms_") + name + ".wav");
            m.exportWav (o, te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (2.5)));
            std::array<float, 3> r { toneDb (o, engine, 50), toneDb (o, engine, 1000), toneDb (o, engine, 12000) };
            o.deleteFile();
            return r;
        };
        using M = VegaMasterStripPlugin;
        auto setp = [&] (int p, float v) { ms->params[p]->setParameter (v, juce::sendNotificationSync); };
        const auto flat = render ([] {}, "flat");
        auto hp = render ([&] { setp (M::hpf, 200); }, "hp");
        check (hp[0] < flat[0] - 20 && std::abs (hp[1] - flat[1]) < 1.0f, "master HP filter at 200 Hz cuts 50 Hz by " + juce::String (flat[0] - hp[0], 1) + " dB");
        auto lp = render ([&] { setp (M::lpf, 3000); }, "lp");
        check (lp[2] < flat[2] - 15 && std::abs (lp[1] - flat[1]) < 1.0f, "master LP filter at 3 kHz cuts 12 kHz by " + juce::String (flat[2] - lp[2], 1) + " dB");
        auto hmf = render ([&] { setp (M::hmfGain, 10); setp (M::hmfFreq, 1000); }, "hmf");
        check (std::abs (hmf[1] - flat[1] - 10.0f) < 1.0f && std::abs (hmf[0] - flat[0]) < 1.0f, "HMF bell +10 dB at 1 kHz (" + juce::String (hmf[1] - flat[1], 1) + " dB)");
        auto lfShelf = render ([&] { setp (M::lfGain, -12); setp (M::lfFreq, 150); }, "lf");
        check (lfShelf[0] < flat[0] - 9 && std::abs (lfShelf[2] - flat[2]) < 0.5f, "LF shelf -12 dB (50 Hz " + juce::String (lfShelf[0] - flat[0], 1) + " dB)");
        auto eqOff = render ([&] { setp (M::hmfGain, 10); setp (M::eqOn, 0); }, "eqoff");
        check (std::abs (eqOff[1] - flat[1]) < 0.3f, "EQ In switch bypasses the EQ");
        auto comp = render ([&] { setp (M::compRatio, 20); setp (M::compThresh, -24); }, "comp");
        float grSeen = ms->grComp.exchange (0.0f);
        check (grSeen > 3.0f, "compressor reduces gain (" + juce::String (grSeen, 1) + " dB GR at -24 dB / 20:1)");
        check (comp[1] < flat[1] + 6.0f, "compressor with auto make-up stays controlled");
        auto trim = render ([&] { setp (M::outGain, -6); }, "trim");
        check (std::abs (trim[1] - flat[1] + 6.0f) < 0.2f, "output trim -6 dB");
        auto cutR = render ([&] { setp (M::cut, 1); }, "cut");
        check (cutR[1] < -100.0f, "CUT silences the master");
        auto gate = render ([&] { setp (M::gateRange, 40); setp (M::gateThresh, -3); }, "gate");
        check (gate[1] < flat[1] - 30, "gate closes below threshold (" + juce::String (gate[1] - flat[1], 1) + " dB)");
        // saved with the project
        setp (M::hfGain, 4.5f);
        auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_ms.vegaproj");
        m.saveProjectAs (pf);
        DawSession m2 (engine); m2.openProject (pf);
        auto* ms2 = m2.masterStrip (false);
        int count = 0; for (auto* p : m2.edit().getMasterPluginList().getPlugins()) if (dynamic_cast<VegaMasterStripPlugin*> (p)) ++count;
        check (ms2 != nullptr && count == 1 && std::abs (ms2->get (M::hfGain) - 4.5f) < 0.01f, "master strip settings restored, one instance");
        pf.deleteFile(); tone.deleteFile();
    }
    {   // stem placement: stems line up with the original clip, original gets muted
        DawSession p (engine);
        auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_stems_test");
        tmpDir.deleteRecursively(); tmpDir.createDirectory();
        juce::Array<juce::File> files;
        for (auto name : { "Drums", "Bass", "Other", "Vocals" })
        {
            auto f = tmpDir.getChildFile (juce::String ("song - ") + name + ".wav");
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100.0, 2, 24, {}, 0));
            juce::AudioBuffer<float> b (2, 44100 * 4); b.clear(); b.setSample (0, 100, 0.5f);
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            files.add (f);
        }
        auto* src = p.getTracks()[0];
        auto orig = p.insertAudioFile (*src, files[0], te::TimePosition::fromSeconds (2.0));
        const int before = p.getTracks().size();
        auto made = p.placeStems (files, StemSeparator::stemNames (StemSeparator::Model::fourStems), "song", orig.get());
        bool aligned = made.size() == 4;
        for (auto* t : made) if (t->getClips().size() != 1 || std::abs (t->getClips()[0]->getPosition().getStart().inSeconds() - 2.0) > 1e-6) aligned = false;
        check (aligned && p.getTracks().size() == before + 4 && orig->isMuted(), "separated stems land on 4 new tracks lined up with the clip; original muted");
        check (made.size() == 4 && made[3]->getName() == "song Vocals", "stem tracks are named after the clip (" + (made.size() == 4 ? made[3]->getName() : juce::String()) + ")");
        tmpDir.deleteRecursively();
    }
    {   // real AI separation (runs when the Demucs model is available, e.g. in CI)
        const juce::String modelPath = juce::SystemStats::getEnvironmentVariable ("VEGA_DEMUCS_MODEL", {});
        if (modelPath.isEmpty() || ! juce::File (modelPath).existsAsFile())
            std::cout << "SKIP Demucs separation (set VEGA_DEMUCS_MODEL to a ggml-model-htdemucs-4s-f16.bin to run it)\n";
        else
        {
            const int n = 44100 * 16;
            juce::AudioBuffer<float> mix (2, n); mix.clear();
            juce::Random rnd (3);
            const double tau = 2 * juce::MathConstants<double>::pi;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / 44100.0, beat = std::fmod (t, 0.5), half = std::fmod (t, 0.25);
                const double bassF = std::fmod (t, 2.0) < 1.0 ? 55.0 : 73.4;
                double v = 0.28 * std::sin (tau * bassF * t) * (0.6 + 0.4 * std::exp (-beat * 4));                     // bass line
                v += 0.6 * std::sin (tau * (50 + 90 * std::exp (-beat * 30)) * beat) * std::exp (-beat * 14);          // kick
                v += 0.12 * (rnd.nextFloat() * 2 - 1) * std::exp (-half * 120);                                       // hats
                const double f0 = 220 * std::pow (2.0, (std::floor (t) == 3 || std::floor (t) == 9 ? 3 : 0) / 12.0) * (1 + 0.01 * std::sin (tau * 5.5 * t));
                double voice = 0; for (int k = 1; k <= 12; ++k) voice += std::sin (tau * f0 * k * t) / k * (k == 3 || k == 4 ? 2.0 : 1.0);
                v += 0.08 * voice * (std::fmod (t, 4.0) < 3.0 ? 1.0 : 0.0);                                             // sung line
                mix.setSample (0, i, (float) v); mix.setSample (1, i, (float) v);
            }
            std::vector<juce::AudioBuffer<float>> stems; juce::String err;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            float lastP = 0;
            const bool ok = StemSeparator::separateBuffer (juce::File (modelPath), mix, stems, [&] (float pr, const juce::String&) { lastP = pr; return true; }, err, 2);
            const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            check (ok && stems.size() == 4, "Demucs separated 16 s into 4 stems in " + juce::String (secs, 1) + " s" + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
            if (ok && stems.size() == 4)
            {
                double e = 0, r = 0;
                for (int i = 0; i < n; ++i) { float sum = 0; for (auto& st : stems) sum += st.getSample (0, i); e += (sum - mix.getSample (0, i)) * (sum - mix.getSample (0, i)); r += mix.getSample (0, i) * mix.getSample (0, i); }
                const double recon = 10 * std::log10 (e / r + 1e-12);
                check (recon < -10, "stems add back up to the mix (error " + juce::String (recon, 1) + " dB)");
                auto bandEnergy = [&] (const juce::AudioBuffer<float>& b, double lo, double hi)
                {
                    juce::dsp::FFT fft (13); const int N = 1 << 13; double sum = 0;
                    for (int off = 0; off + N <= n; off += N)
                    {
                        std::vector<float> d (2 * N, 0.0f);
                        for (int i = 0; i < N; ++i) d[(size_t) i] = b.getSample (0, off + i) * (float) (0.5 - 0.5 * std::cos (tau * i / N));
                        fft.performFrequencyOnlyForwardTransform (d.data());
                        for (int k = (int) (lo * N / 44100); k <= (int) (hi * N / 44100); ++k) sum += (double) d[(size_t) k] * d[(size_t) k];
                    }
                    return sum;
                };
                // stems: 0 drums, 1 bass, 2 other, 3 vocals
                double bassLow[4], hiBand[4], bassTot = 0, hiTot = 0;
                for (int s2 = 0; s2 < 4; ++s2) { bassLow[s2] = bandEnergy (stems[(size_t) s2], 52, 78); hiBand[s2] = bandEnergy (stems[(size_t) s2], 7000, 16000); bassTot += bassLow[s2]; hiTot += hiBand[s2]; }
                check (bassLow[1] / bassTot > 0.4, "bass line goes to the Bass stem (" + juce::String (100 * bassLow[1] / bassTot, 0) + "% of 52-78 Hz)");
                check (hiBand[0] / hiTot > 0.4, "hi-hats go to the Drums stem (" + juce::String (100 * hiBand[0] / hiTot, 0) + "% of 7-16 kHz)");
            }
        }
    }
    {   // without the model: clean error
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        auto r = StemSeparator::separate (StemSeparator::Model::sixStems, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nope.wav"),
                                          juce::File::getSpecialLocation (juce::File::tempDirectory), fm, nullptr);
        check (! r.ok && r.error.isNotEmpty(), "separation reports a clear error when it can't run (" + r.error + ")");
    }
    {   // automation: a volume ramp written into the curve plays back in the render, and is saved
        DawSession a (engine);
        auto tone = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_auto_tone.wav");
        {
            tone.deleteFile(); juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tone), 48000.0, 2, 24, {}, 0));
            juce::AudioBuffer<float> b (2, 48000 * 4);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        auto* t = a.getTracks()[0];
        a.insertAudioFile (*t, tone, te::TimePosition());
        auto vp = t->getVolumePlugin();
        auto& curve = vp->volParam->getCurve();
        curve.addPoint (te::TimePosition::fromSeconds (0.5), te::decibelsToVolumeFaderPosition (0.0f), 0.0f, nullptr);
        curve.addPoint (te::TimePosition::fromSeconds (2.5), te::decibelsToVolumeFaderPosition (-24.0f), 0.0f, nullptr);
        check (dawui::laneParam (*t) == vp->volParam.get() && dawui::automatableParams (*t).size() >= 6, "automation lane lists the track's parameters (" + juce::String (dawui::automatableParams (*t).size()) + "), volume by default");
        auto o = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_auto.wav");
        a.exportWav (o, te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (3.5)));
        std::unique_ptr<juce::AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (o));
        float early = 0, late = 0;
        if (r != nullptr)
        {
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples); r->read (&b, 0, b.getNumSamples(), 0, true, true);
            early = b.getRMSLevel (0, (int) (0.1 * r->sampleRate), (int) (0.3 * r->sampleRate));
            late = b.getRMSLevel (0, (int) (2.8 * r->sampleRate), (int) (0.4 * r->sampleRate));
        }
        const float drop = juce::Decibels::gainToDecibels (late) - juce::Decibels::gainToDecibels (early);
        check (std::abs (drop + 24.0f) < 2.0f, "volume automation ramp plays back (" + juce::String (drop, 1) + " dB from start to end)");
        auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_auto.vegaproj");
        a.saveProjectAs (pf);
        DawSession a2 (engine); a2.openProject (pf);
        check (a2.getTracks()[0]->getVolumePlugin()->volParam->getCurve().getNumPoints() == 2, "automation points saved with the project");
        o.deleteFile(); pf.deleteFile(); tone.deleteFile();
    }
    {   // freeze: the synth track renders to a freeze file and still plays back
        DawSession fz (engine);
        auto* synth = fz.getTracks()[1];
        auto clip = fz.insertMidiClip (*synth, te::TimePosition(), te::TimeDuration::fromSeconds (2));
        if (clip) clip->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (2), 100, 0, nullptr);
        auto res = synth->setFrozen (true, te::Track::individualFreeze);
        check (res.wasOk() && synth->isFrozen (te::Track::individualFreeze), "freeze renders the synth track to audio" + (res.failed() ? " (" + res.getErrorMessage() + ")" : juce::String()));
        auto o = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_freeze.wav");
        fz.exportWav (o, te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (2)));
        const float pk = peakOf (o, engine);
        check (pk > 0.01f, "frozen track still plays (peak " + juce::String (pk, 3) + ")");
        check (synth->setFrozen (false, te::Track::individualFreeze).wasOk() && ! synth->isFrozen (te::Track::individualFreeze), "unfreeze");
        o.deleteFile();
    }
    {   // MIDI learn mapping: a CC moves the mapped parameter and the mapping is saved
        DawSession ml (engine);
        auto* t = ml.getTracks()[0];
        auto* dj = ml.djEq (*t, false);
        auto& pcm = ml.edit().getParameterControlMappings();
        // the real learn flow: learn on -> touch a control on screen -> move a CC
        engine.getMidiLearnState().setActive (true);
        dj->filter->setParameter (0.2f, juce::sendNotificationSync);
        pcm.sendChange (21, 0.5f, 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        engine.getMidiLearnState().setActive (false);
        int ch = -1, cc = -1;
        check (pcm.getParameterMapping (*dj->filter, ch, cc) && cc == 21, "MIDI learn maps CC 21 to the touched control (cc " + juce::String (cc) + ")");
        pcm.sendChange (21, 1.0f, 1);
        check (dj->filter->getCurrentValue() > 0.9f, "MIDI-learned CC 21 moves the DJ filter (" + juce::String (dj->filter->getCurrentValue(), 2) + ")");
        pcm.sendChange (21, 0.0f, 1);
        check (dj->filter->getCurrentValue() < -0.9f, "and back down");
        check (vegaFocusedEdit == &ml.edit(), "controllers follow the open project");
        auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vega_ml.vegaproj");
        pcm.saveToEdit(); ml.saveProjectAs (pf);
        DawSession ml2 (engine); ml2.openProject (pf);
        auto& pcm2 = ml2.edit().getParameterControlMappings();
        pcm2.loadFromEdit();
        auto* dj2 = ml2.djEq (*ml2.getTracks()[0], false);
        pcm2.sendChange (21, 1.0f, 1);
        check (pcm2.getNumControllerIDs() >= 1 && dj2->filter->getCurrentValue() > 0.9f, "MIDI mappings saved with the project");
        pf.deleteFile();
    }
    out.deleteFile(); proj.deleteFile();
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
