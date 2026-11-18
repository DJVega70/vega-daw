#include <map>
#include <set>
#include "DawSession.h"

static const juce::Identifier kindProp ("vegaKind");
static const juce::Identifier colourProp ("vegaColour");
static const juce::Identifier busNumProp ("vegaBusNumber");

DawSession::DawSession (te::Engine& e) : engine (e)
{
    static std::set<te::Engine*> registered;   // built-in types must be registered once per engine, before any edit loads
    if (registered.insert (&e).second) { e.getPluginManager().createBuiltInType<VegaDJEQPlugin>(); e.getPluginManager().createBuiltInType<VegaMasterStripPlugin>(); }
    newProject();
}

VegaMasterStripPlugin* DawSession::masterStrip (bool create)
{
    auto& list = editPtr->getMasterPluginList();
    for (auto* p : list.getPlugins()) if (auto* m = dynamic_cast<VegaMasterStripPlugin*> (p)) return m;
    if (! create) return nullptr;
    auto p = editPtr->getPluginCache().createNewPlugin (VegaMasterStripPlugin::xmlTypeName, {});
    if (p == nullptr) return nullptr;
    list.insertPlugin (p, 0, nullptr);
    return dynamic_cast<VegaMasterStripPlugin*> (p.get());
}

VegaDJEQPlugin* DawSession::djEq (te::AudioTrack& t, bool create)
{
    for (auto* p : t.pluginList.getPlugins()) if (auto* d = dynamic_cast<VegaDJEQPlugin*> (p)) return d;
    if (! create) return nullptr;
    auto p = editPtr->getPluginCache().createNewPlugin (VegaDJEQPlugin::xmlTypeName, {});
    if (p == nullptr) return nullptr;
    int index = t.pluginList.size();
    for (int i = 0; i < t.pluginList.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (t.pluginList[i]) != nullptr) { index = i; break; }
    t.pluginList.insertPlugin (p, index, nullptr);
    return dynamic_cast<VegaDJEQPlugin*> (p.get());
}
DawSession::~DawSession()
{
    if (editPtr) editPtr->getTransport().stop (false, true);
    editPtr.reset();
}

void DawSession::attachEdit (std::unique_ptr<te::Edit> e)
{
    if (editPtr) { if (onBeforeEditChange) onBeforeEditChange(); editPtr->getTransport().stop (false, true); }
    editPtr = std::move (e);
    vegaFocusedEdit = editPtr.get();
    editPtr->playInStopEnabled = true;
    for (auto* t : te::getAudioTracks (*editPtr)) djEq (*t);    // older projects get the DJ section too
    masterStrip();
    editPtr->getTransport().ensureContextAllocated();
    if (onEditChanged) onEditChanged();
}

void DawSession::newProject()
{
    projectFile = juce::File();
    auto tmp = engine.getTemporaryFileManager().getTempDirectory().getChildFile ("Untitled.vegaproj");
    tmp.deleteFile();
    auto e = te::createEmptyEdit (engine, tmp);
    e->tempoSequence.getTempos()[0]->setBpm (124.0);
    // a fresh session: one audio track, one MIDI track with the built-in synth
    attachEdit (std::move (e));
    // createEmptyEdit may already contain tracks; normalise to our starting layout
    for (auto* t : te::getAudioTracks (*editPtr)) editPtr->deleteTrack (t);
    addTrack (TrackKind::audio, "Audio 1");
    addTrack (TrackKind::midi, "Synth 1");
    editPtr->resetChangedStatus();
}

bool DawSession::openProject (const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    auto e = te::loadEditFromFile (engine, f);
    if (e == nullptr) return false;
    projectFile = f;
    attachEdit (std::move (e));
    return true;
}

bool DawSession::saveProjectAs (const juce::File& f)
{
    auto target = f.withFileExtension ("vegaproj");
    te::EditFileOperations ops (*editPtr);
    if (! ops.writeToFile (target, false)) return false;
    editPtr->editFileRetriever = [target] { return target; };
    projectFile = target;
    editPtr->resetChangedStatus();
    return true;
}
bool DawSession::saveProject() { return projectFile != juce::File() && saveProjectAs (projectFile); }
bool DawSession::hasUnsavedChanges() const { return editPtr && editPtr->hasChangedSinceSaved(); }

//==============================================================================
DawSession::TrackKind DawSession::kindOf (const te::Track& t)
{
    const auto k = t.state.getProperty (kindProp).toString();
    return k == "bus" ? TrackKind::bus : k == "midi" ? TrackKind::midi : TrackKind::audio;
}

juce::Colour DawSession::trackColour (const te::Track& t)
{
    if (t.state.hasProperty (colourProp)) return juce::Colour::fromString (t.state.getProperty (colourProp).toString());
    return juce::Colour (0xff48e1e8);
}

te::AudioTrack* DawSession::addTrack (TrackKind kind, const juce::String& name)
{
    auto& ed = *editPtr;
    auto existing = te::getAudioTracks (ed);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();
    auto t = ed.insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);
    if (t == nullptr) return nullptr;
    static const juce::uint32 palette[] { 0xff48e1e8, 0xffe8b448, 0xff7ee87a, 0xffe8708a, 0xffa58bff, 0xff6fb7ff, 0xffffa25e, 0xff5ee8b6 };
    int count = 0; for (auto* other : getTracks (true)) if (kindOf (*other) == kind) ++count;
    const juce::String kindName = kind == TrackKind::bus ? "Bus" : kind == TrackKind::midi ? "Synth" : "Audio";
    t->setName (name.isNotEmpty() ? name : kindName + " " + juce::String (count));
    t->state.setProperty (kindProp, kind == TrackKind::bus ? "bus" : kind == TrackKind::midi ? "midi" : "audio", nullptr);
    t->state.setProperty (colourProp, juce::Colour (palette[(size_t) (existing.size() % 8)]).toString(), nullptr);
    djEq (*t);
    if (kind == TrackKind::midi) { setInstrument (*t, te::FourOscPlugin::xmlTypeName); if (auto vp = t->getVolumePlugin()) vp->setVolumeDb (-10.0f); }
    if (kind == TrackKind::bus)
    {
        t->state.setProperty (busNumProp, -1, nullptr);
        busNumberFor (*t);
    }
    return t.get();
}

juce::Array<te::AudioTrack*> DawSession::getTracks (bool includeBuses) const
{
    juce::Array<te::AudioTrack*> r;
    for (auto* t : te::getAudioTracks (*editPtr)) if (includeBuses || kindOf (*t) != TrackKind::bus) r.add (t);
    return r;
}
juce::Array<te::AudioTrack*> DawSession::getBuses() const
{
    juce::Array<te::AudioTrack*> r;
    for (auto* t : te::getAudioTracks (*editPtr)) if (kindOf (*t) == TrackKind::bus) r.add (t);
    return r;
}

void DawSession::deleteTrack (te::Track& t)
{
    if (auto* at = dynamic_cast<te::AudioTrack*> (&t); at != nullptr && kindOf (*at) == TrackKind::bus)
        for (auto* other : getTracks (true))
        {
            if (getOutputBus (*other) == at) routeToBus (*other, nullptr);
            removeSend (*other, *at);
        }
    editPtr->deleteTrack (&t);
}

void DawSession::moveTrack (te::AudioTrack& t, int newIndex)
{
    auto tracks = getTracks (true);
    tracks.removeFirstMatchingValue (&t);
    newIndex = juce::jlimit (0, tracks.size(), newIndex);
    te::Track* preceding = newIndex > 0 ? tracks[newIndex - 1] : nullptr;
    editPtr->moveTrack (&t, te::TrackInsertPoint (nullptr, preceding));
}

//==============================================================================
void DawSession::routeToBus (te::AudioTrack& source, te::AudioTrack* bus)
{
    if (bus == &source) return;
    if (bus != nullptr) source.getOutput().setOutputToTrack (bus);
    else source.getOutput().setOutputToDefaultDevice (false);
}

te::AudioTrack* DawSession::getOutputBus (te::AudioTrack& t) const { return t.getOutput().getDestinationTrack(); }

int DawSession::busNumberFor (te::AudioTrack& bus)
{
    int n = bus.state.getProperty (busNumProp, -1);
    if (n >= 0) return n;
    juce::Array<int> used; for (auto* b : getBuses()) used.add ((int) b->state.getProperty (busNumProp, -1));
    n = 0; while (used.contains (n)) ++n;
    bus.state.setProperty (busNumProp, n, nullptr);
    // the return that receives sends lives first on the bus
    bool hasReturn = false;
    for (auto* p : bus.pluginList.getPlugins()) if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (p)) { r->busNumber = n; hasReturn = true; }
    if (! hasReturn)
        if (auto p = editPtr->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {}))
        {
            if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (p.get())) r->busNumber = n;
            bus.pluginList.insertPlugin (p, 0, nullptr);
        }
    return n;
}

te::AuxSendPlugin* DawSession::findSend (te::AudioTrack& source, te::AudioTrack& bus) const
{
    const int n = bus.state.getProperty (busNumProp, -1);
    for (auto* p : source.pluginList.getPlugins()) if (auto* s = dynamic_cast<te::AuxSendPlugin*> (p)) if (s->getBusNumber() == n) return s;
    return nullptr;
}

void DawSession::setSend (te::AudioTrack& source, te::AudioTrack& bus, float gainDb)
{
    if (&source == &bus) return;
    const int n = busNumberFor (bus);
    auto* s = findSend (source, bus);
    if (s == nullptr)
    {
        auto p = editPtr->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
        if (p == nullptr) return;
        // post-fader: place just before the volume plugin's successor (end of list, before the meter)
        int idx = source.pluginList.size();
        for (int i = 0; i < source.pluginList.size(); ++i) if (dynamic_cast<te::LevelMeterPlugin*> (source.pluginList[i])) { idx = i; break; }
        source.pluginList.insertPlugin (p, idx, nullptr);
        s = dynamic_cast<te::AuxSendPlugin*> (p.get());
        if (s == nullptr) return;
        s->busNumber = n;
    }
    s->setGainDb (gainDb);
}

void DawSession::removeSend (te::AudioTrack& source, te::AudioTrack& bus)
{
    if (auto* s = findSend (source, bus)) s->removeFromParent();
}

//==============================================================================
te::Plugin::Ptr DawSession::addPlugin (te::AudioTrack& t, const juce::String& type, const juce::PluginDescription* desc, int index)
{
    auto p = desc != nullptr ? editPtr->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc)
                             : editPtr->getPluginCache().createNewPlugin (type, {});
    if (p == nullptr) return {};
    if (index < 0)
    {   // inserts go before the volume/pan fader
        index = t.pluginList.size();
        for (int i = 0; i < t.pluginList.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (t.pluginList[i]) != nullptr || dynamic_cast<VegaDJEQPlugin*> (t.pluginList[i]) != nullptr) { index = i; break; }
    }
    t.pluginList.insertPlugin (p, index, nullptr);
    return p;
}

te::Plugin::Ptr DawSession::setInstrument (te::AudioTrack& t, const juce::String& type, const juce::PluginDescription* desc)
{
    // remove an existing synth in slot 0
    if (auto* first = t.pluginList[0]; first != nullptr && first->isSynth()) first->removeFromParent();
    return addPlugin (t, type, desc, 0);
}

juce::Array<te::Plugin*> DawSession::getInsertPlugins (te::AudioTrack& t) const
{
    juce::Array<te::Plugin*> r;
    for (auto* p : t.pluginList.getPlugins())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) || dynamic_cast<te::LevelMeterPlugin*> (p)
            || dynamic_cast<te::AuxSendPlugin*> (p) || dynamic_cast<te::AuxReturnPlugin*> (p) || dynamic_cast<VegaDJEQPlugin*> (p)) continue;
        r.add (p);
    }
    return r;
}

juce::StringArray DawSession::builtInEffectTypes()
{
    return { te::EqualiserPlugin::xmlTypeName, te::CompressorPlugin::xmlTypeName, te::ReverbPlugin::xmlTypeName, te::DelayPlugin::xmlTypeName,
             te::ChorusPlugin::xmlTypeName, te::PhaserPlugin::xmlTypeName, te::LowPassPlugin::xmlTypeName, te::PitchShiftPlugin::xmlTypeName };
}
juce::String DawSession::builtInDisplayName (const juce::String& type)
{
    if (type == te::FourOscPlugin::xmlTypeName) return "Vega 4-Osc Synth";
    if (type == te::SamplerPlugin::xmlTypeName) return "Vega Basic Sampler";
    if (type == te::EqualiserPlugin::xmlTypeName) return "4-Band EQ";
    if (type == te::CompressorPlugin::xmlTypeName) return "Compressor";
    if (type == te::ReverbPlugin::xmlTypeName) return "Reverb";
    if (type == te::DelayPlugin::xmlTypeName) return "Delay";
    if (type == te::ChorusPlugin::xmlTypeName) return "Chorus";
    if (type == te::PhaserPlugin::xmlTypeName) return "Phaser";
    if (type == te::LowPassPlugin::xmlTypeName) return "Filter";
    if (type == te::PitchShiftPlugin::xmlTypeName) return "Pitch Shift";
    return type;
}

//==============================================================================
te::WaveAudioClip::Ptr DawSession::insertAudioFile (te::AudioTrack& t, const juce::File& f, te::TimePosition at)
{
    te::AudioFile af (engine, f);
    if (! af.isValid()) return {};
    return t.insertWaveClip (f.getFileNameWithoutExtension(), f, { { at, te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
}

te::MidiClip::Ptr DawSession::insertMidiClip (te::AudioTrack& t, te::TimePosition start, te::TimeDuration length)
{
    return t.insertMIDIClip ({ start, start + length }, nullptr);
}

te::MidiClip::Ptr DawSession::importMidiFile (te::AudioTrack& t, const juce::File& f, te::TimePosition at)
{
    juce::FileInputStream in (f); juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) return {};
    mf.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence all;
    for (int i = 0; i < mf.getNumTracks(); ++i) all.addSequence (*mf.getTrack (i), 0);
    all.updateMatchedPairs();
    const double len = std::max (all.getEndTime(), 0.5);
    auto clip = insertMidiClip (t, at, te::TimeDuration::fromSeconds (std::ceil (len / barLength().inSeconds()) * barLength().inSeconds()));
    if (clip == nullptr) return {};
    auto& ts = editPtr->tempoSequence;
    for (int i = 0; i < all.getNumEvents(); ++i)
    {
        auto* ev = all.getEventPointer (i);
        if (! ev->message.isNoteOn() || ev->noteOffObject == nullptr) continue;
        const auto startB = ts.toBeats (at + te::TimeDuration::fromSeconds (ev->message.getTimeStamp())) - ts.toBeats (at);
        const auto endB = ts.toBeats (at + te::TimeDuration::fromSeconds (ev->noteOffObject->message.getTimeStamp())) - ts.toBeats (at);
        clip->getSequence().addNote (ev->message.getNoteNumber(), te::BeatPosition::fromBeats (startB.inBeats()), te::BeatDuration::fromBeats (std::max (0.01, endB.inBeats() - startB.inBeats())),
                                     ev->message.getVelocity(), 0, nullptr);
    }
    return clip;
}

void DawSession::splitClip (te::Clip& c, te::TimePosition t)
{
    if (auto* track = dynamic_cast<te::ClipTrack*> (c.getTrack()))
        if (c.getPosition().time.contains (t) && t > c.getPosition().getStart()) track->splitClip (c, t);
}

int DawSession::sliceAtTransients (te::WaveAudioClip& clip, float sens)
{
    std::unique_ptr<juce::AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (clip.getOriginalFile()));
    if (r == nullptr) return 0;
    const double ratio = clip.getSpeedRatio();
    const auto pos = clip.getPosition();
    const double srcStart = pos.getOffset().inSeconds() * ratio, srcLen = pos.getLength().inSeconds() * ratio;
    const auto first = (juce::int64) (srcStart * r->sampleRate), count = (juce::int64) (srcLen * r->sampleRate);
    if (count < 2048) return 0;
    juce::AudioBuffer<float> b ((int) std::min<juce::uint32> (2, r->numChannels), (int) count);
    r->read (&b, 0, (int) count, first, true, true);
    const int hop = 256, win = 512;
    std::vector<float> e;
    for (int s = 0; s + win <= b.getNumSamples(); s += hop) { double a = 0; for (int i = 0; i < win; ++i) { const float x = b.getSample (0, s + i); a += x * x; } e.push_back ((float) (a / win)); }
    float peak = 0; for (float v : e) peak = std::max (peak, v);
    const float ratioThr = 1.5f + (1.0f - sens) * 18.0f, floorE = peak * std::pow (10.0f, -(20.0f + sens * 30.0f) / 10.0f);
    std::vector<double> cuts; double last = 0;
    for (size_t k = 4; k < e.size(); ++k)
    {
        const float prev = (e[k - 2] + e[k - 3] + e[k - 4]) / 3.0f + 1.0e-10f;
        if (e[k] > floorE && e[k] / prev > ratioThr && e[k] >= e[k - 1])
        {
            const double t = (double) (k * hop - hop / 2) / r->sampleRate;
            if (t - last > 0.07 && t > 0.03) { cuts.push_back (t); last = t; }
        }
    }
    // split from the end so earlier positions stay valid
    te::Clip* current = &clip; int made = 0;
    auto* track = dynamic_cast<te::ClipTrack*> (clip.getTrack());
    if (track == nullptr) return 0;
    edit().getUndoManager().beginNewTransaction ("Slice at transients");
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
    {
        const auto at = pos.getStart() + te::TimeDuration::fromSeconds (*it / ratio);
        if (current->getPosition().time.contains (at)) { track->splitClip (*current, at); ++made; }
    }
    return made;
}

void DawSession::setClipFades (te::AudioClipBase& c, te::TimeDuration in, te::TimeDuration out)
{
    c.setFadeIn (in); c.setFadeOut (out);
}

void DawSession::setClipWarp (te::WaveAudioClip& c, bool followTempo, float semitones)
{
    c.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
    c.setAutoTempo (followTempo);
    c.setPitchChange (semitones);
}

//==============================================================================
void DawSession::setTempo (double bpm) { editPtr->tempoSequence.getTempos()[0]->setBpm (juce::jlimit (20.0, 400.0, bpm)); }
double DawSession::getTempo() const { return editPtr->tempoSequence.getTempos()[0]->getBpm(); }
void DawSession::setTimeSig (int num, int den)
{
    auto* ts = editPtr->tempoSequence.getTimeSig (0);
    ts->numerator = juce::jlimit (1, 16, num); ts->denominator = den;
}
te::TimeDuration DawSession::barLength() const
{
    auto* ts = editPtr->tempoSequence.getTimeSig (0);
    return te::TimeDuration::fromSeconds (60.0 / getTempo() * ts->numerator * 4.0 / ts->denominator);
}
te::TimePosition DawSession::snap (te::TimePosition t, int div) const
{
    if (div <= 0) return t;
    auto& ts = editPtr->tempoSequence;
    const double b = ts.toBeats (t).inBeats();
    const double snapped = std::round (b * div) / div;
    return ts.toTime (te::BeatPosition::fromBeats (std::max (0.0, snapped)));
}

te::TimeRange DawSession::contentRange() const
{
    te::TimePosition end;
    for (auto* t : te::getAudioTracks (*editPtr)) for (auto* c : t->getClips()) end = std::max (end, c->getPosition().getEnd());
    return { te::TimePosition(), end };
}

bool DawSession::exportWav (const juce::File& f, std::optional<te::TimeRange> range)
{
    auto r = range.value_or (contentRange());
    if (r.getLength() <= 0s) return false;
    r = r.withEnd (r.getEnd() + 2s);    // let reverbs / delays ring out
    juce::BigInteger tracksToDo; tracksToDo.setRange (0, te::getAllTracks (*editPtr).size(), true);
    te::Renderer::Parameters p (*editPtr);
    p.destFile = f.withFileExtension ("wav");
    p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 24; p.blockSizeForAudio = 512; p.sampleRateForAudio = 48000;
    p.time = r; p.tracksToDo = tracksToDo; p.usePlugins = true; p.useMasterPlugins = true; p.checkNodesForAudio = false;
    p.destFile.deleteFile();
    return te::Renderer::renderToFile ("Export", p).existsAsFile();
}

juce::Array<te::AudioTrack*> DawSession::placeStems (const juce::Array<juce::File>& stems, const juce::StringArray& names, const juce::String& baseName,
                                                     te::WaveAudioClip* original, te::TimePosition at)
{
    static const std::map<juce::String, juce::uint32> stemColour {
        { "Vocals", 0xffe8708a }, { "Drums", 0xffffa25e }, { "Bass", 0xff7ee87a }, { "Other", 0xff6fb7ff }, { "Guitar", 0xffe8b448 }, { "Piano", 0xffa58bff } };
    juce::Array<te::AudioTrack*> made;
    // put the stems right after the original clip's track (or at the end)
    for (int i = 0; i < stems.size(); ++i)
    {
        auto* t = addTrack (TrackKind::audio, baseName + " " + names[i]);
        if (t == nullptr) continue;
        if (auto it = stemColour.find (names[i]); it != stemColour.end()) t->state.setProperty ("vegaColour", juce::Colour (it->second).toString(), nullptr);
        auto c = insertAudioFile (*t, stems[i], original != nullptr ? original->getPosition().getStart() : at);
        if (c != nullptr && original != nullptr)
        {
            c->setPosition (original->getPosition());          // same start, length and offset into the file
            if (original->getAutoTempo() || std::abs (original->getPitchChange()) > 0.01f) setClipWarp (*c, original->getAutoTempo(), original->getPitchChange());
            c->setGainDB (original->getGainDB());
        }
        made.add (t);
    }
    if (original != nullptr && ! made.isEmpty()) original->setMuted (true);
    return made;
}
