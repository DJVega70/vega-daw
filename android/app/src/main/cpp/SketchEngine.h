#pragma once
// Vega Sketch - the phone's music engine: tracks of clip slots (a clip launcher), drum kits / synths / audio,
// live play, MIDI and audio recording into slots, a metronome, and offline rendering.
// No platform code: Android drives it from an Oboe callback, the desktop tests drive it directly.
#include "Dsp.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vega
{
constexpr int kMaxTracks = 8, kScenes = 8;
enum class TrackType { drums = 0, keys = 1, audio = 2 };

struct NoteEv { int note; double start, length; int velocity; };      // beats, relative to the clip start

struct ClipData
{
    bool used = false;
    bool audio = false;
    double lengthBeats = 4.0;
    std::vector<NoteEv> notes;
    std::shared_ptr<const std::vector<float>> audioL, audioR;           // at the engine sample rate
};

class SketchEngine
{
public:
    SketchEngine();
    void prepare (double sampleRate);
    double sampleRate() const { return sr; }

    // ---- set-up (UI thread)
    void setTempo (double bpm);
    double tempo() const { return bpm; }
    void setBeatsPerBar (int);
    int beatsPerBar() const { return bpb; }
    void setMetronome (bool on) { std::lock_guard<std::mutex> g (lock); metronome = on; }
    void setTrack (int index, TrackType, int sound, float volume, float pan, bool mute, bool solo);
    void removeTrack (int index);
    void setClipNotes (int track, int scene, double lengthBeats, const std::vector<NoteEv>&);
    void setClipAudio (int track, int scene, double lengthBeats, std::vector<float> left, std::vector<float> right);
    void clearClip (int track, int scene);
    /** Sample kits: 'slot' is any number the app chooses; notes[i] plays the WAV in paths[i] */
    bool loadSampleKit (int slot, const std::vector<int>& notes, const std::vector<std::string>& paths);
    void setTrackSampleKit (int track, int slot);            // -1 = the synthesized kit
    void setTrackWarmth (int track, float amount);
    void setQuantiseRecording (double grid) { std::lock_guard<std::mutex> g (lock); recordQuantise = grid; }

    // ---- performing
    void play();                                    // from the top of the bar
    void stop();
    bool isPlaying() const { return playing; }
    void launchClip (int track, int scene);
    void stopTrack (int track);
    void launchScene (int scene);
    void noteOn (int track, int note, int velocity);
    void noteOff (int track, int note);

    // ---- recording into a slot: MIDI (drums / keys) or audio (mic), 'bars' long, loops and overdubs until stopped
    void startRecording (int track, int scene, int bars, bool overdub);
    void stopRecording();
    bool isRecording() const { return recording.active; }
    std::vector<NoteEv> clipNotes (int track, int scene);
    bool takeRecordedAudio (int track, int scene, std::vector<float>& l, std::vector<float>& r);
    void pushInput (const float* mono, int numFrames);              // microphone samples (audio thread)

    // ---- output
    void render (float* left, float* right, int numFrames);         // audio thread

    /** [beat, playing, recording, countingIn, inputLevel, then per track: playingScene, queuedScene, progress, level] */
    std::vector<double> state();
    /** Offline: the clip on its own, 'loops' times (for the desktop "phone audio" prints) */
    void renderClipOffline (int track, int scene, int loops, std::vector<float>& l, std::vector<float>& r);

private:
    struct Track
    {
        bool exists = false; TrackType type = TrackType::drums; int sound = 0;
        float volume = 0.8f, pan = 0.0f; bool mute = false, solo = false;
        ClipData clips[kScenes];
        int playingScene = -1, queuedScene = -1; bool stopQueued = false;
        double clipStart = 0;                       // beat the playing clip started at
        DrumKit drums; PolySynth synth; Warmth warmth; int sampleKit = -1; float warmthAmount = 0;
        std::vector<std::pair<int, double>> held;   // sequencer notes sounding: note, off beat
        float level = 0;
    };
    void processChunk (float* l, float* r, int n);
    void startQueued (double atBeat);
    void sequence (Track&, double b0, double b1, int n, std::vector<std::pair<int, int>>& ons, std::vector<int>& offs, std::vector<int>& offPos, std::vector<int>& onPos);
    void instrumentOn (Track& t, int note, int vel);
    void instrumentOff (Track& t, int note);
    double nextBar (double beat) const { return std::ceil (beat / bpb - 1.0e-9) * bpb; }
    double samplesPerBeat() const { return sr * 60.0 / bpm; }

    std::mutex lock;
    double sr = 48000.0, bpm = 120.0;
    int bpb = 4;
    bool playing = false, metronome = false;
    double beat = 0.0;                              // negative during a count-in
    double recordQuantise = 0.0;
    Track tracks[kMaxTracks];
    struct Recording
    {
        bool active = false, audio = false, overdub = false; int track = -1, scene = -1; double start = 0, length = 4;
        std::vector<std::pair<int, std::pair<double, int>>> open;  // note -> (start beat absolute, velocity)
        std::vector<float> l, r; size_t written = 0; bool audioDone = false;
    } recording;
    float clickPhase = 0, clickEnv = 0, clickFreq = 1000;
    float inputLevel = 0;
    std::vector<std::shared_ptr<const SampleKit>> sampleKits;
};
}
