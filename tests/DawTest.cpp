#include "../Source/DawSession.h"
#include "../Source/HuiSurface.h"
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
    out.deleteFile(); proj.deleteFile();
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
