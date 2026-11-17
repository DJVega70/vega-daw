#include "PianoRoll.h"

PianoRoll::PianoRoll (ViewState& v) : vs (v)
{
    gridBox.addItemList ({ "Grid 1/4", "Grid 1/8", "Grid 1/16", "Grid 1/32", "Grid 1/8T", "Grid 1/16T", "Grid Off" }, 1);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    addAndMakeVisible (gridBox);
    quantBtn.setButtonText ("QUANTISE"); clearBtn.setButtonText ("CLEAR"); closeBtn.setButtonText ("CLOSE");
    quantBtn.onClick = [this]
    {
        if (auto* c = clip())
        {
            auto* um = &vs.edit().getUndoManager(); um->beginNewTransaction ("Quantise");
            for (auto* n : c->getSequence().getNotes()) n->setStartAndLength (te::BeatPosition::fromBeats (std::max (0.0, snap (n->getStartBeat().inBeats(), false))), n->getLengthBeats(), um);
        }
    };
    clearBtn.onClick = [this] { if (auto* c = clip()) { vs.edit().getUndoManager().beginNewTransaction ("Clear notes"); c->getSequence().clear (&vs.edit().getUndoManager()); } };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &quantBtn, &clearBtn, &closeBtn }) addAndMakeVisible (b);
    title.setFont (VegaFonts::ui (12.0f, true)); title.setColour (juce::Label::textColourId, VegaColours::accent);
    addAndMakeVisible (title);
    setWantsKeyboardFocus (true);
    startTimerHz (25);
}

te::MidiClip* PianoRoll::clip() { return dynamic_cast<te::MidiClip*> (vs.getSelectedClip()); }

double PianoRoll::snap (double b, bool bypass) const
{
    static const double div[] { 1, 2, 4, 8, 3, 6, 0 };
    const double d = div[juce::jlimit (0, 6, gridBox.getSelectedItemIndex())];
    if (d <= 0 || bypass) return b;
    return std::round (b * d) / d;
}

void PianoRoll::resized()
{
    title.setBounds (keysW + 6, 4, 360, 24);
    closeBtn.setBounds (getWidth() - 76, 5, 70, 22); clearBtn.setBounds (getWidth() - 152, 5, 70, 22);
    quantBtn.setBounds (getWidth() - 248, 5, 90, 22); gridBox.setBounds (getWidth() - 360, 5, 106, 22);
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff161616));
    g.setColour (juce::Colour (0xff1e1e1e)); g.fillRect (0, 0, getWidth(), toolbarH);
    auto* c = clip();
    if (c == nullptr)
    {
        title.setText ("PIANO ROLL", juce::dontSendNotification);
        g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (13.0f));
        g.drawText ("Double-click a MIDI clip (or an empty spot on an instrument track) to edit notes", getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (c->itemID != lastClip)
    {   // new clip: frame it and centre on its notes
        lastClip = c->itemID; scroll = c->getOffsetInBeats().inBeats();
        const double len = c->getLengthInBeats().inBeats();
        ppb = juce::jlimit (12.0, 400.0, (getWidth() - keysW - 20) / std::max (1.0, len));
        int lo = 127, hi = 0; for (auto* n : c->getSequence().getNotes()) { lo = std::min (lo, n->getNoteNumber()); hi = std::max (hi, n->getNoteNumber()); }
        const int visibleRows = (int) ((getHeight() - toolbarH - velH) / rowH);
        topNote = hi >= lo ? juce::jlimit (visibleRows, 127, (lo + hi) / 2 + visibleRows / 2) : 84;
    }
    title.setText ("PIANO ROLL  -  " + c->getName() + "  (" + (c->getTrack() ? c->getTrack()->getName() : juce::String()) + ")", juce::dontSendNotification);
    const auto grid = gridArea();
    const auto col = dawui::clipColour (*c);
    g.saveState(); g.reduceClipRegion (grid.withX (0).withWidth (getWidth()));
    // rows + keys
    for (int n = 127; n >= 0; --n)
    {
        const float y = noteToY (n);
        if (y > grid.getBottom() || y + rowH < grid.getY()) continue;
        const bool black = juce::MidiMessage::isMidiNoteBlack (n);
        g.setColour (juce::Colour (black ? 0xff191919 : 0xff1f1f1f)); g.fillRect ((float) keysW, y, (float) grid.getWidth(), rowH);
        g.setColour (juce::Colour (0xff141414)); g.drawHorizontalLine ((int) (y + rowH), (float) keysW, (float) getWidth());
        g.setColour (black ? juce::Colour (0xff202020) : juce::Colour (0xffd8d8d8)); g.fillRect (0.0f, y, (float) keysW - 2, rowH - 1);
        if (n % 12 == 0) { g.setColour (juce::Colours::black); g.setFont (VegaFonts::ui (9.0f, true)); g.drawText (juce::MidiMessage::getMidiNoteName (n, true, true, 3), 2, (int) y, keysW - 6, (int) rowH, juce::Justification::centredRight); }
    }
    // beat lines + clip range
    const double clipStart = c->getOffsetInBeats().inBeats(), clipEnd = clipStart + c->getLengthInBeats().inBeats();
    const int num = vs.edit().tempoSequence.getTimeSig (0)->numerator;
    for (double b = std::floor (scroll * 4) / 4; b < xToBeat ((float) getWidth()); b += 0.25)
    {
        const bool beat = std::abs (b - std::round (b)) < 1e-6, bar = beat && ((int) std::round (b) % num == 0);
        if (! beat && ppb < 40) continue;
        g.setColour (juce::Colours::white.withAlpha (bar ? 0.14f : beat ? 0.07f : 0.03f));
        g.drawVerticalLine ((int) beatToX (b), (float) grid.getY(), (float) grid.getBottom());
    }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> ((float) keysW, (float) grid.getY(), std::max (0.0f, beatToX (clipStart) - keysW), (float) grid.getHeight()));
    g.fillRect (juce::Rectangle<float> (beatToX (clipEnd), (float) grid.getY(), std::max (0.0f, (float) getWidth() - beatToX (clipEnd)), (float) grid.getHeight()));
    // notes
    for (auto* n : c->getSequence().getNotes())
    {
        auto r = juce::Rectangle<float> (beatToX (n->getStartBeat().inBeats()), noteToY (n->getNoteNumber()) + 1, (float) (n->getLengthBeats().inBeats() * ppb) - 1, rowH - 2);
        g.setColour (col.withAlpha (0.45f + 0.55f * n->getVelocity() / 127.0f)); g.fillRoundedRectangle (r, 2.0f);
        g.setColour (n == dragNote ? juce::Colours::white : col.brighter (0.3f)); g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }
    // playhead (if inside this clip)
    auto& tr = vs.edit().getTransport();
    const double ph = vs.timeToBeats (tr.getPosition()) - vs.timeToBeats (c->getPosition().getStart()) + clipStart;
    if (ph >= clipStart && ph <= clipEnd) { g.setColour (VegaColours::gold); g.fillRect (beatToX (ph), (float) grid.getY(), 1.5f, (float) grid.getHeight()); }
    g.restoreState();
    // velocity lane
    auto va = velArea();
    g.setColour (juce::Colour (0xff121212)); g.fillRect (va);
    g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (9.5f, true)); g.drawText ("VELOCITY", 4, va.getY(), keysW - 8, 14, juce::Justification::left);
    for (auto* n : c->getSequence().getNotes())
    {
        const float x = beatToX (n->getStartBeat().inBeats()), h = (va.getHeight() - 8) * n->getVelocity() / 127.0f;
        if (x < keysW) continue;
        g.setColour (col); g.fillRect (x, (float) va.getBottom() - h - 2, 3.0f, h); g.fillEllipse (x - 2, (float) va.getBottom() - h - 5, 7, 7);
    }
    g.setColour (VegaColours::line); g.drawHorizontalLine (va.getY(), 0, (float) getWidth()); g.drawHorizontalLine (0, 0, (float) getWidth());
}

te::MidiNote* PianoRoll::noteAt (juce::Point<float> p, bool& edge)
{
    auto* c = clip(); if (c == nullptr) return nullptr;
    for (auto* n : c->getSequence().getNotes())
    {
        auto r = juce::Rectangle<float> (beatToX (n->getStartBeat().inBeats()), noteToY (n->getNoteNumber()), (float) (n->getLengthBeats().inBeats() * ppb), rowH);
        if (r.contains (p)) { edge = p.x > r.getRight() - std::min (8.0f, r.getWidth() * 0.3f); return n; }
    }
    return nullptr;
}

void PianoRoll::audition (int note, int vel)
{
    if (auto* c = clip())
        if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
        {
            t->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), {});
            juce::Timer::callAfterDelay (220, [tp = te::AudioTrack::Ptr (t), note] { tp->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), {}); });
        }
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false; auto* n = noteAt (e.position, edge);
    setMouseCursor (n == nullptr ? juce::MouseCursor::NormalCursor : edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto* c = clip(); if (c == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    mode = Mode::none; dragNote = nullptr;
    if (velArea().contains (e.getPosition())) { mode = Mode::velocity; um->beginNewTransaction ("Velocity"); mouseDrag (e); return; }
    if (! gridArea().contains (e.getPosition()))
    {
        if (e.x < keysW && e.y > toolbarH) audition (yToNote ((float) e.y), 100);
        return;
    }
    bool edge = false;
    if (auto* n = noteAt (e.position, edge))
    {
        if (e.mods.isPopupMenu()) { um->beginNewTransaction ("Delete note"); c->getSequence().removeNote (*n, um); return; }
        um->beginNewTransaction (edge ? "Resize note" : "Move note");
        dragNote = n; mode = edge ? Mode::resize : Mode::move;
        origStart = n->getStartBeat().inBeats(); origLen = n->getLengthBeats().inBeats(); origNote = n->getNoteNumber();
        grabBeat = xToBeat (e.position.x); grabNote = yToNote (e.position.y);
        lastLen = origLen;
        if (! edge) audition (origNote, n->getVelocity());
        return;
    }
    if (e.mods.isPopupMenu()) return;
    // create a note
    um->beginNewTransaction ("Add note");
    static const double divs[] { 1, 2, 4, 8, 3, 6, 0 };
    const double d = divs[juce::jlimit (0, 6, gridBox.getSelectedItemIndex())];
    const double clicked = xToBeat (e.position.x);
    const double st = (d > 0 && ! e.mods.isShiftDown()) ? std::floor (clicked * d) / d : clicked;
    const int note = juce::jlimit (0, 127, yToNote (e.position.y));
    dragNote = c->getSequence().addNote (note, te::BeatPosition::fromBeats (std::max (0.0, st)), te::BeatDuration::fromBeats (lastLen), 100, 0, um);
    if (dragNote) { mode = Mode::resize; origStart = dragNote->getStartBeat().inBeats(); origLen = lastLen; grabBeat = xToBeat (e.position.x); audition (note, 100); }
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    auto* c = clip(); if (c == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    const bool free = e.mods.isShiftDown();
    if (mode == Mode::velocity)
    {
        const auto va = velArea();
        for (auto* n : c->getSequence().getNotes())
            if (std::abs (beatToX (n->getStartBeat().inBeats()) - e.position.x) < 5)
                n->setVelocity (juce::jlimit (1, 127, (int) ((va.getBottom() - e.position.y) / (va.getHeight() - 8) * 127)), um);
        return;
    }
    if (dragNote == nullptr) return;
    if (mode == Mode::move)
    {
        const double ns = std::max (0.0, snap (origStart + xToBeat (e.position.x) - grabBeat, free));
        const int nn = juce::jlimit (0, 127, origNote + yToNote (e.position.y) - grabNote);
        dragNote->setStartAndLength (te::BeatPosition::fromBeats (ns), te::BeatDuration::fromBeats (origLen), um);
        if (nn != dragNote->getNoteNumber()) { dragNote->setNoteNumber (nn, um); audition (nn, dragNote->getVelocity()); }
    }
    else if (mode == Mode::resize)
    {
        const double end = snap (xToBeat (e.position.x), free);
        const double len = std::max (1.0 / 32, end - origStart);
        dragNote->setStartAndLength (te::BeatPosition::fromBeats (origStart), te::BeatDuration::fromBeats (len), um);
        lastLen = len;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent&) { mode = Mode::none; dragNote = nullptr; }

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge = false;
    if (auto* n = noteAt (e.position, edge)) if (auto* c = clip()) { vs.edit().getUndoManager().beginNewTransaction ("Delete note"); c->getSequence().removeNote (*n, &vs.edit().getUndoManager()); }
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = xToBeat (e.position.x);
        ppb = juce::jlimit (8.0, 800.0, ppb * (w.deltaY > 0 ? 1.15 : 1 / 1.15));
        scroll = anchor - (e.position.x - keysW) / ppb;
    }
    else if (e.mods.isShiftDown()) scroll -= w.deltaY * 200 / ppb;
    else topNote = juce::jlimit (20, 127, topNote + (w.deltaY > 0 ? 3 : -3));
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
    return false;
}
