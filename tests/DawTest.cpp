#include "../Source/DawSession.h"
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
    out.deleteFile(); proj.deleteFile();
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
