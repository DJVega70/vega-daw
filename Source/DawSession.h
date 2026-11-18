#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include "DJEQPlugin.h"
#include "MasterStripPlugin.h"

namespace te = tracktion;
using namespace std::literals;

/** Runs engine tasks (rendering, etc.) to completion on the calling thread. */
inline void runEngineTaskBlocking (te::ThreadPoolJobWithProgress& job)
{
    while (job.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain) {}
}
/** The edit that hardware controllers / MIDI learn talk to (set whenever the session's edit changes) */
inline te::Edit* vegaFocusedEdit = nullptr;

struct HeadlessUIBehaviour : public te::UIBehaviour
{
    te::Edit* getCurrentlyFocusedEdit() override { return vegaFocusedEdit; }
    te::Edit* getLastFocusedEdit() override { return vegaFocusedEdit; }
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& j) override { runEngineTaskBlocking (j); }
};

/** Everything the DAW does to the engine, kept free of UI so it can be tested headless. */
class DawSession
{
public:
    enum class TrackKind { audio, midi, bus };

    explicit DawSession (te::Engine&);
    ~DawSession();

    te::Engine& engine;
    te::Edit& edit() { return *editPtr; }
    std::function<void()> onEditChanged, onBeforeEditChange;            // called after new / open (the Edit object was replaced)

    // ---- project files (.vegaproj = Tracktion edit XML)
    void newProject();
    bool openProject (const juce::File&);
    bool saveProject();                               // to the current file (false if untitled)
    bool saveProjectAs (const juce::File&);
    juce::File getProjectFile() const { return projectFile; }
    bool hasUnsavedChanges() const;

    // ---- tracks
    te::AudioTrack* addTrack (TrackKind, const juce::String& name = {});
    static TrackKind kindOf (const te::Track&);
    juce::Array<te::AudioTrack*> getTracks (bool includeBuses = true) const;
    juce::Array<te::AudioTrack*> getBuses() const;
    void deleteTrack (te::Track&);
    void moveTrack (te::AudioTrack&, int newIndex);
    static juce::Colour trackColour (const te::Track&);

    // ---- routing
    void routeToBus (te::AudioTrack& source, te::AudioTrack* bus);     // nullptr = master
    te::AudioTrack* getOutputBus (te::AudioTrack&) const;
    void setSend (te::AudioTrack& source, te::AudioTrack& bus, float gainDb);   // creates send/return pair if needed
    void removeSend (te::AudioTrack& source, te::AudioTrack& bus);
    te::AuxSendPlugin* findSend (te::AudioTrack& source, te::AudioTrack& bus) const;
    int busNumberFor (te::AudioTrack& bus);

    // ---- plugins
    te::Plugin::Ptr addPlugin (te::AudioTrack&, const juce::String& type, const juce::PluginDescription* desc = nullptr, int index = -1);
    te::Plugin::Ptr setInstrument (te::AudioTrack&, const juce::String& type, const juce::PluginDescription* desc = nullptr);
    juce::Array<te::Plugin*> getInsertPlugins (te::AudioTrack&) const;
    VegaDJEQPlugin* djEq (te::AudioTrack&, bool create = true);
    VegaMasterStripPlugin* masterStrip (bool create = true);               // EQ / dynamics on the master bus            // the channel's DJ EQ / filter (always present, sits before the fader)      // user plugins, excludes volume/meter/aux
    static juce::StringArray builtInEffectTypes();
    static juce::String builtInDisplayName (const juce::String& type);

    // ---- clips
    te::WaveAudioClip::Ptr insertAudioFile (te::AudioTrack&, const juce::File&, te::TimePosition at);
    te::MidiClip::Ptr insertMidiClip (te::AudioTrack&, te::TimePosition start, te::TimeDuration length);
    te::MidiClip::Ptr importMidiFile (te::AudioTrack&, const juce::File&, te::TimePosition at);
    void splitClip (te::Clip&, te::TimePosition);
    int sliceAtTransients (te::WaveAudioClip&, float sensitivity = 0.5f);    // splits the clip at detected hits, returns number of new clips
    static void setClipFades (te::AudioClipBase&, te::TimeDuration fadeIn, te::TimeDuration fadeOut);
    static void setClipWarp (te::WaveAudioClip&, bool followTempo, float semitones);   // time-stretch + pitch (SoundTouch)
    /** Put separated stems on new audio tracks, lined up with the clip they came from (or at 'at' for a whole file).
        Returns the new tracks. The original clip (if any) is muted. */
    juce::Array<te::AudioTrack*> placeStems (const juce::Array<juce::File>& stems, const juce::StringArray& names, const juce::String& baseName,
                                             te::WaveAudioClip* original, te::TimePosition at = {});

    // ---- transport / timing
    void setTempo (double bpm);
    double getTempo() const;
    void setTimeSig (int num, int den);
    te::TimeDuration barLength() const;
    te::TimePosition snap (te::TimePosition, int gridDivisionsPerBeat) const;

    // ---- export
    bool exportWav (const juce::File&, std::optional<te::TimeRange> range = std::nullopt);
    te::TimeRange contentRange() const;

private:
    void attachEdit (std::unique_ptr<te::Edit>);
    std::unique_ptr<te::Edit> editPtr;
    juce::File projectFile;
};
