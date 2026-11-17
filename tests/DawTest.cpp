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
    out.deleteFile(); proj.deleteFile();
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
