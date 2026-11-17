#include "ArrangeView.h"

//==============================================================================
class ArrangeView::TrackHeader : public juce::Component
{
public:
    TrackHeader (ArrangeView& o, te::AudioTrack& t) : owner (o), track (&t)
    {
        name.setText (t.getName(), juce::dontSendNotification);
        name.setEditable (false, true);
        name.setFont (VegaFonts::ui (13.0f, true));
        name.setColour (juce::Label::textColourId, VegaColours::text);
        name.onTextChange = [this] { if (track) track->setName (name.getText()); };
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);
        auto setup = [this] (juce::TextButton& b, const char* txt, juce::Colour on)
        {
            b.setButtonText (txt); b.setClickingTogglesState (false);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            addAndMakeVisible (b);
        };
        setup (mute, "M", juce::Colour (0xffe0a030)); setup (solo, "S", juce::Colour (0xff48e1e8)); setup (arm, "R", juce::Colour (0xffe04848)); setup (input, "IN", VegaColours::accentDim);
        mute.onClick = [this] { track->setMute (! track->isMuted (false)); refresh(); };
        solo.onClick = [this] { track->setSolo (! track->isSolo (false)); refresh(); };
        arm.onClick = [this] { toggleArm(); };
        input.onClick = [this] { inputMenu(); };
        const bool bus = DawSession::kindOf (t) == DawSession::TrackKind::bus;
        arm.setVisible (! bus); input.setVisible (! bus);
        vol.setSliderStyle (juce::Slider::LinearBar); vol.setRange (-60.0, 6.0, 0.1); vol.setSkewFactorFromMidPoint (-12.0);
        vol.setTextValueSuffix (" dB"); vol.setColour (juce::Slider::trackColourId, DawSession::trackColour (t).withAlpha (0.45f));
        vol.onValueChange = [this] { if (auto vp = track->getVolumePlugin()) vp->setVolumeDb ((float) vol.getValue()); };
        addAndMakeVisible (vol);
        refresh();
    }
    void refresh()
    {
        if (track == nullptr) return;
        mute.setToggleState (track->isMuted (false), juce::dontSendNotification);
        solo.setToggleState (track->isSolo (false), juce::dontSendNotification);
        arm.setToggleState (isArmed(), juce::dontSendNotification);
        if (auto vp = track->getVolumePlugin()) if (! vol.isMouseButtonDown()) vol.setValue (vp->getVolumeDb(), juce::dontSendNotification);
        if (name.getText() != track->getName() && ! name.isBeingEdited()) name.setText (track->getName(), juce::dontSendNotification);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const bool sel = owner.vs.selectedTrack == track->itemID;
        g.setColour (juce::Colour (sel ? 0xff2a2f2f : 0xff222222)); g.fillRect (getLocalBounds().reduced (0, 1));
        g.setColour (DawSession::trackColour (*track)); g.fillRect (0, 1, 5, getHeight() - 2);
        g.setColour (VegaColours::line); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
        const auto kind = DawSession::kindOf (*track);
        g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::textDim);
        juce::String sub = kind == DawSession::TrackKind::bus ? "BUS" : kind == DawSession::TrackKind::midi ? "INSTRUMENT" : "AUDIO";
        if (kind == DawSession::TrackKind::midi)
            if (auto* p = track->pluginList[0]; p != nullptr && p->isSynth()) sub = p->getName().toUpperCase();
        if (auto* out = owner.vs.session.getOutputBus (*track)) sub << "  >  " << out->getName().toUpperCase();
        g.drawText (sub, 12, 26, getWidth() - 20, 12, juce::Justification::left);
    }
    void resized() override
    {
        name.setBounds (8, 4, getWidth() - 16, 22);
        auto row = juce::Rectangle<int> (10, 42, getWidth() - 20, 22);
        for (auto* b : { &mute, &solo, &arm, &input }) { if (! b->isVisible()) continue; b->setBounds (row.removeFromLeft (b == &input ? 34 : 26)); row.removeFromLeft (4); }
        vol.setBounds (row.withTrimmedLeft (4));
        if (getHeight() < 70) { vol.setVisible (false); } else vol.setVisible (true);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.vs.selectTrack (track.get());
        if (e.mods.isPopupMenu()) menu();
        dragStartY = e.getScreenY();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { name.showEditor(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        // drag the bottom edge to resize, or drag the header to reorder
        if (dragStartY - getScreenY() > getHeight() - 10 || resizing)
        {
            resizing = true;
            track->state.setProperty ("vegaHeight", juce::jlimit (44, 260, e.getScreenY() - getScreenY()), nullptr);
            owner.resized(); return;
        }
        auto p = owner.getLocalPoint (nullptr, e.getScreenPosition());
        if (auto* target = owner.trackAtY (p.y - rulerH))
            if (target != track)
            {
                auto tracks = owner.vs.session.getTracks();
                owner.vs.session.moveTrack (*track, tracks.indexOf (target));
            }
    }
    void mouseUp (const juce::MouseEvent&) override { resizing = false; }
    te::AudioTrack::Ptr track;
private:
    bool isArmed()
    {
        for (auto* in : track->edit.getAllInputDevices())
            if (te::isOnTargetTrack (*in, *track, 0) && in->isRecordingEnabled (track->itemID)) return true;
        return false;
    }
    void ensureInput()
    {
        // assign a default input if the track has none: first stereo/mono wave input for audio, all MIDI ins for instruments
        bool has = false;
        for (auto* in : track->edit.getAllInputDevices()) if (te::isOnTargetTrack (*in, *track, 0)) has = true;
        if (has) return;
        const bool midi = DawSession::kindOf (*track) == DawSession::TrackKind::midi;
        for (auto* in : track->edit.getAllInputDevices())
        {
            const auto type = in->getInputDevice().getDeviceType();
            if (midi ? type == te::InputDevice::physicalMidiDevice : type == te::InputDevice::waveDevice)
            {
                [[maybe_unused]] auto r = in->setTarget (track->itemID, ! midi, &track->edit.getUndoManager(), 0);
                if (! midi) break;
            }
        }
    }
    void toggleArm()
    {
        ensureInput();
        const bool now = ! isArmed();
        for (auto* in : track->edit.getAllInputDevices())
            if (te::isOnTargetTrack (*in, *track, 0)) in->setRecordingEnabled (track->itemID, now);
        refresh();
    }
    void inputMenu()
    {
        juce::PopupMenu m;
        int id = 1; std::vector<te::InputDeviceInstance*> list;
        for (auto* in : track->edit.getAllInputDevices())
        {
            const auto type = in->getInputDevice().getDeviceType();
            if (type != te::InputDevice::waveDevice && type != te::InputDevice::physicalMidiDevice) continue;
            const bool on = te::isOnTargetTrack (*in, *track, 0);
            m.addItem (id++, (type == te::InputDevice::waveDevice ? "Audio: " : "MIDI: ") + in->getInputDevice().getName(), true, on);
            list.push_back (in);
        }
        if (list.empty()) m.addItem (-1, "No inputs enabled - Options > Audio / MIDI Settings", false);
        m.addSeparator();
        m.addItem (900, "Monitor input (hear it live)", true, monitoring());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&input), [this, list] (int r)
        {
            if (r == 900)
            {
                for (auto* in : track->edit.getAllInputDevices())
                    if (te::isOnTargetTrack (*in, *track, 0))
                        in->getInputDevice().setMonitorMode (monitoring() ? te::InputDevice::MonitorMode::off : te::InputDevice::MonitorMode::on);
                return;
            }
            if (r < 1 || r > (int) list.size()) return;
            auto* in = list[(size_t) r - 1];
            if (te::isOnTargetTrack (*in, *track, 0)) [[maybe_unused]] auto res = in->removeTarget (track->itemID, &track->edit.getUndoManager());
            else [[maybe_unused]] auto res = in->setTarget (track->itemID, in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice, &track->edit.getUndoManager(), 0);
            refresh();
        });
    }
    bool monitoring()
    {
        for (auto* in : track->edit.getAllInputDevices())
            if (te::isOnTargetTrack (*in, *track, 0)) return in->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on;
        return false;
    }
    void menu()
    {
        juce::PopupMenu m, colours, route;
        m.addItem (1, "Rename"); 
        static const juce::uint32 pal[] { 0xff48e1e8, 0xffe8b448, 0xff7ee87a, 0xffe8708a, 0xffa58bff, 0xff6fb7ff, 0xffffa25e, 0xff5ee8b6, 0xffcccccc };
        for (int i = 0; i < 9; ++i) colours.addColouredItem (100 + i, "Colour " + juce::String (i + 1), juce::Colour (pal[i]));
        m.addSubMenu ("Colour", colours);
        auto& s = owner.vs.session;
        route.addItem (200, "Master", true, s.getOutputBus (*track) == nullptr);
        auto buses = s.getBuses();
        for (int i = 0; i < buses.size(); ++i) if (buses[i] != track.get()) route.addItem (201 + i, buses[i]->getName(), true, s.getOutputBus (*track) == buses[i]);
        m.addSubMenu ("Output", route);
        m.addSeparator();
        m.addItem (3, "Delete track");
        m.showMenuAsync ({}, [this, buses] (int r)
        {
            if (track == nullptr) return;
            if (r == 1) name.showEditor();
            else if (r >= 100 && r < 109) { static const juce::uint32 pal[] { 0xff48e1e8, 0xffe8b448, 0xff7ee87a, 0xffe8708a, 0xffa58bff, 0xff6fb7ff, 0xffffa25e, 0xff5ee8b6, 0xffcccccc }; track->state.setProperty ("vegaColour", juce::Colour (pal[r - 100]).toString(), nullptr); }
            else if (r == 200) owner.vs.session.routeToBus (*track, nullptr);
            else if (r > 200) owner.vs.session.routeToBus (*track, buses[r - 201]);
            else if (r == 3) { auto t = track; owner.vs.session.deleteTrack (*t); }
            owner.repaint();
        });
    }
    ArrangeView& owner;
    juce::Label name; juce::TextButton mute, solo, arm, input; juce::Slider vol;
    int dragStartY = 0; bool resizing = false;
};

//==============================================================================
class ArrangeView::Lanes : public juce::Component
{
public:
    explicit Lanes (ArrangeView& o) : owner (o), vs (o.vs) { setWantsKeyboardFocus (false); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff181818));
        auto& ed = vs.edit();
        const int num = ed.tempoSequence.getTimeSig (0)->numerator;
        // rows
        int y = -vs.scrollY;
        for (auto* t : vs.session.getTracks())
        {
            const int h = owner.trackHeight (*t);
            if (y + h >= 0 && y < getHeight())
            {
                g.setColour (juce::Colour (vs.selectedTrack == t->itemID ? 0xff1f2323 : 0xff1b1b1b)); g.fillRect (0, y + 1, getWidth(), h - 2);
                g.setColour (juce::Colour (0xff111111)); g.drawHorizontalLine (y + h - 1, 0, (float) getWidth());
            }
            y += h;
        }
        // grid
        const double first = std::floor (vs.scrollBeats), last = vs.xToBeat ((float) getWidth(), 0);
        for (double b = first; b <= last; b += 1.0)
        {
            const bool bar = ((int) b % num) == 0;
            if (! bar && vs.pixelsPerBeat < 12) continue;
            g.setColour (juce::Colours::white.withAlpha (bar ? 0.09f : 0.035f));
            g.drawVerticalLine ((int) vs.beatToX (b, 0), 0, (float) std::min (getHeight(), y));
        }
        // loop
        auto& tr = ed.getTransport();
        if (tr.looping)
        {
            const auto lr = tr.getLoopRange();
            const float x0 = vs.beatToX (vs.timeToBeats (lr.getStart()), 0), x1 = vs.beatToX (vs.timeToBeats (lr.getEnd()), 0);
            g.setColour (VegaColours::accent.withAlpha (0.05f)); g.fillRect (juce::Rectangle<float> (x0, 0, x1 - x0, (float) getHeight()));
        }
        // clips
        y = -vs.scrollY;
        for (auto* t : vs.session.getTracks())
        {
            const int h = owner.trackHeight (*t);
            if (y + h >= 0 && y < getHeight())
                for (auto* c : t->getClips()) drawClip (g, *c, y, h);
            y += h;
        }
        // recording preview
        if (tr.isRecording())
        {
            const float x0 = vs.beatToX (vs.timeToBeats (tr.getTimeWhenStarted()), 0), x1 = vs.beatToX (vs.timeToBeats (tr.getPosition()), 0);
            int yy = -vs.scrollY;
            for (auto* t : vs.session.getTracks())
            {
                const int h = owner.trackHeight (*t);
                bool armed = false;
                for (auto* in : ed.getAllInputDevices()) if (te::isOnTargetTrack (*in, *t, 0) && in->isRecordingEnabled (t->itemID)) armed = true;
                if (armed) { g.setColour (juce::Colour (0x55e04848)); g.fillRect (juce::Rectangle<float> (x0, (float) yy + 4, x1 - x0, (float) h - 8)); }
                yy += h;
            }
        }
        // drop hint
        if (dropY >= 0) { g.setColour (VegaColours::accent); g.fillRect (0, dropY, getWidth(), 2); }
        // playhead
        const float px = vs.beatToX (vs.timeToBeats (tr.getPosition()), 0);
        g.setColour (VegaColours::gold); g.fillRect (px - 0.5f, 0.0f, 1.5f, (float) getHeight());
    }

    void drawClip (juce::Graphics& g, te::Clip& c, int rowY, int rowH)
    {
        const auto pos = c.getPosition();
        const float x0 = vs.beatToX (vs.timeToBeats (pos.getStart()), 0), x1 = vs.beatToX (vs.timeToBeats (pos.getEnd()), 0);
        if (x1 < 0 || x0 > getWidth()) return;
        auto r = juce::Rectangle<float> (x0, (float) rowY + 3, std::max (2.0f, x1 - x0), (float) rowH - 6);
        const auto col = dawui::clipColour (c);
        const bool sel = vs.selectedClip == c.itemID;
        g.setColour (col.withAlpha (0.22f).withMultipliedBrightness (c.isMuted() ? 0.4f : 1.0f)); g.fillRoundedRectangle (r, 4.0f);
        auto title = r.removeFromTop (15.0f);
        g.setColour (col.withAlpha (0.55f)); g.fillRoundedRectangle (title.withHeight (15.0f), 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f)); g.setFont (VegaFonts::ui (10.5f, true));
        g.drawText (c.getName(), title.reduced (5, 0).toNearestInt(), juce::Justification::centredLeft, true);
        g.saveState(); g.reduceClipRegion (r.toNearestInt());
        if (auto* ac = dynamic_cast<te::WaveAudioClip*> (&c))
        {
            auto& th = thumbFor (*ac);
            const auto srcStart = ac->getPosition().getOffset().inSeconds() * ac->getSpeedRatio();
            const auto srcLen = pos.getLength().inSeconds() * ac->getSpeedRatio();
            g.setColour (col);
            th.drawChannels (g, r.toNearestInt(), { te::TimePosition::fromSeconds (srcStart), te::TimePosition::fromSeconds (srcStart + srcLen) }, juce::Decibels::decibelsToGain (ac->getGainDB()));
            // fades
            const float fi = (float) (ac->getFadeIn().inSeconds() / pos.getLength().inSeconds()) * r.getWidth();
            const float fo = (float) (ac->getFadeOut().inSeconds() / pos.getLength().inSeconds()) * r.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            if (fi > 1) g.drawLine (r.getX(), r.getBottom(), r.getX() + fi, r.getY(), 1.2f);
            if (fo > 1) g.drawLine (r.getRight() - fo, r.getY(), r.getRight(), r.getBottom(), 1.2f);
            if (ac->getAutoTempo() || std::abs (ac->getPitchChange()) > 0.01f)
            {
                g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::gold);
                juce::String tag; if (ac->getAutoTempo()) tag << "WARP "; if (std::abs (ac->getPitchChange()) > 0.01f) tag << juce::String (ac->getPitchChange(), 1) << " st";
                g.drawText (tag, r.reduced (4, 2).toNearestInt(), juce::Justification::topRight);
            }
        }
        else if (auto* mc = dynamic_cast<te::MidiClip*> (&c))
        {
            auto& seq = mc->getSequence();
            int lo = 127, hi = 0;
            for (auto* n : seq.getNotes()) { lo = std::min (lo, n->getNoteNumber()); hi = std::max (hi, n->getNoteNumber()); }
            if (hi >= lo)
            {
                lo -= 2; hi += 2;
                const float nh = std::max (1.5f, std::min (6.0f, r.getHeight() / (float) (hi - lo + 1)));
                g.setColour (col);
                const double clipStartBeat = vs.timeToBeats (pos.getStart()) - mc->getOffsetInBeats().inBeats();
                for (auto* n : seq.getNotes())
                {
                    const float nx0 = vs.beatToX (clipStartBeat + n->getStartBeat().inBeats(), 0), nx1 = vs.beatToX (clipStartBeat + n->getEndBeat().inBeats(), 0);
                    const float ny = r.getBottom() - (n->getNoteNumber() - lo + 1) * (r.getHeight() / (float) (hi - lo + 1));
                    g.fillRect (nx0, ny, std::max (1.5f, nx1 - nx0 - 1), nh);
                }
            }
        }
        g.restoreState();
        g.setColour (sel ? juce::Colours::white : col.withAlpha (0.8f));
        g.drawRoundedRectangle (juce::Rectangle<float> (x0, (float) rowY + 3, std::max (2.0f, x1 - x0), (float) rowH - 6), 4.0f, sel ? 2.0f : 1.0f);
    }

    te::SmartThumbnail& thumbFor (te::WaveAudioClip& c)
    {
        const auto key = c.itemID.toString() + c.getOriginalFile().getFullPathName();
        auto it = thumbs.find (key);
        if (it == thumbs.end())
            it = thumbs.emplace (key, std::make_unique<te::SmartThumbnail> (vs.edit().engine, te::AudioFile (vs.edit().engine, c.getOriginalFile()), *this, &vs.edit())).first;
        return *it->second;
    }

    // ---------------------------------------------------------------- mouse
    enum class Drag { none, move, trimStart, trimEnd, fadeIn, fadeOut };
    struct Hit { te::Clip* clip = nullptr; Drag zone = Drag::none; };
    Hit hitTest (juce::Point<float> p)
    {
        int rowTop = 0;
        auto* t = owner.trackAtY ((int) p.y, &rowTop);
        if (t == nullptr) return {};
        const int h = owner.trackHeight (*t);
        for (auto* c : t->getClips())
        {
            const auto pos = c->getPosition();
            const float x0 = vs.beatToX (vs.timeToBeats (pos.getStart()), 0), x1 = vs.beatToX (vs.timeToBeats (pos.getEnd()), 0);
            if (p.x < x0 || p.x > x1) continue;
            const bool audio = dynamic_cast<te::AudioClipBase*> (c) != nullptr;
            const float top = (float) rowTop + 3;
            if (audio && p.y < top + 15 && p.x < x0 + 10) return { c, Drag::fadeIn };
            if (audio && p.y < top + 15 && p.x > x1 - 10) return { c, Drag::fadeOut };
            if (p.x < x0 + 6) return { c, Drag::trimStart };
            if (p.x > x1 - 6) return { c, Drag::trimEnd };
            juce::ignoreUnused (h);
            return { c, Drag::move };
        }
        return {};
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = hitTest (e.position);
        setMouseCursor (h.zone == Drag::trimStart || h.zone == Drag::trimEnd ? juce::MouseCursor::LeftRightResizeCursor
                        : h.zone == Drag::fadeIn || h.zone == Drag::fadeOut ? juce::MouseCursor::CrosshairCursor
                        : h.zone == Drag::move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        const auto h = hitTest (e.position);
        drag = h.zone; dragged = false;
        if (h.clip != nullptr)
        {
            vs.selectClip (h.clip);
            const auto pos = h.clip->getPosition();
            origStart = pos.getStart(); origEnd = pos.getEnd(); origOffset = pos.getOffset();
            grabBeat = vs.xToBeat (e.position.x, 0);
            if (auto* ac = dynamic_cast<te::AudioClipBase*> (h.clip)) { origFadeIn = ac->getFadeIn(); origFadeOut = ac->getFadeOut(); }
            if (e.mods.isPopupMenu()) { clipMenu (*h.clip, e); drag = Drag::none; }
            else if (e.mods.isAltDown() && drag == Drag::move)
            {   // alt-drag duplicates
                owner.duplicateSelectedClip();
            }
        }
        else
        {
            vs.selectClip (nullptr);
            if (auto* t = owner.trackAtY ((int) e.position.y)) vs.selectTrack (t);
            if (! e.mods.isPopupMenu())
                vs.edit().getTransport().setPosition (vs.beatsToTime (std::max (0.0, vs.snapBeat (vs.xToBeat (e.position.x, 0), e.mods.isShiftDown()))));
        }
        owner.repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* c = vs.getSelectedClip();
        if (c == nullptr || drag == Drag::none) return;
        if (! dragged && e.getDistanceFromDragStart() < 3) return;
        if (! dragged) { vs.edit().getUndoManager().beginNewTransaction ("Edit clip"); dragged = true; }
        const bool free = e.mods.isShiftDown();
        const double beat = vs.xToBeat (e.position.x, 0);
        const double startB = vs.timeToBeats (origStart), endB = vs.timeToBeats (origEnd);
        switch (drag)
        {
            case Drag::move:
            {
                double newStart = vs.snapBeat (startB + (beat - grabBeat), free);
                newStart = std::max (0.0, newStart);
                c->setStart (vs.beatsToTime (newStart), false, true);
                // move between tracks of the same kind
                if (auto* target = owner.trackAtY ((int) e.position.y))
                    if (target != c->getTrack() && (DawSession::kindOf (*target) == DawSession::TrackKind::midi) == (dynamic_cast<te::MidiClip*> (c) != nullptr))
                        c->moveTo (*target);
                break;
            }
            case Drag::trimStart:
            {
                double ns = juce::jlimit (0.0, endB - 1.0 / 16, vs.snapBeat (beat, free));
                const auto newStart = vs.beatsToTime (ns);
                const auto delta = newStart - origStart;
                if (origOffset + delta < 0s) break;              // can't reveal before the source start
                c->setStart (newStart, false, false);
                c->setOffset (origOffset + delta);
                c->setLength (origEnd - newStart, false);
                break;
            }
            case Drag::trimEnd:
            {
                const double ne = std::max (startB + 1.0 / 16, vs.snapBeat (beat, free));
                auto newLen = vs.beatsToTime (ne) - origStart;
                if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c); ac != nullptr && ! ac->getAutoTempo() && ! ac->isLooping())
                {   // audio can't extend past its source
                    const double maxLen = ac->getSourceLength().inSeconds() / ac->getSpeedRatio() - origOffset.inSeconds();
                    newLen = te::TimeDuration::fromSeconds (std::min (newLen.inSeconds(), maxLen));
                }
                c->setLength (newLen, false);
                break;
            }
            case Drag::fadeIn: case Drag::fadeOut:
            {
                if (auto* ac = dynamic_cast<te::AudioClipBase*> (c))
                {
                    const double len = (origEnd - origStart).inSeconds();
                    const double t = (vs.beatsToTime (std::max (0.0, beat)) - origStart).inSeconds();
                    if (drag == Drag::fadeIn) ac->setFadeIn (te::TimeDuration::fromSeconds (juce::jlimit (0.0, len, t)));
                    else ac->setFadeOut (te::TimeDuration::fromSeconds (juce::jlimit (0.0, len, len - t)));
                }
                break;
            }
            default: break;
        }
        owner.repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag = Drag::none; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto h = hitTest (e.position);
        if (h.clip != nullptr)
        {
            if (dynamic_cast<te::MidiClip*> (h.clip) != nullptr && vs.onOpenPianoRoll) { vs.selectClip (h.clip); vs.onOpenPianoRoll(); }
            return;
        }
        if (auto* t = owner.trackAtY ((int) e.position.y))
            if (DawSession::kindOf (*t) == DawSession::TrackKind::midi)
            {   // new one-bar MIDI clip at the bar under the mouse
                const int num = vs.edit().tempoSequence.getTimeSig (0)->numerator;
                const double bar = std::floor (vs.xToBeat (e.position.x, 0) / num) * num;
                auto clip = vs.session.insertMidiClip (*t, vs.beatsToTime (std::max (0.0, bar)), vs.beatsToTime (bar + num) - vs.beatsToTime (bar));
                if (clip) { vs.selectClip (clip.get()); if (vs.onOpenPianoRoll) vs.onOpenPianoRoll(); }
            }
    }
    void clipMenu (te::Clip& c, const juce::MouseEvent&)
    {
        juce::PopupMenu m;
        m.addItem (1, "Split at playhead      Ctrl+E");
        m.addItem (2, "Duplicate                    Ctrl+D");
        m.addItem (3, "Delete                         Del");
        m.addItem (4, c.isMuted() ? "Unmute" : "Mute");
        m.addItem (5, "Rename");
        auto* ac = dynamic_cast<te::WaveAudioClip*> (&c);
        if (ac != nullptr)
        {
            m.addSeparator();
            m.addItem (10, "Warp to project tempo", true, ac->getAutoTempo());
            juce::PopupMenu pitch; for (int st = -12; st <= 12; ++st) pitch.addItem (100 + st + 12, (st > 0 ? "+" : "") + juce::String (st) + " semitones", true, juce::roundToInt (ac->getPitchChange()) == st);
            m.addSubMenu ("Pitch", pitch);
            m.addItem (11, "Reverse", true, ac->getIsReversed());
            m.addItem (12, "Loop clip", true, ac->isLooping());
            juce::PopupMenu gain; for (int db : { -12, -6, -3, 0, 3, 6, 12 }) gain.addItem (200 + db + 12, juce::String (db) + " dB", true, juce::roundToInt (ac->getGainDB()) == db);
            m.addSubMenu ("Clip gain", gain);
            m.addItem (13, "Slice at transients (new clips)");
        }
        else if (auto* mc = dynamic_cast<te::MidiClip*> (&c))
        {
            m.addSeparator();
            m.addItem (20, "Open in piano roll");
            m.addItem (21, "Quantise notes to grid");
            juce::ignoreUnused (mc);
        }
        m.showMenuAsync ({}, [this, id = c.itemID] (int r)
        {
            auto* c = vs.getSelectedClip(); if (c == nullptr || c->itemID != id) return;
            auto* ac = dynamic_cast<te::WaveAudioClip*> (c);
            if (r == 1) owner.splitSelectedAtPlayhead();
            else if (r == 2) owner.duplicateSelectedClip();
            else if (r == 3) owner.deleteSelectedClip();
            else if (r == 4) c->setMuted (! c->isMuted());
            else if (r == 5)
            {
                auto* w = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", c->getName()); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int res) { if (res == 1) if (auto* cc = vs.getSelectedClip(); cc && cc->itemID == id) cc->setName (w->getTextEditorContents ("name")); owner.repaint(); }), true);
            }
            else if (ac != nullptr && r == 10) DawSession::setClipWarp (*ac, ! ac->getAutoTempo(), ac->getPitchChange());
            else if (ac != nullptr && r >= 100 && r <= 124) DawSession::setClipWarp (*ac, ac->getAutoTempo(), (float) (r - 112));
            else if (ac != nullptr && r == 11) ac->setIsReversed (! ac->getIsReversed());
            else if (ac != nullptr && r == 12) { if (ac->isLooping()) ac->disableLooping(); else ac->setLoopRangeBeats ({ te::BeatPosition(), ac->getLengthInBeats() }); }
            else if (ac != nullptr && r >= 188 && r <= 224) ac->setGainDB ((float) (r - 212));
            else if (ac != nullptr && r == 13) owner.vs.session.sliceAtTransients (*ac);
            else if (r == 20 && vs.onOpenPianoRoll) vs.onOpenPianoRoll();
            else if (r == 21) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) quantise (*mc);
            owner.repaint();
        });
    }
    void quantise (te::MidiClip& mc)
    {
        const int div = std::max (1, vs.snapDiv);
        auto* um = &vs.edit().getUndoManager(); um->beginNewTransaction ("Quantise");
        for (auto* n : mc.getSequence().getNotes())
        {
            const double s = std::round (n->getStartBeat().inBeats() * div) / div;
            n->setStartAndLength (te::BeatPosition::fromBeats (s), n->getLengthBeats(), um);
        }
    }
    int dropY = -1;
private:
    ArrangeView& owner; ViewState& vs;
    std::map<juce::String, std::unique_ptr<te::SmartThumbnail>> thumbs;
    Drag drag = Drag::none; bool dragged = false;
    te::TimePosition origStart, origEnd; te::TimeDuration origOffset, origFadeIn, origFadeOut; double grabBeat = 0;
};

//==============================================================================
class ArrangeView::Ruler : public juce::Component
{
public:
    explicit Ruler (ArrangeView& o) : owner (o), vs (o.vs) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1b1b));
        auto& tr = vs.edit().getTransport();
        // loop range
        const auto lr = tr.getLoopRange();
        const float lx0 = vs.beatToX (vs.timeToBeats (lr.getStart()), 0), lx1 = vs.beatToX (vs.timeToBeats (lr.getEnd()), 0);
        g.setColour ((tr.looping ? VegaColours::accent : VegaColours::textDim).withAlpha (tr.looping ? 0.35f : 0.15f));
        g.fillRect (juce::Rectangle<float> (lx0, 0, lx1 - lx0, 9.0f));
        const int num = vs.edit().tempoSequence.getTimeSig (0)->numerator;
        const double first = std::floor (vs.scrollBeats), last = vs.xToBeat ((float) getWidth(), 0);
        g.setFont (VegaFonts::ui (10.5f, true));
        for (double b = first; b <= last; b += 1.0)
        {
            const float x = vs.beatToX (b, 0);
            const bool bar = ((int) b % num) == 0;
            if (! bar && vs.pixelsPerBeat < 10) continue;
            g.setColour (bar ? VegaColours::text : VegaColours::textDim.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, bar ? 12.0f : 21.0f, (float) getHeight());
            if (bar && (vs.pixelsPerBeat * num > 28 || ((int) b / num) % 4 == 0))
                g.drawText (juce::String ((int) b / num + 1), (int) x + 3, 10, 40, 14, juce::Justification::left);
        }
        const float px = vs.beatToX (vs.timeToBeats (tr.getPosition()), 0);
        juce::Path tri; tri.addTriangle (px - 6, 18, px + 6, 18, px, (float) getHeight());
        g.setColour (VegaColours::gold); g.fillPath (tri);
        g.setColour (VegaColours::line); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        loopDrag = e.position.y < 10 || e.mods.isShiftDown();
        startBeat = vs.snapBeat (vs.xToBeat (e.position.x, 0));
        if (! loopDrag) setPos (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! loopDrag) { setPos (e); return; }
        const double b = vs.snapBeat (vs.xToBeat (e.position.x, 0));
        if (std::abs (b - startBeat) < 1e-6) return;
        auto& tr = vs.edit().getTransport();
        tr.setLoopRange ({ vs.beatsToTime (std::max (0.0, std::min (b, startBeat))), vs.beatsToTime (std::max (b, startBeat)) });
        tr.looping = true;
        repaint(); owner.lanes->repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.position.y < 10) { auto& tr = vs.edit().getTransport(); tr.looping = ! tr.looping.get(); repaint(); }
    }
private:
    void setPos (const juce::MouseEvent& e)
    {
        vs.edit().getTransport().setPosition (vs.beatsToTime (std::max (0.0, vs.snapBeat (vs.xToBeat (e.position.x, 0), e.mods.isShiftDown()))));
        repaint(); owner.lanes->repaint();
    }
    ArrangeView& owner; ViewState& vs; bool loopDrag = false; double startBeat = 0;
};

//==============================================================================
ArrangeView::ArrangeView (ViewState& v) : vs (v)
{
    ruler = std::make_unique<Ruler> (*this); lanes = std::make_unique<Lanes> (*this);
    addAndMakeVisible (*ruler); addAndMakeVisible (*lanes);
    addAndMakeVisible (hbar); addAndMakeVisible (vbar);
    hbar.setAutoHide (false); vbar.setAutoHide (false);
    hbar.addListener (this); vbar.addListener (this);
    setWantsKeyboardFocus (true);
    editChanged();
    startTimerHz (30);
}
ArrangeView::~ArrangeView() { watched.removeListener (this); }
void ArrangeView::scrollBarMoved (juce::ScrollBar* bar, double s)
{
    if (bar == &hbar) { if (std::abs (s - vs.scrollBeats) > 1e-6) { vs.scrollBeats = s; vs.followPlayhead = false; repaint(); } }
    else if ((int) s != vs.scrollY) { vs.scrollY = (int) s; resized(); }
}

void ArrangeView::editChanged()
{
    watched.removeListener (this);
    watched = vs.edit().state;
    watched.addListener (this);
    vs.selectedClip = {}; vs.selectedTrack = {};
    rebuildHeaders();
}

void ArrangeView::rebuildHeaders()
{
    headers.clear();
    for (auto* t : vs.session.getTracks())
    {
        auto* h = headers.add (new TrackHeader (*this, *t));
        addAndMakeVisible (h);
    }
    resized(); repaint();
}

int ArrangeView::totalTracksHeight() { int h = 0; for (auto* t : vs.session.getTracks()) h += trackHeight (*t); return h; }
te::AudioTrack* ArrangeView::trackAtY (int y, int* rowTop)
{
    int top = -vs.scrollY;
    for (auto* t : vs.session.getTracks())
    {
        const int h = trackHeight (*t);
        if (y >= top && y < top + h) { if (rowTop) *rowTop = top; return t; }
        top += h;
    }
    return nullptr;
}
int ArrangeView::trackTop (te::AudioTrack& tr) { int top = -vs.scrollY; for (auto* t : vs.session.getTracks()) { if (t == &tr) return top; top += trackHeight (*t); } return top; }

void ArrangeView::resized()
{
    const int sb = 12;
    ruler->setBounds (headerW, 0, getWidth() - headerW - sb, rulerH);
    lanes->setBounds (headerW, rulerH, getWidth() - headerW - sb, getHeight() - rulerH - sb);
    hbar.setBounds (headerW, getHeight() - sb, getWidth() - headerW - sb, sb);
    vbar.setBounds (getWidth() - sb, rulerH, sb, getHeight() - rulerH - sb);
    const int total = totalTracksHeight() + 120;
    vs.scrollY = juce::jlimit (0, std::max (0, total - lanes->getHeight()), vs.scrollY);
    vbar.setRangeLimits (0, std::max (total, lanes->getHeight())); vbar.setCurrentRange (vs.scrollY, lanes->getHeight(), juce::dontSendNotification);
    int y = rulerH - vs.scrollY;
    for (auto* h : headers)
    {
        const int th = h->track != nullptr ? trackHeight (*h->track) : 80;
        h->setBounds (0, y, headerW, th);
        h->setVisible (y + th > rulerH && y < getHeight());
        y += th;
    }
    lanes->toFront (false); ruler->toFront (false);
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1d1d1d));
    g.setColour (juce::Colour (0xff1b1b1b)); g.fillRect (0, 0, headerW, rulerH);
    g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (10.0f, true));
    g.drawText ("TRACKS", 10, 0, 100, rulerH, juce::Justification::centredLeft);
    g.setColour (VegaColours::line); g.drawVerticalLine (headerW - 1, 0, (float) getHeight());
}

void ArrangeView::timerCallback()
{
    if (needsRebuild) { needsRebuild = false; rebuildHeaders(); }
    auto& tr = vs.edit().getTransport();
    if (tr.isPlaying() && vs.followPlayhead)
    {
        const double pb = vs.timeToBeats (tr.getPosition()), visible = lanes->getWidth() / vs.pixelsPerBeat;
        if (pb > vs.scrollBeats + visible * 0.92 || pb < vs.scrollBeats) vs.scrollBeats = std::max (0.0, pb - visible * 0.1);
    }
    const double totalBeats = std::max (vs.timeToBeats (vs.session.contentRange().getEnd()) + 64.0, vs.scrollBeats + lanes->getWidth() / vs.pixelsPerBeat);
    hbar.setRangeLimits (0, totalBeats); hbar.setCurrentRange (vs.scrollBeats, lanes->getWidth() / vs.pixelsPerBeat, juce::dontSendNotification);
    if (++tick % 10 == 0 || needsRepaint) { for (auto* h : headers) h->refresh(); needsRepaint = false; }
    ruler->repaint(); lanes->repaint();
}

void ArrangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {   // zoom around the mouse
        const float x = (float) (e.getEventRelativeTo (lanes.get()).position.x);
        const double anchor = vs.xToBeat (x, 0);
        vs.pixelsPerBeat = juce::jlimit (2.0, 400.0, vs.pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
        vs.scrollBeats = std::max (0.0, anchor - x / vs.pixelsPerBeat);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY;
        vs.scrollBeats = std::max (0.0, vs.scrollBeats - d * 300.0 / vs.pixelsPerBeat);
        vs.followPlayhead = false;
    }
    else { vs.scrollY = std::max (0, vs.scrollY - (int) (w.deltaY * 240)); }
    resized(); repaint();
}

bool ArrangeView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }
    if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { duplicateSelectedClip(); return true; }
    if (k == juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0)) { splitSelectedAtPlayhead(); return true; }
    return false;
}

void ArrangeView::deleteSelectedClip()
{
    if (auto* c = vs.getSelectedClip()) { vs.edit().getUndoManager().beginNewTransaction ("Delete clip"); c->removeFromParent(); vs.selectClip (nullptr); repaint(); }
}
void ArrangeView::duplicateSelectedClip()
{
    auto* c = vs.getSelectedClip(); if (c == nullptr) return;
    auto* track = dynamic_cast<te::ClipTrack*> (c->getTrack()); if (track == nullptr) return;
    vs.edit().getUndoManager().beginNewTransaction ("Duplicate clip");
    auto state = c->state.createCopy();
    te::EditItemID::remapIDs (state, nullptr, vs.edit());
    if (auto* nc = track->insertClipWithState (state))
    {
        const auto len = c->getPosition().getLength();
        nc->setStart (c->getPosition().getEnd(), false, true);
        nc->setLength (len, false);
        vs.selectClip (nc);
    }
    repaint();
}
void ArrangeView::splitSelectedAtPlayhead()
{
    if (auto* c = vs.getSelectedClip()) { vs.edit().getUndoManager().beginNewTransaction ("Split"); vs.session.splitClip (*c, vs.edit().getTransport().getPosition()); repaint(); }
}

void ArrangeView::filesDropped (const juce::StringArray& files, int x, int y)
{
    lanes->dropY = -1;
    auto p = lanes->getLocalPoint (this, juce::Point<int> (x, y));
    const double beat = std::max (0.0, vs.snapBeat (vs.xToBeat ((float) p.x, 0)));
    auto* target = trackAtY (p.y);
    for (auto& path : files)
    {
        const juce::File f (path);
        const bool midi = f.hasFileExtension ("mid;midi");
        if (target == nullptr || (DawSession::kindOf (*target) == DawSession::TrackKind::midi) != midi || DawSession::kindOf (*target) == DawSession::TrackKind::bus)
            target = vs.session.addTrack (midi ? DawSession::TrackKind::midi : DawSession::TrackKind::audio, f.getFileNameWithoutExtension());
        if (target == nullptr) continue;
        if (midi) vs.session.importMidiFile (*target, f, vs.beatsToTime (beat));
        else vs.session.insertAudioFile (*target, f, vs.beatsToTime (beat));
        target = nullptr;
    }
    rebuildHeaders();
}
