#include "SketchEngine.h"
#include <cstring>

namespace vega
{
SketchEngine::SketchEngine() { prepare (48000.0); }

void SketchEngine::prepare (double sampleRate)
{
    std::lock_guard<std::mutex> g (lock);
    sr = sampleRate;
    for (auto& t : tracks) { t.drums.prepare ((float) sr); t.synth.prepare ((float) sr); }
}

void SketchEngine::setTempo (double b) { std::lock_guard<std::mutex> g (lock); bpm = std::clamp (b, 40.0, 220.0); }
void SketchEngine::setBeatsPerBar (int b) { std::lock_guard<std::mutex> g (lock); bpb = std::clamp (b, 2, 12); }

void SketchEngine::setTrack (int i, TrackType type, int sound, float volume, float pan, bool mute, bool solo)
{
    if (i < 0 || i >= kMaxTracks) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[i];
    if (t.exists && t.type != type) { t.drums.allOff(); t.synth.allOff(); }
    t.exists = true; t.type = type; t.sound = sound; t.volume = volume; t.pan = pan; t.mute = mute; t.solo = solo;
    if (type == TrackType::drums) t.drums.setKit (sound); else if (type == TrackType::keys) t.synth.setPreset (sound);
}

void SketchEngine::removeTrack (int i)
{
    if (i < 0 || i >= kMaxTracks) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[i];
    t.exists = false; t.playingScene = t.queuedScene = -1; t.stopQueued = false; t.held.clear();
    for (auto& c : t.clips) c = ClipData();
    t.drums.allOff(); t.synth.allOff();
    if (recording.active && recording.track == i) recording.active = false;
}

void SketchEngine::setClipNotes (int ti, int s, double lengthBeats, const std::vector<NoteEv>& notes)
{
    if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return;
    std::lock_guard<std::mutex> g (lock);
    auto& c = tracks[ti].clips[s];
    c.used = true; c.audio = false; c.lengthBeats = std::max (0.25, lengthBeats); c.notes = notes; c.audioL.reset(); c.audioR.reset();
}

void SketchEngine::setClipAudio (int ti, int s, double lengthBeats, std::vector<float> left, std::vector<float> right)
{
    if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return;
    auto l = std::make_shared<const std::vector<float>> (std::move (left));
    auto r = std::make_shared<const std::vector<float>> (right.empty() ? *l : std::move (right));
    std::lock_guard<std::mutex> g (lock);
    auto& c = tracks[ti].clips[s];
    c.used = true; c.audio = true; c.lengthBeats = std::max (0.25, lengthBeats); c.notes.clear(); c.audioL = l; c.audioR = r;
}

void SketchEngine::clearClip (int ti, int s)
{
    if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[ti];
    t.clips[s] = ClipData();
    if (t.playingScene == s) { t.playingScene = -1; for (auto& h : t.held) instrumentOff (t, h.first); t.held.clear(); }
    if (t.queuedScene == s) t.queuedScene = -1;
}

//============================================================================== performing
void SketchEngine::play()
{
    std::lock_guard<std::mutex> g (lock);
    if (playing) return;
    playing = true; beat = 0.0;
    startQueued (0.0);
}

void SketchEngine::stop()
{
    std::lock_guard<std::mutex> g (lock);
    playing = false; beat = 0.0;
    for (auto& t : tracks)
    {
        t.playingScene = -1; t.queuedScene = -1; t.stopQueued = false;
        for (auto& h : t.held) instrumentOff (t, h.first);
        t.held.clear();
    }
    if (recording.active && ! recording.audio) recording.active = false;
    if (recording.active && recording.audio) recording.audioDone = true;
}

void SketchEngine::launchClip (int ti, int s)
{
    if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[ti];
    if (! t.clips[s].used) { t.queuedScene = -1; t.stopQueued = t.playingScene >= 0; return; }
    t.queuedScene = s; t.stopQueued = false;
    if (! playing) { playing = true; beat = 0.0; startQueued (0.0); }
}

void SketchEngine::stopTrack (int ti)
{
    if (ti < 0 || ti >= kMaxTracks) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[ti];
    t.queuedScene = -1;
    if (! playing) { t.playingScene = -1; return; }
    t.stopQueued = t.playingScene >= 0;
}

void SketchEngine::launchScene (int s)
{
    if (s < 0 || s >= kScenes) return;
    std::lock_guard<std::mutex> g (lock);
    for (auto& t : tracks)
    {
        if (! t.exists) continue;
        if (t.clips[s].used) { t.queuedScene = s; t.stopQueued = false; }
        else { t.queuedScene = -1; t.stopQueued = t.playingScene >= 0; }
    }
    if (! playing) { playing = true; beat = 0.0; startQueued (0.0); }
}

void SketchEngine::startQueued (double at)
{
    for (auto& t : tracks)
    {
        if (t.stopQueued) { t.playingScene = -1; t.stopQueued = false; for (auto& h : t.held) instrumentOff (t, h.first); t.held.clear(); }
        if (t.queuedScene >= 0)
        {
            for (auto& h : t.held) instrumentOff (t, h.first);
            t.held.clear();
            t.playingScene = t.queuedScene; t.queuedScene = -1; t.clipStart = at;
        }
    }
}

void SketchEngine::instrumentOn (Track& t, int note, int vel)
{
    if (t.type == TrackType::drums) t.drums.trigger (note, vel);
    else if (t.type == TrackType::keys) t.synth.noteOn (note, vel);
}
void SketchEngine::instrumentOff (Track& t, int note)
{
    if (t.type == TrackType::keys) t.synth.noteOff (note);
}

void SketchEngine::noteOn (int ti, int note, int vel)
{
    if (ti < 0 || ti >= kMaxTracks) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[ti];
    instrumentOn (t, note, vel);
    if (recording.active && ! recording.audio && recording.track == ti && playing)
    {
        // a hit just before the downbeat counts as on the downbeat
        double at = beat;
        if (at < recording.start && at > recording.start - 0.125) at = recording.start;
        if (at >= recording.start) recording.open.push_back ({ note, { at, vel } });
    }
}

void SketchEngine::noteOff (int ti, int note)
{
    if (ti < 0 || ti >= kMaxTracks) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[ti];
    instrumentOff (t, note);
    if (! (recording.active && ! recording.audio && recording.track == ti)) return;
    for (size_t i = 0; i < recording.open.size(); ++i)
    {
        if (recording.open[i].first != note) continue;
        const double startAbs = recording.open[i].second.first;
        const int vel = recording.open[i].second.second;
        recording.open.erase (recording.open.begin() + (long) i);
        auto& c = t.clips[recording.scene];
        const double len = c.lengthBeats;
        double rel = std::fmod (startAbs - recording.start, len);
        if (rel < 0) rel += len;
        if (recordQuantise > 0) { rel = std::round (rel / recordQuantise) * recordQuantise; if (rel >= len - 1.0e-6) rel = 0; }
        double noteLen = std::max (t.type == TrackType::drums ? 0.25 : 0.05, beat - startAbs);
        if (t.type == TrackType::drums) noteLen = 0.25;
        c.notes.push_back ({ note, rel, std::min (noteLen, len), vel });
        return;
    }
}

//============================================================================== recording
void SketchEngine::startRecording (int ti, int s, int bars, bool overdub)
{
    if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return;
    std::lock_guard<std::mutex> g (lock);
    auto& t = tracks[ti];
    if (! t.exists) return;
    recording = Recording();
    recording.active = true; recording.track = ti; recording.scene = s; recording.overdub = overdub;
    recording.audio = t.type == TrackType::audio;
    auto& c = t.clips[s];
    const double len = std::max (1, bars) * (double) bpb;
    if (! overdub || ! c.used || c.audio != recording.audio) { c = ClipData(); c.used = true; c.audio = recording.audio; c.lengthBeats = len; }
    recording.length = c.lengthBeats;
    if (playing) recording.start = nextBar (beat);
    else { playing = true; beat = -(double) bpb; recording.start = 0.0; for (auto& tr : tracks) { tr.playingScene = -1; tr.queuedScene = -1; } }
    if (recording.audio)
    {   // don't play the slot while recording into it
        if (t.playingScene == s) t.playingScene = -1;
        const size_t frames = (size_t) (recording.length * samplesPerBeat());
        recording.l.assign (frames, 0.0f); recording.r.assign (frames, 0.0f);
        t.queuedScene = -1;
    }
    else { t.queuedScene = s; t.stopQueued = false; }
}

void SketchEngine::stopRecording()
{
    std::lock_guard<std::mutex> g (lock);
    if (! recording.active) return;
    if (recording.audio) { recording.audioDone = true; return; }
    // close notes still held
    auto& t = tracks[recording.track];
    for (auto& o : recording.open)
    {
        auto& c = t.clips[recording.scene];
        double rel = std::fmod (o.second.first - recording.start, c.lengthBeats);
        if (rel < 0) rel += c.lengthBeats;
        c.notes.push_back ({ o.first, rel, std::max (0.1, std::min (beat - o.second.first, c.lengthBeats)), o.second.second });
    }
    recording.open.clear();
    recording.active = false;
}

std::vector<NoteEv> SketchEngine::clipNotes (int ti, int s)
{
    std::lock_guard<std::mutex> g (lock);
    if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return {};
    auto n = tracks[ti].clips[s].notes;
    std::sort (n.begin(), n.end(), [] (const NoteEv& a, const NoteEv& b) { return a.start < b.start; });
    return n;
}

bool SketchEngine::takeRecordedAudio (int ti, int s, std::vector<float>& l, std::vector<float>& r)
{
    std::lock_guard<std::mutex> g (lock);
    if (! recording.audio || ! recording.audioDone || recording.track != ti || recording.scene != s) return false;
    const size_t n = std::min (recording.written, recording.l.size());
    l.assign (recording.l.begin(), recording.l.begin() + (long) n);
    r.assign (recording.r.begin(), recording.r.begin() + (long) n);
    l.resize (recording.l.size(), 0.0f); r.resize (recording.r.size(), 0.0f);
    auto& c = tracks[ti].clips[s];
    c.used = true; c.audio = true; c.lengthBeats = recording.length;
    c.audioL = std::make_shared<const std::vector<float>> (l); c.audioR = std::make_shared<const std::vector<float>> (r);
    recording = Recording();
    return true;
}

void SketchEngine::pushInput (const float* in, int n)
{
    std::lock_guard<std::mutex> g (lock);
    float peak = 0;
    for (int i = 0; i < n; ++i) peak = std::max (peak, std::abs (in[i]));
    inputLevel = std::max (peak, inputLevel * 0.9f);
    if (! recording.active || ! recording.audio || recording.audioDone || ! playing || beat < recording.start) return;
    for (int i = 0; i < n && recording.written < recording.l.size(); ++i)
    {
        recording.l[recording.written] = in[i]; recording.r[recording.written] = in[i];
        ++recording.written;
    }
    if (recording.written >= recording.l.size()) recording.audioDone = true;
}

//============================================================================== rendering
void SketchEngine::render (float* l, float* r, int n)
{
    std::memset (l, 0, sizeof (float) * (size_t) n);
    std::memset (r, 0, sizeof (float) * (size_t) n);
    std::lock_guard<std::mutex> g (lock);
    for (int done = 0; done < n;)
    {
        const int chunk = std::min (64, n - done);
        processChunk (l + done, r + done, chunk);
        done += chunk;
    }
    for (int i = 0; i < n; ++i) { l[i] = softClip (l[i] * 1.1f) * 0.98f; r[i] = softClip (r[i] * 1.1f) * 0.98f; }
}

void SketchEngine::sequence (Track& t, double b0, double b1, int n, std::vector<std::pair<int, int>>& ons, std::vector<int>& offs,
                             std::vector<int>& offPos, std::vector<int>& onPos)
{
    const double spb = samplesPerBeat();
    auto pos = [&] (double b) { return std::clamp ((int) ((b - b0) * spb), 0, n - 1); };
    // note-offs due in this chunk
    for (size_t i = 0; i < t.held.size();)
        if (t.held[i].second < b1) { offs.push_back (t.held[i].first); offPos.push_back (pos (t.held[i].second)); t.held.erase (t.held.begin() + (long) i); }
        else ++i;
    if (t.playingScene < 0) return;
    const auto& c = t.clips[t.playingScene];
    if (! c.used || c.audio || c.lengthBeats <= 0) return;
    const double rel0 = b0 - t.clipStart;
    if (b1 - t.clipStart <= 0) return;
    for (const auto& e : c.notes)
    {
        double k = std::ceil ((rel0 - e.start) / c.lengthBeats - 1.0e-9);
        if (k < 0) k = 0;
        for (double abs = t.clipStart + k * c.lengthBeats + e.start; abs < b1; abs += c.lengthBeats)
        {
            if (abs < b0 - 1.0e-9) continue;
            ons.push_back ({ e.note, e.velocity }); onPos.push_back (pos (abs));
            t.held.push_back ({ e.note, abs + std::max (0.02, e.length) });
        }
    }
}

void SketchEngine::processChunk (float* outL, float* outR, int n)
{
    float tl[64], tr[64];
    const double spb = samplesPerBeat();
    const double b0 = beat, b1 = beat + n / spb;
    bool anySolo = false;
    for (auto& t : tracks) if (t.exists && t.solo) anySolo = true;

    if (playing)
    {
        // launches wait for the next bar line (or start at the downbeat after a count-in)
        bool anyQueued = false;
        for (auto& t : tracks) if (t.queuedScene >= 0 || t.stopQueued) anyQueued = true;
        if (anyQueued)
        {
            const double q = b0 < 0 ? 0.0 : nextBar (b0);
            if (q >= b0 - 1.0e-9 && q < b1) startQueued (q);
        }
        // the slot being recorded into starts when the recording does
        if (recording.active && ! recording.audio && b0 <= recording.start && recording.start < b1)
        {
            auto& t = tracks[recording.track];
            if (t.queuedScene == recording.scene || t.playingScene != recording.scene) { t.playingScene = recording.scene; t.queuedScene = -1; t.clipStart = recording.start; }
        }
        // audio recording finished: play the new clip
        if (recording.active && recording.audio && recording.audioDone == false && recording.written >= recording.l.size() && ! recording.l.empty())
            recording.audioDone = true;
    }

    for (auto& t : tracks)
    {
        if (! t.exists) continue;
        std::fill (tl, tl + n, 0.0f); std::fill (tr, tr + n, 0.0f);
        if (playing)
        {
            std::vector<std::pair<int, int>> ons; std::vector<int> offs, offPos, onPos;
            sequence (t, b0, b1, n, ons, offs, offPos, onPos);
            if (ons.empty() && offs.empty())
            {
                if (t.type == TrackType::drums) t.drums.render (tl, tr, n); else if (t.type == TrackType::keys) t.synth.render (tl, tr, n);
            }
            else
            {   // split the chunk at each event so hits land on the sample
                struct Ev { int pos; int note; int vel; bool on; };
                std::vector<Ev> evs;
                for (size_t i = 0; i < offs.size(); ++i) evs.push_back ({ offPos[i], offs[i], 0, false });
                for (size_t i = 0; i < ons.size(); ++i) evs.push_back ({ onPos[i], ons[i].first, ons[i].second, true });
                std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.pos < b.pos || (a.pos == b.pos && ! a.on && b.on); });
                int at = 0;
                for (auto& e : evs)
                {
                    if (e.pos > at)
                    {
                        if (t.type == TrackType::drums) t.drums.render (tl + at, tr + at, e.pos - at); else if (t.type == TrackType::keys) t.synth.render (tl + at, tr + at, e.pos - at);
                        at = e.pos;
                    }
                    if (e.on) instrumentOn (t, e.note, e.vel); else instrumentOff (t, e.note);
                }
                if (at < n) { if (t.type == TrackType::drums) t.drums.render (tl + at, tr + at, n - at); else if (t.type == TrackType::keys) t.synth.render (tl + at, tr + at, n - at); }
            }
            // audio clips
            if (t.playingScene >= 0)
            {
                const auto& c = t.clips[t.playingScene];
                if (c.used && c.audio && c.audioL && ! c.audioL->empty())
                {
                    const double loopSamples = c.lengthBeats * spb;
                    for (int i = 0; i < n; ++i)
                    {
                        double rel = (b0 + i / spb - t.clipStart) * spb;
                        if (rel < 0) continue;
                        rel = std::fmod (rel, loopSamples);
                        const size_t idx = (size_t) rel;
                        if (idx < c.audioL->size()) { tl[i] += (*c.audioL)[idx]; tr[i] += (*c.audioR)[idx]; }
                    }
                }
            }
        }
        else
        {
            for (size_t i = 0; i < t.held.size(); ++i) instrumentOff (t, t.held[i].first);
            t.held.clear();
            if (t.type == TrackType::drums) t.drums.render (tl, tr, n); else if (t.type == TrackType::keys) t.synth.render (tl, tr, n);
        }
        const bool audible = ! t.mute && (! anySolo || t.solo);
        const float gl = t.volume * std::sqrt (0.5f * (1.0f - t.pan)) * 1.414f, gr = t.volume * std::sqrt (0.5f * (1.0f + t.pan)) * 1.414f;
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            const float a = tl[i] * gl, b = tr[i] * gr;
            peak = std::max (peak, std::max (std::abs (a), std::abs (b)));
            if (audible) { outL[i] += a; outR[i] += b; }
        }
        t.level = std::max (peak, t.level * 0.92f);
    }

    // metronome (and the count-in before recording)
    const bool click = playing && (metronome || b0 < 0);
    for (int i = 0; i < n; ++i)
    {
        if (click)
        {
            const double bb = b0 + i / spb, prev = b0 + (i - 1) / spb;
            if (i == 0 ? (std::floor (bb) == bb) : (std::floor (bb) != std::floor (prev)))
            {
                const int beatNum = (int) std::floor (bb + 1.0e-9);
                const bool downbeat = ((beatNum % bpb) + bpb) % bpb == 0;
                clickEnv = 1.0f; clickFreq = downbeat ? 1600.0f : 1000.0f;
            }
        }
        if (clickEnv > 0.0005f)
        {
            clickPhase += clickFreq / (float) sr; clickPhase -= std::floor (clickPhase);
            const float s = std::sin (kTwoPi * clickPhase) * clickEnv * 0.35f;
            outL[i] += s; outR[i] += s;
            clickEnv *= 0.9985f;
        }
    }
    if (playing) beat = b1;
    // recording loops forever for MIDI (overdubbing each pass); audio stops when the slot is full
}

std::vector<double> SketchEngine::state()
{
    std::lock_guard<std::mutex> g (lock);
    std::vector<double> s { beat, playing ? 1.0 : 0.0, recording.active ? 1.0 : 0.0, beat < 0 && playing ? 1.0 : 0.0, inputLevel };
    s.push_back (recording.active && recording.audio && recording.audioDone ? 1.0 : 0.0);      // audio take ready to collect
    for (auto& t : tracks)
    {
        double prog = 0;
        if (t.playingScene >= 0 && t.clips[t.playingScene].lengthBeats > 0)
        {
            prog = std::fmod (std::max (0.0, beat - t.clipStart), t.clips[t.playingScene].lengthBeats) / t.clips[t.playingScene].lengthBeats;
        }
        s.push_back (t.playingScene); s.push_back (t.stopQueued ? -2 : t.queuedScene); s.push_back (prog); s.push_back (t.level);
    }
    inputLevel *= 0.85f;
    return s;
}

void SketchEngine::renderClipOffline (int ti, int s, int loops, std::vector<float>& outL, std::vector<float>& outR)
{
    ClipData c; TrackType type; int sound;
    {
        std::lock_guard<std::mutex> g (lock);
        if (ti < 0 || ti >= kMaxTracks || s < 0 || s >= kScenes) return;
        c = tracks[ti].clips[s]; type = tracks[ti].type; sound = tracks[ti].sound;
    }
    if (! c.used) return;
    const double spb = samplesPerBeat();
    const size_t total = (size_t) (c.lengthBeats * loops * spb);
    outL.assign (total, 0.0f); outR.assign (total, 0.0f);
    if (c.audio)
    {
        for (size_t i = 0; i < total && c.audioL; ++i)
        {
            const size_t idx = (size_t) std::fmod ((double) i, c.lengthBeats * spb);
            if (idx < c.audioL->size()) { outL[i] = (*c.audioL)[idx]; outR[i] = (*c.audioR)[idx]; }
        }
        return;
    }
    DrumKit dk; PolySynth ps; dk.prepare ((float) sr); ps.prepare ((float) sr); dk.setKit (sound); ps.setPreset (sound);
    struct Ev { size_t pos; int note; int vel; bool on; };
    std::vector<Ev> evs;
    for (int k = 0; k < loops; ++k)
        for (auto& e : c.notes)
        {
            const double st = (k * c.lengthBeats + e.start) * spb;
            evs.push_back ({ (size_t) st, e.note, e.velocity, true });
            evs.push_back ({ (size_t) (st + std::max (0.02, e.length) * spb), e.note, 0, false });
        }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.pos < b.pos || (a.pos == b.pos && ! a.on && b.on); });
    size_t at = 0, ei = 0;
    while (at < total)
    {
        while (ei < evs.size() && evs[ei].pos <= at)
        {
            if (evs[ei].on) { if (type == TrackType::drums) dk.trigger (evs[ei].note, evs[ei].vel); else ps.noteOn (evs[ei].note, evs[ei].vel); }
            else if (type == TrackType::keys) ps.noteOff (evs[ei].note);
            ++ei;
        }
        size_t next = ei < evs.size() ? std::min (evs[ei].pos, total) : total;
        next = std::min (next, at + 256);
        const int n = (int) std::max<size_t> (1, next - at);
        if (type == TrackType::drums) dk.render (outL.data() + at, outR.data() + at, std::min (n, (int) (total - at)));
        else ps.render (outL.data() + at, outR.data() + at, std::min (n, (int) (total - at)));
        at += (size_t) n;
    }
    for (size_t i = 0; i < total; ++i) { outL[i] = softClip (outL[i] * 1.1f) * 0.98f; outR[i] = softClip (outR[i] * 1.1f) * 0.98f; }
}
}
