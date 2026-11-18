#include "MixerView.h"

//==============================================================================
// Hardware-style parts
namespace xl
{
const juce::Colour face { 0xff1d1d1f }, faceLight { 0xff242427 }, keyFace { 0xff202023 }, keyEdge { 0xff38383c }, slot { 0xff0a0a0b };
const juce::Colour rowLed[4] { juce::Colour (0xffe35ce8), juce::Colour (0xff1ea9ee), juce::Colour (0xff17dca2), juce::Colour (0xffffb23a) };
// pad LED colours run pink -> violet -> blue -> cyan -> green across the channels, like the hardware
inline juce::Colour columnColour (int i)
{
    static const juce::uint32 c[] { 0xffe35ce8, 0xffb183f2, 0xff7fa2f7, 0xff67cdf7, 0xff52dcef, 0xff31e3e6, 0xff2fe2d2, 0xff22dfa8 };
    return juce::Colour (c[((i % 8) + 8) % 8]);
}

inline void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float lit)
{
    if (lit > 0.5f)
    {
        g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (r.expanded (5.0f, 4.0f), 4.0f);
        g.setColour (c.withAlpha (0.35f)); g.fillRoundedRectangle (r.expanded (2.0f, 1.5f), 2.5f);
    }
    g.setColour (lit > 0.5f ? c : c.withMultipliedBrightness (0.35f).withAlpha (0.55f));
    g.fillRoundedRectangle (r, 1.0f);
    if (lit > 0.5f) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillRect (r.withHeight (1.0f).reduced (r.getWidth() * 0.15f, 0)); }
}

inline void drawKeyBox (juce::Graphics& g, juce::Rectangle<float> r, bool down = false)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.translated (0, 2.0f), 5.0f);
    g.setGradientFill (juce::ColourGradient (down ? juce::Colour (0xff1a1a1c) : juce::Colour (0xff27272a), 0, r.getY(), juce::Colour (0xff1b1b1d), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (keyEdge); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

/** Rotary knob: soft-touch dark cap with a white pointer */
class Knob : public juce::Slider
{
public:
    // drawn by VegaLookAndFeel::drawRotarySlider - the same machined knob and tick ring as the plug-ins
    Knob() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setMouseDragSensitivity (200);
        setPopupDisplayEnabled (true, true, nullptr);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
};

/** Long-throw fader: black slot, chunky cap with a white line */
class Fader : public juce::Slider
{
public:
    Fader() : juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox)
    {
        setSliderSnapsToMousePosition (false);
        setPopupDisplayEnabled (true, true, nullptr);
    }
    static constexpr float capW = 52.0f, capH = 26.0f;
    void paint (juce::Graphics& g) override
    {
        const float cx = getWidth() * 0.5f;
        auto slotR = juce::Rectangle<float> (cx - 5.0f, 6.0f, 10.0f, getHeight() - 12.0f);
        g.setColour (slot); g.fillRoundedRectangle (slotR, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f)); g.drawRoundedRectangle (slotR.expanded (1.0f), 5.5f, 1.0f);
        const float y = (float) getPositionOfValue (getValue());
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, y });
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (cap.translated (0, 5.0f).expanded (1.0f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff47474b), 0, cap.getY(), juce::Colour (0xff232326), 0, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f)); g.drawHorizontalLine ((int) cap.getY() + 1, cap.getX() + 4, cap.getRight() - 4);
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawRoundedRectangle (cap, 4.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRect (cap.getX() + 3, y + 0.5f, cap.getWidth() - 6, 2.5f);
        g.setColour (juce::Colour (0xffe6e6e6)); g.fillRect (cap.getX() + 3, y - 1.5f, cap.getWidth() - 6, 2.5f);
    }
};

/** Compact horizontal pan control: slot with a centre notch and a small cap */
class PanBar : public juce::Slider
{
public:
    PanBar() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setRange (-1.0, 1.0, 0.01); setDoubleClickReturnValue (true, 0.0);
        setSliderSnapsToMousePosition (false); setPopupDisplayEnabled (true, true, nullptr);
        textFromValueFunction = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("Pan C") : "Pan " + juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? " L" : " R"); };
    }
    void paint (juce::Graphics& g) override
    {
        const float cy = getHeight() * 0.5f;
        auto slotR = juce::Rectangle<float> (8.0f, cy - 2.0f, getWidth() - 16.0f, 4.0f);
        g.setColour (slot); g.fillRoundedRectangle (slotR, 2.0f);
        g.setColour (juce::Colour (0xff8a8a8a)); g.fillRect (getWidth() * 0.5f - 0.5f, cy - 6.0f, 1.0f, 12.0f);
        const float x = (float) getPositionOfValue (getValue());
        const float cx = getWidth() * 0.5f;
        g.setColour (rowLed[2].withAlpha (0.7f)); g.fillRect (juce::Rectangle<float> (std::min (x, cx), cy - 1.0f, std::abs (x - cx), 2.0f));
        auto cap = juce::Rectangle<float> (12.0f, 14.0f).withCentre ({ x, cy });
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4e), 0, cap.getY(), juce::Colour (0xff252528), 0, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (juce::Colour (0xffe6e6e6)); g.fillRect (x - 0.75f, cap.getY() + 3, 1.5f, cap.getHeight() - 6);
    }
};

/** Pad with an LED strip: dim when off, glowing when on */
class Pad : public juce::Button
{
public:
    Pad() : juce::Button ({}) {}
    juce::Colour led { 0xff48e1e8 };
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (down ? juce::Colour (0xff141416) : over ? juce::Colour (0xff1d1d20) : juce::Colour (0xff18181a));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff2c2c30)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        drawLed (g, juce::Rectangle<float> (r.getWidth() * 0.42f, 4.0f).withCentre (r.getCentre()), led, getToggleState() ? 1.0f : 0.0f);
    }
};

/** Small hardware key with a drawn symbol */
class Key : public juce::Button
{
public:
    enum Shape { up, down, left, right, rec, play, text };
    Key (Shape s, juce::String t = {}) : juce::Button (t), shape (s) {}
    Shape shape; juce::Colour on { 0xffffffff };
    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
    {
        auto r = getLocalBounds().toFloat();
        if (isDown) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r.reduced (2.0f), 3.0f); }
        const auto c = r.getCentre(); const float s = std::min (r.getWidth(), r.getHeight()) * 0.22f;
        const auto ink = juce::Colour (0xffe8e8e8).withAlpha (isOver ? 1.0f : 0.85f);
        juce::Path p;
        switch (shape)
        {
            case up:    p.startNewSubPath (c.x - s, c.y + s * 0.5f); p.lineTo (c.x, c.y - s * 0.5f); p.lineTo (c.x + s, c.y + s * 0.5f); break;
            case down:  p.startNewSubPath (c.x - s, c.y - s * 0.5f); p.lineTo (c.x, c.y + s * 0.5f); p.lineTo (c.x + s, c.y - s * 0.5f); break;
            case left:  p.startNewSubPath (c.x + s * 0.5f, c.y - s); p.lineTo (c.x - s * 0.5f, c.y); p.lineTo (c.x + s * 0.5f, c.y + s); break;
            case right: p.startNewSubPath (c.x - s * 0.5f, c.y - s); p.lineTo (c.x + s * 0.5f, c.y); p.lineTo (c.x - s * 0.5f, c.y + s); break;
            case rec:
            {
                const auto col = juce::Colour (0xffff3355);
                if (getToggleState()) { g.setColour (col.withAlpha (0.3f)); g.fillEllipse (juce::Rectangle<float> (s * 3.4f, s * 3.4f).withCentre (c)); }
                g.setColour (getToggleState() ? col : col.withMultipliedBrightness (0.75f)); g.fillEllipse (juce::Rectangle<float> (s * 2.3f, s * 2.3f).withCentre (c));
                return;
            }
            case play:
            {
                const auto col = juce::Colour (0xff22dd66);
                juce::Path t; t.addTriangle (c.x - s * 0.9f, c.y - s * 1.1f, c.x - s * 0.9f, c.y + s * 1.1f, c.x + s * 1.1f, c.y);
                if (getToggleState()) { g.setColour (col.withAlpha (0.3f)); g.strokePath (t, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved)); }
                g.setColour (getToggleState() ? col : col.withMultipliedBrightness (0.8f)); g.fillPath (t);
                return;
            }
            case text:
                g.setColour (getToggleState() ? on : ink); g.setFont (VegaFonts::ui (12.5f, getToggleState()));
                g.drawText (getButtonText(), r, juce::Justification::centred);
                return;
        }
        g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};
} // namespace xl

#include "MasterSection.h"

//==============================================================================
namespace mixlayout
{
constexpr int padH = 36, padGap = 10, bottom = 8, knobRowH = 68, headerH = 30, numKnobs = 4;
inline int padY (int h, int row) { return h - bottom - padH - (1 - row) * (padH + padGap); }   // row 0 = Solo/Arm, row 1 = Mute/Select
}

//==============================================================================
class MixerView::Strip : public juce::Component
{
public:
    static constexpr int W = 124;
    te::AudioTrack* getTrack() const { return track.get(); }
    Strip (MixerView& o, ViewState& v, te::AudioTrack* t, int column) : owner (o), vs (v), track (t), col (column)
    {
        fader.setRange (-60.0, 6.0, 0.1); fader.setSkewFactorFromMidPoint (-10.0);
        fader.setTextValueSuffix (" dB"); fader.setDoubleClickReturnValue (true, 0.0);
        fader.onValueChange = [this] { if (auto vp = volume()) vp->setVolumeDb ((float) fader.getValue()); touch ("Volume", juce::String (fader.getValue(), 1) + " dB"); };
        // gestures let automation WRITE mode record these moves
        fader.onDragStart = [this] { if (auto vp = volume()) dawui::gesture (vp->volParam.get(), true); };
        fader.onDragEnd   = [this] { if (auto vp = volume()) dawui::gesture (vp->volParam.get(), false); };
        pan.onDragStart = [this] { if (auto vp = volume()) dawui::gesture (vp->panParam.get(), true); };
        pan.onDragEnd   = [this] { if (auto vp = volume()) dawui::gesture (vp->panParam.get(), false); };
        for (int i = 0; i < 4; ++i)
        {
            knobs[i].onDragStart = [this, i] { if (auto* d = dj()) dawui::gesture (i < 3 ? d->band[i].get() : d->filter.get(), true); };
            knobs[i].onDragEnd   = [this, i] { if (auto* d = dj()) dawui::gesture (i < 3 ? d->band[i].get() : d->filter.get(), false); };
        }
        pan.onValueChange = [this] { if (auto vp = volume()) vp->setPan ((float) pan.getValue()); touch ("Pan", pan.getTextFromValue (pan.getValue())); };
        // HI / MID / LOW isolator knobs: 12 o'clock = 0 dB, fully left = kill
        static const char* bandName[] { "Hi", "Mid", "Low" };
        for (int i = 0; i < 3; ++i)
        {
            knobs[i].setRange (VegaDJEQPlugin::killDb, VegaDJEQPlugin::maxDb, 0.1); knobs[i].setSkewFactorFromMidPoint (0.0);
            knobs[i].setDoubleClickReturnValue (true, 0.0);
            knobs[i].textFromValueFunction = [] (double v) { return v <= VegaDJEQPlugin::killDb + 0.05 ? juce::String ("KILL") : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
            knobs[i].onValueChange = [this, i]
            {
                if (auto* d = dj()) d->band[i]->setParameter ((float) knobs[i].getValue(), juce::sendNotification);
                touch (bandName[i], knobs[i].getTextFromValue (knobs[i].getValue()));
                repaint();
            };
        }
        // DJ filter: left = low-pass, right = high-pass, centre = off
        knobs[3].setRange (-1.0, 1.0, 0.001); knobs[3].setDoubleClickReturnValue (true, 0.0);
        knobs[3].textFromValueFunction = [] (double v) { return VegaDJEQPlugin::describeFilter ((float) v); };
        knobs[3].onValueChange = [this]
        {
            if (auto* d = dj()) d->filter->setParameter ((float) knobs[3].getValue(), juce::sendNotification);
            touch ("Filter", knobs[3].getTextFromValue (knobs[3].getValue()));
            repaint();
        };
        knobs[0].setTooltip ("HI - highs above 2.5 kHz (turn fully left to kill)");
        knobs[1].setTooltip ("MID - 250 Hz to 2.5 kHz (turn fully left to kill)");
        knobs[2].setTooltip ("LOW - lows below 250 Hz (turn fully left to kill)");
        knobs[3].setTooltip ("FILTER - left: low-pass, right: high-pass, centre: off (double-click to reset)");
        addAndMakeVisible (pan);
        addAndMakeVisible (fader);
        for (auto& k : knobs) addAndMakeVisible (k);
        if (track != nullptr)
        {
            for (int i = 0; i < 2; ++i) { addAndMakeVisible (pads[i]); pads[i].led = xl::columnColour (col); }
            pads[0].onClick = [this] { topPad(); };
            pads[1].onClick = [this] { bottomPad(); };
            output.onChange = [this] { const int i = output.getSelectedItemIndex(); vs.session.routeToBus (*track, i <= 0 ? nullptr : outBuses[i - 1]); };
            addAndMakeVisible (output);
            if (DawSession::kindOf (*track) == DawSession::TrackKind::midi)
            {
                instrument.onClick = [this]
                {
                    auto* first = track->pluginList[0];
                    if (first != nullptr && first->isSynth() && ! juce::ModifierKeys::currentModifiers.isPopupMenu()) dawui::openPlugin (first);
                    else chooseInstrument();
                };
                instrument.setTooltip ("Click to open the instrument. Use the arrow to change it.");
                swapInst.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe")); swapInst.setTooltip ("Change instrument");
                swapInst.onClick = [this] { chooseInstrument(); };
                addAndMakeVisible (instrument); addAndMakeVisible (swapInst);
            }
            addFx.setButtonText ("+ FX"); addFx.onClick = [this] { chooseFx(); }; addAndMakeVisible (addFx);
            addSend.setButtonText ("+ SEND"); addSend.onClick = [this] { chooseSend(); }; addAndMakeVisible (addSend);
            for (auto* b : { &instrument, &swapInst, &addFx, &addSend }) styleButton (*b);
            if (auto* lm = track->getLevelMeterPlugin()) { lm->measurer.addClient (client); measurer = &lm->measurer; }
        }
        rebuildSlots();
    }
    ~Strip() override { if (measurer) measurer->removeClient (client); if (masterAdded && masterCtx) masterCtx->masterLevels.removeClient (client); }

    te::VolumeAndPanPlugin* volume() { return track != nullptr ? track->getVolumePlugin() : vs.edit().getMasterVolumePlugin().get(); }
    juce::String name() const { return track != nullptr ? track->getName() : juce::String ("Master"); }

    /** Height the edit section (instrument, inserts, extra sends, output) needs */
    int editNeeded() const
    {
        if (track == nullptr) return 0;
        int h = 6;
        if (instrument.isVisible()) h += 26;
        h += slots.size() * 22 + 24;          // inserts + "+ FX"
        h += sends.size() * 28 + 24;          // extra sends + "+ SEND"
        h += 28;                               // output
        return h;
    }
    int editH = 0;

    void rebuildSlots()
    {
        slots.clear(); sends.clear();
        if (track == nullptr) { updateKnobs(); resized(); return; }
        for (auto* p : vs.session.getInsertPlugins (*track))
        {
            if (p->isSynth() && DawSession::kindOf (*track) == DawSession::TrackKind::midi && track->pluginList.indexOf (p) == 0) continue;
            auto* b = slots.add (new juce::TextButton (p->getName()));
            styleButton (*b);
            b->setColour (juce::TextButton::buttonColourId, p->isEnabled() ? juce::Colour (0xff23292a) : juce::Colour (0xff1c1c1e));
            b->setColour (juce::TextButton::textColourOffId, p->isEnabled() ? VegaColours::text : VegaColours::textDim);
            b->onClick = [this, pp = te::Plugin::Ptr (p), b]
            {
                if (juce::ModifierKeys::currentModifiers.isPopupMenu()) slotMenu (pp, b); else dawui::openPlugin (pp.get());
            };
            addAndMakeVisible (b);
        }
        for (auto* bus : vs.session.getBuses())
            if (auto* s = vs.session.findSend (*track, *bus))
            {
                auto* row = sends.add (new SendRow (*s, bus->getName()));
                addAndMakeVisible (row->knob); addAndMakeVisible (row->label);
            }
        djPlugin = vs.session.djEq (*track);
        if (DawSession::kindOf (*track) == DawSession::TrackKind::midi)
        {
            auto* first = track->pluginList[0];
            instrument.setButtonText (first != nullptr && first->isSynth() ? first->getName() : "Choose instrument...");
        }
        outBuses.clear(); output.clear (juce::dontSendNotification);
        output.addItem ("Out: Master", 1);
        for (auto* b : vs.session.getBuses()) if (b != track) { outBuses.add (b); output.addItem ("Out: " + b->getName(), outBuses.size() + 1); }
        auto* cur = vs.session.getOutputBus (*track);
        output.setSelectedItemIndex (cur == nullptr ? 0 : outBuses.indexOf (cur) + 1, juce::dontSendNotification);
        updateKnobs();
        resized();
    }
    juce::String layoutKey()
    {
        juce::String k;
        if (track == nullptr) return "master";
        k << track->getName() << "|" << track->pluginList.size();
        for (auto* p : track->pluginList.getPlugins()) k << p->getName() << (int) p->isEnabled();
        if (auto* o = vs.session.getOutputBus (*track)) k << ">" << o->getName();
        return k;
    }
    void refresh()
    {
        if (auto vp = volume())
        {
            if (! fader.isMouseButtonDown()) fader.setValue (vp->getVolumeDb(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown()) pan.setValue (vp->getPan(), juce::dontSendNotification);
        }
        if (auto* d = dj())
        {
            bool changed = false;
            for (int i = 0; i < 3; ++i)
                if (! knobs[i].isMouseButtonDown() && std::abs (knobs[i].getValue() - d->band[i]->getCurrentValue()) > 0.05) { knobs[i].setValue (d->band[i]->getCurrentValue(), juce::dontSendNotification); changed = true; }
            if (! knobs[3].isMouseButtonDown() && std::abs (knobs[3].getValue() - d->filter->getCurrentValue()) > 0.001) { knobs[3].setValue (d->filter->getCurrentValue(), juce::dontSendNotification); changed = true; }
            if (changed) repaint();
        }
        for (auto* s : sends) if (! s->knob.isMouseButtonDown()) s->knob.setValue (s->send.getGainDb(), juce::dontSendNotification);
        if (track != nullptr)
        {
            const bool sh = owner.shift;
            pads[0].setToggleState (sh ? dawui::isArmed (*track) : track->isSolo (false), juce::dontSendNotification);
            pads[1].setToggleState (sh ? vs.selectedTrack == track->itemID : ! track->isMuted (false), juce::dontSendNotification);
            pads[0].led = sh ? juce::Colour (0xffff3355) : xl::columnColour (col);
            pads[0].setTooltip (sh ? "Record arm" : "Solo"); pads[1].setTooltip (sh ? "Select" : "Mute (lit = playing)");
            pads[0].repaint(); pads[1].repaint();
        }
        auto* m = measurer;
        if (track == nullptr) if (auto* ctx = vs.edit().getCurrentPlaybackContext()) { if (! masterAdded) { ctx->masterLevels.addClient (client); masterAdded = true; masterCtx = ctx; } m = &ctx->masterLevels; }
        if (m != nullptr)
            for (int c = 0; c < 2; ++c) { const float db = client.getAndClearAudioLevel (c).dB; level[c] = std::max (db, level[c] - 1.5f); }
        const bool sel = track != nullptr && vs.selectedTrack == track->itemID;
        if (sel != wasSelected) { wasSelected = sel; repaint(); }
        repaint (meterArea); repaint (valueArea);
    }
    void paint (juce::Graphics& g) override
    {
        using namespace mixlayout;
        auto r = getLocalBounds().toFloat();
        const bool sel = track != nullptr && vs.selectedTrack == track->itemID;
        if (sel) { g.setColour (juce::Colours::white.withAlpha (0.035f)); g.fillRoundedRectangle (r.reduced (1.0f), 6.0f); g.setColour (juce::Colours::white.withAlpha (0.16f)); g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 1.0f); }
        const auto tcol = track != nullptr ? DawSession::trackColour (*track) : VegaColours::gold;
        g.setColour (tcol); g.fillRoundedRectangle (juce::Rectangle<float> (W - 24.0f, 3.0f).withCentre ({ W * 0.5f, 5.0f }), 1.5f);
        g.setColour (track == nullptr ? VegaColours::gold : juce::Colour (0xffe9e9e9)); g.setFont (VegaFonts::ui (12.5f, true));
        g.drawText (track != nullptr ? track->getName() : juce::String ("MASTER"), 6, 9, W - 12, 18, juce::Justification::centred, true);
        if (showEditSection())
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, (float) headerH, W - 8.0f, (float) editH - 4.0f), 4.0f);
        }
        // EQ section, drawn like a plug-in panel: titled box, knob titles above, values below
        if (knobs[0].isVisible())
        {
            auto box = juce::Rectangle<float> (5.0f, (float) knobs[0].getY() - 15.0f, W - 10.0f, (float) (knobs[3].getBottom() - knobs[0].getY()) + 30.0f);
            g.setColour (juce::Colour (0xff181818)); g.fillRoundedRectangle (box, 4.0f);
            g.setColour (VegaColours::line); g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (VegaColours::line); g.drawHorizontalLine (knobs[3].getY() - 13, box.getX() + 6, box.getRight() - 6);
        }
        static const char* labels[] { "HI", "MID", "LOW", "FILTER" };
        for (int i = 0; i < mixlayout::numKnobs; ++i)
        {
            if (! knobs[i].isVisible()) continue;
            auto kb = knobs[i].getBounds();
            const double v = knobs[i].getValue();
            const bool killed = i < 3 && v <= VegaDJEQPlugin::killDb + 0.05;
            const bool active = i < 3 ? std::abs (v) >= 0.05 : std::abs (v) >= 0.03;
            g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::text);
            g.drawText (labels[i], 0, kb.getY() - 12, W, 12, juce::Justification::centred);
            g.setFont (VegaFonts::ui (9.5f));
            g.setColour (killed ? juce::Colour (0xffff5a5a) : active ? VegaColours::accent : VegaColours::textDim);
            g.drawText (active || killed ? knobs[i].getTextFromValue (v) : juce::String (i < 3 ? "0 dB" : "OFF"), 0, kb.getBottom() - 1, W, 11, juce::Justification::centred);
        }
        // scale ticks to the right of the fader
        auto fb = fader.getBounds();
        const int tx = fb.getCentreX() + 24;
        for (int i = 0; i <= 8; ++i)
        {
            const int y = fb.getY() + 13 + (int) ((fb.getHeight() - 26) * i / 8.0f);
            g.setColour (juce::Colour (0xffd0d0d0).withAlpha (0.75f));
            g.fillRect (i % 2 == 0 ? tx : tx + 6, y, i % 2 == 0 ? 22 : 12, 2);
        }
        // LED meter (left of the fader), same segments and colours as the plug-ins' meters
        {
            auto mr = meterArea.toFloat();
            const int nSeg = (int) std::size (vegaui::LedMeter::segDb);
            const float segH = mr.getHeight() / (float) nSeg, colW = (mr.getWidth() - 2.0f) / 2.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < nSeg; ++i)
                {
                    const float th = vegaui::LedMeter::segDb[i];
                    auto seg = juce::Rectangle<float> (mr.getX() + c * (colW + 2.0f), mr.getY() + i * segH, colW, segH).reduced (0, segH * 0.18f);
                    const juce::Colour col = th >= -0.1f ? juce::Colour (0xffff5a5a) : (th > -18.0f ? juce::Colours::white : VegaColours::accent);
                    g.setColour (level[c] >= th ? col : juce::Colour (0xff3a3a3a));
                    g.fillRect (seg);
                }
        }
        // value readout
        g.setFont (VegaFonts::mono (11.0f)); g.setColour (VegaColours::textDim);
        g.drawText (juce::String (fader.getValue(), 1) + " dB", valueArea, juce::Justification::centred);
        // column number like the hardware's 1..8 / 9..16 labels
        if (track != nullptr)
        {
            g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::textDim.withAlpha (0.8f));
            g.drawText (juce::String (col + 1), 0, mixlayout::padY (getHeight(), 0) - 15, W, 12, juce::Justification::centred);
        }
    }
    void resized() override
    {
        using namespace mixlayout;
        const int h = getHeight();
        int y = headerH;
        const bool ed = showEditSection();
        for (auto* c : std::initializer_list<juce::Component*> { &instrument, &swapInst, &addFx, &addSend, &output }) c->setVisible (ed && track != nullptr && (c != &instrument && c != &swapInst ? true : DawSession::kindOf (*track) == DawSession::TrackKind::midi));
        for (auto* s : slots) s->setVisible (ed);
        for (auto* s : sends) { s->knob.setVisible (ed); s->label.setVisible (ed); }
        if (ed)
        {
            int ey = y + 4;
            if (instrument.isVisible()) { instrument.setBounds (8, ey, W - 38, 22); swapInst.setBounds (W - 28, ey, 20, 22); ey += 26; }
            for (auto* s : slots) { s->setBounds (8, ey, W - 16, 20); ey += 22; }
            addFx.setBounds (8, ey, W - 16, 20); ey += 24;
            for (auto* s : sends) { s->label.setBounds (8, ey + 4, W - 48, 18); s->knob.setBounds (W - 40, ey, 32, 26); ey += 28; }
            addSend.setBounds (8, ey, W - 16, 20); ey += 24;
            output.setBounds (8, ey, W - 16, 22);
            y += editH;
        }
        else if (track == nullptr) y += editH;
        y += 4;
        for (int i = 0; i < numKnobs; ++i)
        {
            knobs[i].setVisible (track != nullptr);
            knobs[i].setBounds (W / 2 - 30, y + (i * knobRowH) + 11, 60, 46);
        }
        y += numKnobs * knobRowH + 8;
        pan.setBounds (10, y, W - 20, 20); y += 26;
        const int fBottom = (track != nullptr ? padY (h, 0) - 20 : h - bottom - 20) - 16;
        fader.setBounds (W / 2 - 36, y, 60, std::max (60, fBottom - y));
        meterArea = { fader.getX() - 10, y + 8, 12, std::max (40, fader.getHeight() - 16) };
        valueArea = { 0, fader.getBottom() + 1, W, 14 };
        if (track != nullptr) for (int i = 0; i < 2; ++i) pads[i].setBounds (2, padY (h, i), W - 4, padH);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (track != nullptr && e.y < mixlayout::headerH) { vs.selectTrack (track.get()); owner.touched (track->getName(), "Selected"); }
    }
    te::AudioTrack::Ptr track;
private:
    bool showEditSection() const { return owner.showEdit && track != nullptr; }
    VegaDJEQPlugin* dj() const { return dynamic_cast<VegaDJEQPlugin*> (djPlugin.get()); }
    void updateKnobs()
    {
        if (auto* d = dj())
        {
            for (int i = 0; i < 3; ++i) knobs[i].setValue (d->band[i]->getCurrentValue(), juce::dontSendNotification);
            knobs[3].setValue (d->filter->getCurrentValue(), juce::dontSendNotification);
        }
        repaint();
    }
    void touch (const juce::String& what, const juce::String& value) { owner.touched (name() + "  " + what, value); }
    void topPad()
    {
        if (owner.shift) { dawui::toggleArm (*track); owner.touched (track->getName(), dawui::isArmed (*track) ? "Armed" : "Disarmed"); }
        else { track->setSolo (! track->isSolo (false)); owner.touched (track->getName(), track->isSolo (false) ? "Solo On" : "Solo Off"); }
    }
    void bottomPad()
    {
        if (owner.shift) { vs.selectTrack (track.get()); owner.touched (track->getName(), "Selected"); }
        else { track->setMute (! track->isMuted (false)); owner.touched (track->getName(), track->isMuted (false) ? "Muted" : "Unmuted"); }
    }
    static void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff222225));
        b.setColour (juce::TextButton::textColourOffId, VegaColours::text);
    }
    struct SendRow
    {
        SendRow (te::AuxSendPlugin& s, const juce::String& busName) : send (s)
        {
            label.setText ("> " + busName, juce::dontSendNotification); label.setFont (VegaFonts::ui (10.5f));
            label.setColour (juce::Label::textColourId, VegaColours::text);
            knob.setRange (-60.0, 6.0, 0.1); knob.setSkewFactorFromMidPoint (-12.0);
            knob.setTextValueSuffix (" dB"); knob.setDoubleClickReturnValue (true, 0.0);
            knob.setValue (s.getGainDb(), juce::dontSendNotification);
            knob.onValueChange = [this] { send.setGainDb ((float) knob.getValue()); };
        }
        te::AuxSendPlugin& send; xl::Knob knob; juce::Label label;
    };
    void chooseInstrument()
    {
        dawui::choosePlugin (vs.edit().engine, true, &instrument, [this, t = track] (juce::String type, std::optional<juce::PluginDescription> d)
        {
            auto p = d ? vs.session.setInstrument (*t, {}, &*d) : vs.session.setInstrument (*t, type);
            rebuildSlots();
            dawui::openPlugin (p.get());
        });
    }
    void chooseFx()
    {
        dawui::choosePlugin (vs.edit().engine, false, &addFx, [this, t = track] (juce::String type, std::optional<juce::PluginDescription> d)
        {
            auto p = d ? vs.session.addPlugin (*t, {}, &*d) : vs.session.addPlugin (*t, type);
            rebuildSlots();
            dawui::openPlugin (p.get());
        });
    }
    void chooseSend()
    {
        juce::PopupMenu m; auto buses = vs.session.getBuses();
        for (int i = 0; i < buses.size(); ++i) if (buses[i] != track.get()) m.addItem (1 + i, buses[i]->getName(), vs.session.findSend (*track, *buses[i]) == nullptr);
        m.addSeparator(); m.addItem (999, "New bus...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addSend), [this, buses] (int r)
        {
            if (r == 999) { if (auto* b = vs.session.addTrack (DawSession::TrackKind::bus)) vs.session.setSend (*track, *b, -6.0f); }
            else if (r > 0) vs.session.setSend (*track, *buses[r - 1], -6.0f);
            rebuildSlots();
        });
    }
    void slotMenu (te::Plugin::Ptr p, juce::Component* target)
    {
        juce::PopupMenu m, sc;
        m.addItem (1, "Open editor");
        m.addItem (2, p->isEnabled() ? "Bypass" : "Enable");
        m.addItem (3, "Move up"); m.addItem (4, "Move down");
        if (p->canSidechain())
        {
            auto names = p->getSidechainSourceNames (true);
            const auto cur = p->getSidechainSourceName();
            for (int i = 0; i < names.size(); ++i) sc.addItem (100 + i, names[i], true, names[i] == cur);
            m.addSubMenu ("Sidechain input", sc);
        }
        m.addSeparator(); m.addItem (9, "Remove");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, p] (int r)
        {
            if (r == 1) dawui::openPlugin (p.get());
            else if (r == 2) p->setEnabled (! p->isEnabled());
            else if (r == 3 || r == 4)
            {
                const int idx = track->pluginList.indexOf (p.get()), to = idx + (r == 3 ? -1 : 1);
                if (to >= 0 && to < track->pluginList.size() && ! dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[to]) && ! (to == 0 && track->pluginList[0]->isSynth()))
                { auto state = p->state; auto parent = state.getParent(); parent.moveChild (parent.indexOf (state), parent.indexOf (track->pluginList[to]->state), &vs.edit().getUndoManager()); }
            }
            else if (r >= 100) { auto names = p->getSidechainSourceNames (true); p->setSidechainSourceByName (names[r - 100]); if (r > 100) p->guessSidechainRouting(); }
            else if (r == 9) { p->deleteFromParent(); }
            rebuildSlots();
        });
    }
    MixerView& owner;
    ViewState& vs;
    int col = 0;
    xl::Fader fader; xl::Knob knobs[mixlayout::numKnobs]; xl::PanBar pan; xl::Pad pads[2];
    juce::TextButton instrument, addFx, addSend, swapInst; juce::ComboBox output;
    juce::OwnedArray<juce::TextButton> slots; juce::OwnedArray<SendRow> sends; juce::Array<te::AudioTrack*> outBuses;
    te::Plugin::Ptr djPlugin;
    te::LevelMeasurer::Client client; te::LevelMeasurer* measurer = nullptr;
    bool masterAdded = false, wasSelected = false; te::EditPlaybackContext* masterCtx = nullptr;
    float level[2] { -100, -100 }; juce::Rectangle<int> meterArea, valueArea;
};

//==============================================================================
namespace tp
{
/** Light-grey console key; lit (salmon / cyan / green) when its function is on */
class Key : public juce::Button
{
public:
    enum Shape { square, pill, navUp, navDown, navLeft, navRight };
    explicit Key (Shape sh = square, juce::Colour litColour = juce::Colour (0xfff2867c)) : juce::Button ({}), shape (sh), lit (litColour) {}
    Shape shape; juce::Colour lit;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        const float rad = shape == pill ? r.getHeight() * 0.45f : 4.0f;
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r.translated (0, 2.0f), rad);
        const bool on = getToggleState();
        auto top = on ? lit.brighter (0.25f) : juce::Colour (down ? 0xffa4a9b2 : over ? 0xffc9ced6 : 0xffbcc1c9);
        auto bot = on ? lit.darker (0.15f) : juce::Colour (0xff8d929b);
        if (on) { g.setColour (lit.withAlpha (0.3f)); g.fillRoundedRectangle (r.expanded (3.0f), rad + 3.0f); }
        g.setGradientFill (juce::ColourGradient (top, 0, r.getY(), bot, 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + rad, r.getRight() - rad);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.drawRoundedRectangle (r, rad, 1.0f);
    }
};

/** Dark transport key with an icon */
class BigKey : public juce::Button
{
public:
    enum Icon { rew, ffwd, stop, play, rec };
    explicit BigKey (Icon i) : juce::Button ({}), icon (i) {}
    Icon icon;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRoundedRectangle (r.translated (0, 2.0f), 5.0f);
        g.setColour (juce::Colour (0xff0f1012)); g.fillRoundedRectangle (r, 5.0f);
        auto face = r.reduced (3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xff26282c : over ? 0xff3a3d42 : 0xff33363b), 0, face.getY(), juce::Colour (0xff222428), 0, face.getBottom(), false));
        g.fillRoundedRectangle (face, 3.0f);
        const auto c = face.getCentre(); const float s = face.getHeight() * 0.22f;
        const bool on = getToggleState();
        juce::Colour ink = juce::Colour (0xff5d6168);
        if (on) ink = icon == rec ? juce::Colour (0xffff3a4a) : icon == play ? juce::Colour (0xff32e070) : juce::Colour (0xffd8dce2);
        juce::Path p;
        auto tri = [&] (float cx, bool right) { if (right) p.addTriangle (cx - s, c.y - s, cx - s, c.y + s, cx + s * 0.9f, c.y); else p.addTriangle (cx + s, c.y - s, cx + s, c.y + s, cx - s * 0.9f, c.y); };
        switch (icon)
        {
            case rew:  tri (c.x - s * 0.9f, false); tri (c.x + s * 0.9f, false); break;
            case ffwd: tri (c.x - s * 0.9f, true);  tri (c.x + s * 0.9f, true);  break;
            case stop: p.addRectangle (c.x - s * 0.85f, c.y - s * 0.85f, s * 1.7f, s * 1.7f); break;
            case play: tri (c.x + s * 0.1f, true); break;
            case rec:  p.addEllipse (c.x - s * 0.95f, c.y - s * 0.95f, s * 1.9f, s * 1.9f); break;
        }
        if (on) { g.setColour (ink.withAlpha (0.35f)); g.strokePath (p, juce::PathStrokeType (5.0f)); }
        g.setColour (ink); g.fillPath (p);
    }
};

/** Jog wheel: drag around (or up / down) to move the playhead, or scrub when SCRUB is lit */
class Jog : public juce::Component
{
public:
    std::function<void (double turns)> onTurn;
    std::function<void (bool)> onTouch;
    bool plain = false;     // true: all-black wheel (no bright ring)
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        const auto c = r.getCentre(); const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (juce::Rectangle<float> (rad * 2 + 6, rad * 2 + 6).withCentre (c.translated (0, 3)));
        if (plain) g.setColour (juce::Colour (0xff1a1b1e));
        else g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x, c.y - rad, juce::Colour (0xff8a8e94), c.x, c.y + rad, false));
        g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c));                   // outer ring
        g.setColour (juce::Colour (0xff0d0e10)); g.fillEllipse (juce::Rectangle<float> (rad * 1.86f, rad * 1.86f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2e33), c.x - rad * 0.5f, c.y - rad * 0.6f, juce::Colour (0xff141518), c.x + rad * 0.5f, c.y + rad * 0.7f, true));
        g.fillEllipse (juce::Rectangle<float> (rad * 1.76f, rad * 1.76f).withCentre (c));
        // finger dimple
        const juce::Point<float> d (c.x + std::sin (angle) * rad * 0.52f, c.y - std::cos (angle) * rad * 0.52f);
        const float dr = rad * 0.24f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), d.x, d.y - dr, juce::Colour (0xff0c0d0f), d.x, d.y + dr, false));
        g.fillEllipse (juce::Rectangle<float> (dr * 2, dr * 2).withCentre (d));
        g.setColour (juce::Colours::white.withAlpha (0.08f)); g.drawEllipse (juce::Rectangle<float> (dr * 2, dr * 2).withCentre (d), 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { lastAngle = angleAt (e.position); lastY = e.position.y; if (onTouch) onTouch (true); }
    void mouseUp (const juce::MouseEvent&) override { if (onTouch) onTouch (false); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        double delta;
        if (e.position.getDistanceFrom (c) > getWidth() * 0.12f)
        {   // circular motion
            const float a = angleAt (e.position);
            float da = a - lastAngle;
            if (da > juce::MathConstants<float>::pi) da -= juce::MathConstants<float>::twoPi;
            if (da < -juce::MathConstants<float>::pi) da += juce::MathConstants<float>::twoPi;
            lastAngle = a; delta = da / juce::MathConstants<double>::twoPi;
        }
        else { delta = (lastY - e.position.y) / 300.0; lastAngle = angleAt (e.position); }
        lastY = e.position.y;
        angle += (float) (delta * juce::MathConstants<double>::twoPi);
        if (onTurn) onTurn (delta);
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const double delta = w.deltaY * 0.15;
        angle += (float) (delta * juce::MathConstants<double>::twoPi);
        if (onTurn) onTurn (delta);
        repaint();
    }
private:
    float angleAt (juce::Point<float> p) const { const auto c = getLocalBounds().toFloat().getCentre(); return std::atan2 (p.x - c.x, c.y - p.y); }
    float angle = 0, lastAngle = 0, lastY = 0;
};
} // namespace tp

namespace uf
{
/** White soft key; glows green when on */
class SoftKey : public juce::Button
{
public:
    SoftKey() : juce::Button ({}) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (r.translated (0, 2.0f), 3.0f);
        if (getToggleState())
        {
            g.setColour (juce::Colour (0xfffff0c8).withAlpha (0.35f)); g.fillRoundedRectangle (r.expanded (3.0f), 5.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c8), r.getCentreX(), r.getCentreY(), juce::Colour (0xffffc860), r.getX(), r.getY(), true));
        }
        else g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xffa8a8ac : over ? 0xffd2d2d6 : 0xffc4c4c8), 0, r.getY(), juce::Colour (0xff8e8e92), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
};

/** Square console key with a drawn icon or text; optional lit colour */
class Key : public juce::Button
{
public:
    enum Icon { none, left, right, up, down, dot, loop, metro, dropIn, shiftUp, write, text };
    Key (Icon i = none, juce::String t = {}, juce::Colour litCol = juce::Colour (0xffffd98a), bool darkFace = false)
        : juce::Button (t), icon (i), lit (litCol), dark (false) { juce::ignoreUnused (darkFace); }
    Icon icon; juce::Colour lit; bool dark;
    void paintButton (juce::Graphics& g, bool over, bool isDown) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = getToggleState();
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.translated (0, 2.0f), 3.0f);
        g.setColour (juce::Colour (0xff17181a)); g.fillRoundedRectangle (r, 3.0f);                           // bezel
        auto face = r.reduced (3.0f);
        if (on) { g.setColour (lit.withAlpha (0.4f)); g.fillRoundedRectangle (r.expanded (2.0f), 4.0f); }
        juce::Colour top = on ? lit.brighter (0.2f) : dark ? juce::Colour (isDown ? 0xff2a2c30 : over ? 0xff3b3e43 : 0xff33363a) : juce::Colour (isDown ? 0xffa8a8ac : over ? 0xffd2d2d6 : 0xffc4c4c8);
        juce::Colour bot = on ? lit.darker (0.2f) : dark ? juce::Colour (0xff222427) : juce::Colour (0xff8e8e92);
        g.setGradientFill (juce::ColourGradient (top, 0, face.getY(), bot, 0, face.getBottom(), false));
        g.fillRoundedRectangle (face, 2.0f);
        const auto ink = on || ! dark ? juce::Colour (0xff1c1d20) : juce::Colour (0xffe9eaec);
        const auto c = face.getCentre(); const float s = std::min (face.getWidth(), face.getHeight()) * 0.26f;
        g.setColour (ink);
        juce::Path p;
        switch (icon)
        {
            case left:  p.addTriangle (c.x + s * 0.7f, c.y - s, c.x + s * 0.7f, c.y + s, c.x - s * 0.8f, c.y); break;
            case right: p.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 0.8f, c.y); break;
            case up:    p.addTriangle (c.x - s, c.y + s * 0.7f, c.x + s, c.y + s * 0.7f, c.x, c.y - s * 0.8f); break;
            case down:  p.addTriangle (c.x - s, c.y - s * 0.7f, c.x + s, c.y - s * 0.7f, c.x, c.y + s * 0.8f); break;
            case dot:   p.addEllipse (c.x - s * 0.8f, c.y - s * 0.8f, s * 1.6f, s * 1.6f); break;
            case shiftUp: p.addArrow ({ c.x, c.y + s, c.x, c.y - s }, s * 0.7f, s * 1.8f, s * 1.0f); break;
            case loop:
            {
                juce::Path a; a.addCentredArc (c.x, c.y, s, s, 0, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
                g.strokePath (a, juce::PathStrokeType (1.6f));
                p.addTriangle (c.x + s * 0.2f, c.y - s * 1.4f, c.x + s * 0.2f, c.y - s * 0.6f, c.x + s * 0.9f, c.y - s);
                break;
            }
            case metro: p.addTriangle (c.x - s * 0.8f, c.y + s, c.x + s * 0.8f, c.y + s, c.x, c.y - s); g.fillPath (p); p.clear();
                        g.setColour (on || ! dark ? juce::Colours::white : juce::Colour (0xff222427)); g.drawLine (c.x, c.y + s * 0.6f, c.x + s * 0.6f, c.y - s * 0.6f, 1.5f); return;
            case dropIn: p.addArrow ({ c.x, c.y - s, c.x, c.y + s * 0.4f }, s * 0.6f, s * 1.6f, s * 0.9f); p.addRectangle (c.x - s, c.y + s * 0.7f, s * 2.0f, s * 0.35f); break;
            case write: { juce::Path a; a.addRectangle (c.x - s, c.y - s, s * 2, s * 2); g.strokePath (a, juce::PathStrokeType (1.4f));
                          p.addTriangle (c.x - s * 0.5f, c.y - s * 0.5f, c.x + s * 0.5f, c.y - s * 0.5f, c.x, c.y + s * 0.5f); break; }
            case text: g.setFont (VegaFonts::ui (getButtonText().length() > 3 ? 9.5f : 11.0f, true)); g.drawFittedText (getButtonText(), face.toNearestInt(), juce::Justification::centred, 2); return;
            case none: break;
        }
        g.fillPath (p);
    }
};

/** White transport key */
class TransportKey : public juce::Button
{
public:
    enum Icon { rew, ffwd, stop, play, rec };
    explicit TransportKey (Icon i) : juce::Button ({}), icon (i) {}
    Icon icon;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = getToggleState();
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.translated (0, 3.0f), 4.0f);
        g.setColour (juce::Colour (0xff1a1b1d)); g.fillRoundedRectangle (r, 4.0f);
        auto face = r.reduced (3.0f);
        juce::Colour top = juce::Colour (down ? 0xffb8b8bc : over ? 0xffe2e2e6 : 0xffd2d2d6), bot = juce::Colour (0xff8a8a8e);
        if (on && icon == play) { top = juce::Colour (0xff8cff6c); bot = juce::Colour (0xff3fcf32); g.setColour (juce::Colour (0x667dff5a)); g.fillRoundedRectangle (r.expanded (3.0f), 6.0f); }
        if (on && icon == rec)  { top = juce::Colour (0xffff7a7a); bot = juce::Colour (0xffe03a3a); g.setColour (juce::Colour (0x66ff4040)); g.fillRoundedRectangle (r.expanded (3.0f), 6.0f); }
        g.setGradientFill (juce::ColourGradient (top, 0, face.getY(), bot, 0, face.getBottom(), false));
        g.fillRoundedRectangle (face, 3.0f);
        const auto c = face.getCentre(); const float s = face.getHeight() * 0.2f;
        juce::Path p;
        auto tri = [&] (float cx, bool rgt) { if (rgt) p.addTriangle (cx - s, c.y - s, cx - s, c.y + s, cx + s * 0.9f, c.y); else p.addTriangle (cx + s, c.y - s, cx + s, c.y + s, cx - s * 0.9f, c.y); };
        switch (icon)
        {
            case rew: tri (c.x - s * 0.9f, false); tri (c.x + s * 0.9f, false); break;
            case ffwd: tri (c.x - s * 0.9f, true); tri (c.x + s * 0.9f, true); break;
            case stop: p.addRectangle (c.x - s * 0.9f, c.y - s * 0.9f, s * 1.8f, s * 1.8f); break;
            case play: tri (c.x + s * 0.15f, true); break;
            case rec: p.addEllipse (c.x - s * 0.75f, c.y - s * 0.75f, s * 1.5f, s * 1.5f); break;
        }
        g.setColour (icon == rec ? juce::Colour (on ? 0xffffffff : 0xffe8352f) : juce::Colour (0xff2a2b2e));
        g.fillPath (p);
    }
};

/** Endless chrome encoder (knurled skirt, black top, white index dot) */
class Encoder : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (double)> onDelta;    // +/- per drag step (1.0 = one full sweep)
    std::function<void()> onReset;
    void paint (juce::Graphics& g) override
    {
        // same look as the master section's knobs: ring of dots, grey skirt, dark cap, white pointer
        auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre(); const float outer = std::min (r.getWidth(), r.getHeight()) * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        for (int i = 0; i < 24; ++i)
        {
            const float a = i * juce::MathConstants<float>::twoPi / 24.0f;
            g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre ({ c.x + std::sin (a) * outer * 0.95f, c.y - std::cos (a) * outer * 0.95f }));
        }
        const float d = outer * 1.5f;
        auto skirt = juce::Rectangle<float> (d, d).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillEllipse (skirt.translated (0, 3.0f).expanded (1.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd6d6d8), skirt.getX(), skirt.getY(), juce::Colour (0xff6c6c70), skirt.getRight(), skirt.getBottom(), false));
        g.fillEllipse (skirt);
        auto cap = skirt.reduced (d * 0.16f).translated (0, -d * 0.04f);
        const auto capCol = console::black;
        g.setGradientFill (juce::ColourGradient (capCol.brighter (0.45f), cap.getX(), cap.getY(), capCol.darker (0.35f), cap.getRight(), cap.getBottom(), true));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillEllipse (cap.reduced (cap.getWidth() * 0.22f).translated (-cap.getWidth() * 0.1f, -cap.getHeight() * 0.12f));
        g.setColour (juce::Colours::white);
        g.drawLine (c.x + std::sin (angle) * d * 0.12f, c.y - std::cos (angle) * d * 0.12f, c.x + std::sin (angle) * d * 0.47f, c.y - std::cos (angle) * d * 0.47f, 2.4f);
    }
    void mouseDown (const juce::MouseEvent& e) override { lastY = e.position.y; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double d = (lastY - e.position.y) / 200.0; lastY = e.position.y;
        angle += (float) (d * juce::MathConstants<double>::twoPi);
        if (onDelta) onDelta (d);
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { const double d = w.deltaY * 0.08; angle += (float) (d * 6.28); if (onDelta) onDelta (d); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }
private:
    float angle = 0, lastY = 0;
};
} // namespace uf

/** Left-hand column, styled after a console's channel / transport control unit, drawn on a fixed
    460 x 880 canvas and scaled exactly like the master section. */
class MixerView::ControlPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int CW = 460, CH = 880;
    int widthForHeight (int h) const { return juce::roundToInt (CW * (float) h / CH); }

    ControlPanel (MixerView& o, ViewState& v) : owner (o), vs (v)
    {
        addAndMakeVisible (canvas);
        canvas.setSize (CW, CH);
        canvas.paintFn = [this] (juce::Graphics& g) { paintCanvas (g); };

        // soft keys: VU (meters) / EDIT (inserts) / SHIFT (pads arm/select) / LEARN (MIDI learn)
        for (int i = 0; i < 4; ++i) { canvas.addAndMakeVisible (soft[i]); soft[i].setBounds (54 + i * 96, 22, 60, 28); }
        soft[0].setToggleState (true, juce::dontSendNotification);
        soft[0].onClick = [this] { soft[0].setToggleState (true, juce::dontSendNotification); peak[0] = peak[1] = -100; owner.touched ("Meters", "Peak hold reset"); };
        soft[1].onClick = [this] { owner.showEdit = ! owner.showEdit; owner.touched ("Edit", owner.showEdit ? "Inserts shown" : "Inserts hidden"); owner.layoutChanged(); };
        soft[2].onClick = [this] { owner.shift = ! owner.shift; owner.touched ("Pads", owner.shift ? "Arm / Select" : "Solo / Mute"); };
        soft[3].onClick = [this] { auto& ls = vs.edit().engine.getMidiLearnState(); ls.setActive (! ls.isActive()); owner.touched ("MIDI learn", ls.isActive() ? "Move a control, then a knob" : "Off"); };
        soft[0].setTooltip ("VU meters (click to reset the peak hold)"); soft[1].setTooltip ("Edit: show / hide each channel's instrument, inserts and sends");
        soft[2].setTooltip ("Shift: the pads become Record Arm (top) and Select (bottom)"); soft[3].setTooltip ("MIDI learn: move a control on screen, then a knob on your controller");

        // 4 encoders under the screen (page 1: volume / pan / filter / VU ref, page 2 (5-8): HI / MID / LOW / master)
        for (int i = 0; i < 4; ++i)
        {
            canvas.addAndMakeVisible (enc[i]); enc[i].setBounds (encX (i) - 30, 334, 60, 60);
            enc[i].onDelta = [this, i] (double d) { encoderMoved (i, d); };
            enc[i].onReset = [this, i] { encoderReset (i); };
        }
        canvas.addAndMakeVisible (channel); channel.setBounds (62, 432, 96, 96);
        channel.onDelta = [this] (double d) { chanAcc += d * 12.0; while (chanAcc >= 1) { owner.selectNeighbour (1); chanAcc -= 1; } while (chanAcc <= -1) { owner.selectNeighbour (-1); chanAcc += 1; } };
        channel.setTooltip ("CHANNEL: turn to select tracks");

        auto key = [this] (uf::Key& k, int x, int y, int w = 40, int h = 34) { canvas.addAndMakeVisible (k); k.setBounds (x, y, w, h); };
        key (mode, 232, 424); key (page58, 282, 424); key (chanL, 332, 424); key (chanR, 382, 424);
        key (bankL, 232, 482); key (bankR, 282, 482); key (layer, 332, 482); key (scrub, 382, 482);
        mode.setClickingTogglesState (true); page58.setClickingTogglesState (true); layer.setClickingTogglesState (true); scrub.setClickingTogglesState (true);
        mode.setTooltip ("MODE: the arrow keys zoom instead of moving"); page58.setTooltip ("5-8: encoders control HI / MID / LOW / master volume");
        chanL.setTooltip ("Shift the mixer by one channel"); chanR.setTooltip ("Shift the mixer by one channel");
        bankL.setTooltip ("Previous 8 channels"); bankR.setTooltip ("Next 8 channels");
        layer.setTooltip ("LAYER: rewind / fast-forward nudge the selected clip by one grid step"); scrub.setTooltip ("SCRUB: the jog wheel plays the audio as you turn it");
        mode.onClick = [this] { owner.touched ("Mode", mode.getToggleState() ? "Arrows zoom" : "Arrows navigate"); };
        page58.onClick = [this] { owner.touched ("Encoders", page58.getToggleState() ? "5-8: Hi / Mid / Low / Master" : "1-4: Vol / Pan / Filter / VU ref"); };
        chanL.onClick = [this] { owner.scrollStrips (-1); }; chanR.onClick = [this] { owner.scrollStrips (1); };
        bankL.onClick = [this] { owner.pageBy (-1); }; bankR.onClick = [this] { owner.pageBy (1); };
        layer.onClick = [this] { owner.touched ("Nudge", layer.getToggleState() ? "<< >> move the clip" : "Off"); };
        scrub.onClick = [this] { owner.touched ("Jog wheel", scrub.getToggleState() ? "Scrub" : "Move playhead"); };

        // navigation cluster + jog
        key (navUp, 92, 568, 36, 34); key (navDown, 92, 668, 36, 34); key (navLeft, 42, 618, 36, 34); key (navRight, 142, 618, 36, 34); key (navDot, 92, 618, 36, 34);
        navUp.onClick = [this] { if (mode.getToggleState()) vZoom (1); else owner.selectNeighbour (-1); };
        navDown.onClick = [this] { if (mode.getToggleState()) vZoom (-1); else owner.selectNeighbour (1); };
        navLeft.onClick = [this] { if (mode.getToggleState()) hZoom (-1); else moveBars (-1); };
        navRight.onClick = [this] { if (mode.getToggleState()) hZoom (1); else moveBars (1); };
        navDot.onClick = [this] { toggleMarker(); };
        navUp.setTooltip ("Previous track (MODE: taller tracks)"); navDown.setTooltip ("Next track (MODE: shorter tracks)");
        navLeft.setTooltip ("Back one bar (MODE: zoom out)"); navRight.setTooltip ("Forward one bar (MODE: zoom in)");
        navDot.setTooltip ("Drop a marker at the playhead (or remove the one there)");
        canvas.addAndMakeVisible (jog); jog.setBounds (232, 548, 200, 200); jog.plain = true;
        jog.onTurn = [this] (double t) { jogTurn (t); };
        jog.onTouch = [this] (bool d) { if (scrub.getToggleState()) vs.edit().getTransport().setUserDragging (d); };

        // automation / function row
        juce::Button* row[] { &autoOff, &autoRead, &autoWrite, &cycle, &click, &drop, &shiftKey };
        for (int i = 0; i < 7; ++i) { canvas.addAndMakeVisible (row[i]); row[i]->setBounds (36 + i * 56, 760, 38, 32); }
        autoOff.setTooltip ("Automation OFF (no read, no write)"); autoRead.setTooltip ("Automation READ"); autoWrite.setTooltip ("Automation WRITE: record moves while playing");
        cycle.setTooltip ("Cycle (loop)"); click.setTooltip ("Metronome"); drop.setTooltip ("Drop-in: record only inside the cycle range"); shiftKey.setTooltip ("Shift: Rew / Ffwd jump between markers");
        autoOff.onClick = [this] { auto& a = vs.edit().getAutomationRecordManager(); a.setReadingAutomation (false); a.setWritingAutomation (false); owner.touched ("Automation", "Off"); };
        autoRead.onClick = [this] { auto& a = vs.edit().getAutomationRecordManager(); a.setReadingAutomation (true); a.setWritingAutomation (false); owner.touched ("Automation", "Read"); };
        autoWrite.onClick = [this] { auto& a = vs.edit().getAutomationRecordManager(); a.setReadingAutomation (true); a.setWritingAutomation (! a.isWritingAutomation()); owner.touched ("Automation", a.isWritingAutomation() ? "Write" : "Read"); };
        cycle.onClick = [this] { auto& t = vs.edit().getTransport(); t.looping = ! t.looping.get(); if (t.looping && t.getLoopRange().getLength() <= 0s) t.setLoopRange ({ 0s, vs.session.barLength() * 4 }); owner.touched ("Cycle", t.looping ? "On" : "Off"); };
        click.onClick = [this] { vs.edit().clickTrackEnabled = ! vs.edit().clickTrackEnabled.get(); owner.touched ("Click", vs.edit().clickTrackEnabled.get() ? "On" : "Off"); };
        drop.onClick = [this] { auto& e = vs.edit(); e.recordingPunchInOut = ! e.recordingPunchInOut.get(); owner.touched ("Drop-in", e.recordingPunchInOut ? "Punch inside the cycle" : "Off"); };
        shiftKey.setClickingTogglesState (true);

        // transport
        juce::Button* tk[] { &rew, &ffwd, &stop, &play, &rec };
        for (int i = 0; i < 5; ++i) { canvas.addAndMakeVisible (tk[i]); tk[i]->setBounds (30 + i * 82, 808, 70, 46); }
        rew.onClick = [this] { if (! held) step (-1); held = false; };
        ffwd.onClick = [this] { if (! held) step (1); held = false; };
        stop.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isPlaying() || t.isRecording()) t.stop (false, false); else t.setPosition (0s); };
        play.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isPlaying()) t.stop (false, false); else t.play (false); };
        rec.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isRecording()) t.stop (true, false); else t.record (false); };
        rew.setTooltip ("Back one bar (hold to keep going). SHIFT: previous marker. LAYER: nudge clip"); ffwd.setTooltip ("Forward one bar (hold to keep going). SHIFT: next marker. LAYER: nudge clip");
        stop.setTooltip ("Stop (again: return to zero)");
        startTimerHz (30);
    }
    ~ControlPanel() override { forgetEdit(); }
    /** Stop metering the current Edit (called before it's destroyed) */
    void forgetEdit() { if (masterAdded && masterCtx != nullptr) masterCtx->masterLevels.removeClient (client); masterAdded = false; masterCtx = nullptr; }

    void show (const juce::String& a, const juce::String& b) { line1 = a; line2 = b; canvas.repaint(); }
    void resized() override { canvas.setTransform (juce::AffineTransform::scale ((float) getHeight() / CH)); }

private:
    struct Canvas : juce::Component
    {
        std::function<void (juce::Graphics&)> paintFn;
        void paint (juce::Graphics& g) override { if (paintFn) paintFn (g); }
    };
    static int encX (int i) { return 84 + i * 97; }

    // ---------------------------------------------------------------- encoders
    te::AudioTrack* target() { return vs.getSelectedTrack(); }
    void encoderMoved (int i, double d)
    {
        auto* t = target();
        const bool p2 = page58.getToggleState();
        if (! p2 && i == 3) { vuRef = juce::jlimit (-24.0f, -8.0f, vuRef + (float) d * 16.0f); owner.touched ("VU reference", juce::String (vuRef, 1) + " dBFS = 0 VU"); return; }
        if (p2 && i == 3)
        {
            if (auto vp = vs.edit().getMasterVolumePlugin()) { vp->setVolumeDb (juce::jlimit (-60.0f, 6.0f, vp->getVolumeDb() + (float) d * 30.0f)); owner.touched ("Master", juce::String (vp->getVolumeDb(), 1) + " dB"); }
            return;
        }
        if (t == nullptr) { owner.touched ("Encoders", "Select a track first"); return; }
        if (! p2 && i == 0) { if (auto vp = t->getVolumePlugin()) { vp->setVolumeDb (juce::jlimit (-60.0f, 6.0f, vp->getVolumeDb() + (float) d * 30.0f)); owner.touched (t->getName() + "  Volume", juce::String (vp->getVolumeDb(), 1) + " dB"); } return; }
        if (! p2 && i == 1) { if (auto vp = t->getVolumePlugin()) { vp->setPan (juce::jlimit (-1.0f, 1.0f, vp->getPan() + (float) d * 2.0f)); owner.touched (t->getName() + "  Pan", panText (vp->getPan())); } return; }
        auto* dj = vs.session.djEq (*t, false);
        if (dj == nullptr) return;
        if (! p2 && i == 2) { dj->filter->setParameter (juce::jlimit (-1.0f, 1.0f, dj->filter->getCurrentValue() + (float) d * 2.0f), juce::sendNotification); owner.touched (t->getName() + "  Filter", VegaDJEQPlugin::describeFilter (dj->filter->getCurrentValue())); return; }
        if (p2 && i < 3)
        {
            auto& b = dj->band[i];
            b->setParameter (juce::jlimit (VegaDJEQPlugin::killDb, VegaDJEQPlugin::maxDb, b->getCurrentValue() + (float) d * 46.0f), juce::sendNotification);
            static const char* nm[] { "Hi", "Mid", "Low" };
            owner.touched (t->getName() + "  " + nm[i], b->getCurrentValueAsString());
        }
    }
    void encoderReset (int i)
    {
        auto* t = target(); const bool p2 = page58.getToggleState();
        if (! p2 && i == 3) { vuRef = -18.0f; return; }
        if (p2 && i == 3) { if (auto vp = vs.edit().getMasterVolumePlugin()) vp->setVolumeDb (0.0f); return; }
        if (t == nullptr) return;
        if (! p2 && i == 0) { if (auto vp = t->getVolumePlugin()) vp->setVolumeDb (0.0f); return; }
        if (! p2 && i == 1) { if (auto vp = t->getVolumePlugin()) vp->setPan (0.0f); return; }
        if (auto* dj = vs.session.djEq (*t, false)) { if (! p2 && i == 2) dj->filter->setParameter (0.0f, juce::sendNotification); else if (p2) dj->band[i]->setParameter (0.0f, juce::sendNotification); }
    }
    static juce::String panText (float v) { return std::abs (v) < 0.01f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? "L" : "R"); }
    juce::String encLabel (int i, juce::String& value)
    {
        auto* t = target(); const bool p2 = page58.getToggleState();
        value = "-";
        if (! p2 && i == 3) { value = juce::String (vuRef, 1) + " dB"; return "Ref"; }
        if (p2 && i == 3) { if (auto vp = vs.edit().getMasterVolumePlugin()) value = juce::String (vp->getVolumeDb(), 1); return "Master"; }
        if (t == nullptr) return i == 0 ? "Volume" : i == 1 ? "Pan" : i == 2 ? "Filter" : "";
        if (! p2 && i == 0) { if (auto vp = t->getVolumePlugin()) value = juce::String (vp->getVolumeDb(), 1); return "Volume"; }
        if (! p2 && i == 1) { if (auto vp = t->getVolumePlugin()) value = panText (vp->getPan()); return "Pan"; }
        if (auto* dj = vs.session.djEq (*t, false))
        {
            if (! p2 && i == 2) { value = VegaDJEQPlugin::describeFilter (dj->filter->getCurrentValue()); return "Filter"; }
            if (p2) { const float v = dj->band[i]->getCurrentValue(); value = v <= VegaDJEQPlugin::killDb + 0.05f ? juce::String ("KILL") : juce::String (v, 1); return i == 0 ? "Hi" : i == 1 ? "Mid" : "Low"; }
        }
        return {};
    }

    // ---------------------------------------------------------------- transport helpers
    void step (int dir)
    {
        auto& t = vs.edit().getTransport();
        if (shiftKey.getToggleState() || juce::ModifierKeys::currentModifiers.isShiftDown())
        {
            auto& mm = vs.edit().getMarkerManager();
            auto* m = dir > 0 ? mm.getNextMarker (t.getPosition() + te::TimeDuration::fromSeconds (0.001)) : mm.getPrevMarker (t.getPosition() - te::TimeDuration::fromSeconds (0.001));
            if (m != nullptr) { t.setPosition (m->getPosition().getStart()); owner.touched ("Marker", m->getName()); }
            return;
        }
        if (layer.getToggleState())
        {
            if (auto* c = vs.getSelectedClip())
            {
                const double grid = vs.snapDiv > 0 ? 1.0 / vs.snapDiv : 1.0;
                c->setStart (vs.beatsToTime (std::max (0.0, vs.timeToBeats (c->getPosition().getStart()) + dir * grid)), false, true);
                owner.touched ("Nudge " + c->getName(), dawui::formatBarsBeats (vs.edit(), c->getPosition().getStart()));
            }
            else owner.touched ("Nudge", "Select a clip first");
            return;
        }
        moveBars (dir);
    }
    void moveBars (int dir)
    {
        auto& t = vs.edit().getTransport();
        const double bar = vs.session.barLength().inSeconds(), now = t.getPosition().inSeconds();
        t.setPosition (te::TimePosition::fromSeconds (dir > 0 ? (std::floor (now / bar + 1e-6) + 1) * bar : std::max (0.0, (std::ceil (now / bar - 1e-6) - 1) * bar)));
        owner.touched ("Position", dawui::formatBarsBeats (vs.edit(), t.getPosition()));
    }
    void hZoom (int dir) { vs.pixelsPerBeat = juce::jlimit (2.0, 400.0, vs.pixelsPerBeat * (dir > 0 ? 1.25 : 0.8)); owner.touched ("Zoom", juce::String (vs.pixelsPerBeat, 0) + " px / beat"); }
    void vZoom (int dir)
    {
        for (auto* t : vs.session.getTracks (true)) t->state.setProperty ("vegaHeight", juce::jlimit (44, 260, (int) t->state.getProperty ("vegaHeight", 84) + dir * 16), nullptr);
        owner.touched ("Track height", dir > 0 ? "Taller" : "Shorter");
    }
    void toggleMarker()
    {
        auto& mm = vs.edit().getMarkerManager();
        const auto now = vs.edit().getTransport().getPosition();
        for (auto& m : mm.getMarkers())
            if (std::abs ((m->getPosition().getStart() - now).inSeconds()) < 0.05) { m->removeFromParent(); owner.touched ("Marker", "Removed"); return; }
        int next = 1; for (auto& m : mm.getMarkers()) next = std::max (next, m->getMarkerID() + 1);
        if (mm.createMarker (next, now, vs.session.barLength(), nullptr) != nullptr) owner.touched ("Marker " + juce::String (next), dawui::formatBarsBeats (vs.edit(), now));
    }
    void jogTurn (double turns)
    {
        auto& t = vs.edit().getTransport();
        if (scrub.getToggleState()) { te::scrub (t, turns * 40.0); return; }
        t.setPosition (vs.beatsToTime (std::max (0.0, vs.timeToBeats (t.getPosition()) + turns * 4.0)));
        owner.touched ("Jog", dawui::formatBarsBeats (vs.edit(), t.getPosition()));
    }

    // ---------------------------------------------------------------- metering + state
    void timerCallback() override
    {
        auto& t = vs.edit().getTransport();
        auto set = [] (juce::Button& b, bool on) { if (b.getToggleState() != on) b.setToggleState (on, juce::dontSendNotification); };
        auto& arm = vs.edit().getAutomationRecordManager();
        set (rec, t.isRecording()); set (play, t.isPlaying()); set (cycle, t.looping.get()); set (drop, vs.edit().recordingPunchInOut.get());
        set (click, vs.edit().clickTrackEnabled.get());
        set (autoWrite, arm.isWritingAutomation()); set (autoRead, arm.isReadingAutomation() && ! arm.isWritingAutomation()); set (autoOff, ! arm.isReadingAutomation() && ! arm.isWritingAutomation());
        set (soft[1], owner.showEdit); set (soft[2], owner.shift); set (soft[3], vs.edit().engine.getMidiLearnState().isActive());
        for (auto [k, dir] : { std::pair<juce::Button*, int> { &rew, -1 }, { &ffwd, 1 } })
        {
            if (k->isDown())
            {
                if (++holdTicks[dir > 0] > 9)
                {
                    held = true;
                    t.setPosition (vs.beatsToTime (std::max (0.0, vs.timeToBeats (t.getPosition()) + dir * 0.6)));
                    owner.touched (dir > 0 ? "Fast forward" : "Rewind", dawui::formatBarsBeats (vs.edit(), t.getPosition()));
                }
            }
            else holdTicks[dir > 0] = 0;
        }
        // master levels -> VU ballistics (~300 ms integration) + peak hold
        if (auto* ctx = vs.edit().getCurrentPlaybackContext())
        {
            if (! masterAdded || masterCtx != ctx)
            {
                if (masterAdded && masterCtx != nullptr) masterCtx->masterLevels.removeClient (client);
                ctx->masterLevels.addClient (client); masterAdded = true; masterCtx = ctx;
            }
            for (int c = 0; c < 2; ++c)
            {
                const float db = client.getAndClearAudioLevel (c).dB;
                const float vu = db - vuRef - 3.0f;                         // peak -> approx. average, referenced to 0 VU
                const float target = juce::jlimit (-25.0f, 4.0f, db < -99 ? -25.0f : vu);
                vuDb[c] += (target - vuDb[c]) * 0.18f;
                current[c] = db < -99 ? -100.0f : db;
                if (current[c] > peak[c]) peak[c] = current[c];
            }
        }
        canvas.repaint();
    }

    // ---------------------------------------------------------------- drawing
    void paintCanvas (juce::Graphics& g)
    {
        if (plate.isNull()) plate = makePlate();
        g.drawImageAt (plate, 0, 0);
        // screen
        auto scr = juce::Rectangle<float> (30, 70, 400, 250);
        g.setColour (juce::Colour (0xff121214)); g.fillRoundedRectangle (scr.expanded (6), 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.drawRoundedRectangle (scr.expanded (6), 6.0f, 2.0f);
        g.setColour (juce::Colour (0xff070809)); g.fillRect (scr);
        // tab bar (soft key labels)
        auto tabs = scr.removeFromTop (20.0f);
        g.setColour (juce::Colour (0xff1c2448)); g.fillRect (tabs);
        static const char* tabNames[] { "VU METERS", "EDIT", "SHIFT", "LEARN" };
        for (int i = 0; i < 4; ++i)
        {
            auto tr = juce::Rectangle<float> (tabs.getX() + i * 100.0f, tabs.getY(), 100.0f, tabs.getHeight());
            if (soft[i].getToggleState()) { g.setColour (juce::Colours::white); g.fillRect (tr.reduced (6, 2)); }
            g.setColour (soft[i].getToggleState() ? juce::Colour (0xff1c2448) : juce::Colour (0xffd8dcf0));
            g.setFont (VegaFonts::ui (11.0f, true)); g.drawText (tabNames[i], tr, juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff0b0e1c)); g.fillRect (scr);
        // touched line
        g.setColour (juce::Colour (0xffc9d2ff)); g.setFont (VegaFonts::ui (11.0f, true));
        g.drawText (line1 + (line2.isNotEmpty() ? "   " + line2 : juce::String()), scr.removeFromTop (18.0f).reduced (8, 0).toNearestInt(), juce::Justification::centred, true);
        // two VU meters
        for (int c = 0; c < 2; ++c)
        {
            auto mr = juce::Rectangle<float> (scr.getX() + 12 + c * 194.0f, scr.getY() + 6, 182.0f, 118.0f);
            g.setColour (juce::Colour (0xffd8dcf0)); g.setFont (VegaFonts::ui (11.0f, true));
            g.drawText (c == 0 ? "L" : "R", mr.withHeight (12).translated (0, -4), juce::Justification::centred);
            drawVu (g, mr.withTrimmedTop (10), vuDb[c]);
            // current / max
            auto rd = juce::Rectangle<float> (mr.getX(), mr.getBottom() + 4, mr.getWidth(), 34);
            g.setFont (VegaFonts::ui (9.5f)); g.setColour (juce::Colour (0xff9aa3c8));
            g.drawText ("current", rd.withWidth (rd.getWidth() / 2).withHeight (12), juce::Justification::centred);
            g.setColour (juce::Colour (0xffff7a8a)); g.drawText ("max", rd.withTrimmedLeft (rd.getWidth() / 2).withHeight (12), juce::Justification::centred);
            g.setFont (VegaFonts::ui (16.0f, true)); g.setColour (juce::Colours::white);
            g.drawText (current[c] < -99 ? juce::String ("-inf") : juce::String (current[c], 1) + " dB", rd.withWidth (rd.getWidth() / 2).withTrimmedTop (12), juce::Justification::centred);
            g.setColour (juce::Colour (0xffff5a6e));
            g.drawText (peak[c] < -99 ? juce::String ("-inf") : juce::String (peak[c], 1) + " dB", rd.withTrimmedLeft (rd.getWidth() / 2).withTrimmedTop (12), juce::Justification::centred);
        }
        // bottom bar: what each encoder does
        auto bar = juce::Rectangle<float> (scr.getX(), 300, scr.getWidth(), 20);
        g.setColour (juce::Colour (0xff1c2448)); g.fillRect (bar);
        for (int i = 0; i < 4; ++i)
        {
            juce::String value; const auto name = encLabel (i, value);
            auto seg = juce::Rectangle<float> ((float) encX (i) - 48, bar.getY(), 96, bar.getHeight());
            g.setColour (juce::Colour (0xff9aa3c8)); g.setFont (VegaFonts::ui (9.5f, true));
            g.drawText (name, seg.withWidth (44).toNearestInt(), juce::Justification::centredRight);
            g.setColour (juce::Colours::white); g.fillRect (seg.withTrimmedLeft (48).reduced (0, 3));
            g.setColour (juce::Colour (0xff11152a)); g.drawText (value, seg.withTrimmedLeft (48).toNearestInt(), juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff3a3c40));
        for (int i = 1; i < 4; ++i) g.fillRect ((float) encX (i) - 49, bar.getY() + 3, 1.0f, bar.getHeight() - 6);
        // labels + channel arcs
        g.setColour (console::ink); g.setFont (VegaFonts::ui (12.0f, true));
        g.drawText ("CHANNEL", 50, 536, 120, 16, juce::Justification::centred);
        {
            juce::Path arc; arc.addCentredArc (110, 480, 62, 62, 0, -2.4f, -0.5f, true);
            juce::Path arc2; arc2.addCentredArc (110, 480, 62, 62, 0, 0.5f, 2.4f, true);
            g.strokePath (arc, juce::PathStrokeType (1.6f)); g.strokePath (arc2, juce::PathStrokeType (1.6f));
            g.fillEllipse (110 - 62 * std::sin (2.4f) - 3, 480 + 62 * std::cos (2.4f) - 3, 6, 6);
            g.fillEllipse (110 + 62 * std::sin (2.4f) - 3, 480 + 62 * std::cos (2.4f) - 3, 6, 6);
        }
        g.setFont (VegaFonts::ui (11.0f, true));
        g.drawText ("BANK", 232, 518, 90, 14, juce::Justification::centred);
        g.drawText ("LAYER", 332, 518, 40, 14, juce::Justification::centred);
        g.drawText ("SCRUB", 376, 518, 52, 14, juce::Justification::centred);
        g.setColour (console::line); g.fillRect (232.0f, 468.0f, 190.0f, 2.0f); g.setColour (console::ink);
        g.drawLine (240, 532, 252, 532, 1.2f); g.drawLine (302, 532, 314, 532, 1.2f);
        // nav ring
        {
            juce::Path ring; ring.addEllipse (110 - 66, 635 - 66, 132, 132);
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.strokePath (ring, juce::PathStrokeType (1.4f));
        }
        // function row labels
        static const char* fn[] { "OFF", "READ", "WRT", "CYCLE", "CLICK", "DROP", "SHIFT" };
        for (int i = 0; i < 7; ++i)
        {
            auto lr = juce::Rectangle<float> (36.0f + i * 56 - 4, 795, 46, 13);
            g.setColour (console::inkDim); g.setFont (VegaFonts::ui (9.5f, true)); g.drawText (fn[i], lr, juce::Justification::centred);
        }
        // brand
        g.setColour (console::line); g.fillRect (24.0f, 752.0f, (float) CW - 48, 2.0f);
        g.setColour (console::inkDim); g.setFont (VegaFonts::ui (11.0f, true));
        g.drawText ("Vega Productions", 30, 858, 200, 14, juce::Justification::centredLeft);
        g.setFont (VegaFonts::ui (9.5f)); g.drawText ("CONTROL SECTION", CW - 230, 858, 200, 14, juce::Justification::centredRight);
    }

    void drawVu (juce::Graphics& g, juce::Rectangle<float> r, float vu)
    {
        // bezel + backlit face: warm blue glow from inside
        g.setColour (juce::Colour (0xff2b2f38)); g.fillRoundedRectangle (r, 4.0f);
        auto face = r.reduced (5.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3eeff), face.getCentreX(), face.getBottom() - 10, juce::Colour (0xff6f95d8), face.getX(), face.getY(), true));
        g.fillRoundedRectangle (face, 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x66ffe6b0), face.getCentreX(), face.getBottom(), juce::Colour (0x00ffe6b0), face.getCentreX(), face.getY(), false));
        g.fillRoundedRectangle (face, 2.0f);                                           // warm lamp glow at the bottom
        const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.55f);
        const float rad = face.getHeight() * 1.04f;
        auto angleFor = [] (float v)
        {   // classic VU scale: -20 .. +3 spread over about 96 degrees, non-linear
            const float n = std::pow (juce::Decibels::decibelsToGain (juce::jlimit (-20.0f, 3.0f, v)) / juce::Decibels::decibelsToGain (3.0f), 0.85f);
            return juce::degreesToRadians (-40.0f + 80.0f * n);       // keeps the whole scale inside the face
        };
        // scale arcs
        juce::Path arc; arc.addCentredArc (pivot.x, pivot.y, rad, rad, 0, angleFor (-20), angleFor (0), true);
        g.setColour (juce::Colour (0xff1b2340)); g.strokePath (arc, juce::PathStrokeType (1.3f));
        juce::Path red; red.addCentredArc (pivot.x, pivot.y, rad + 2, rad + 2, 0, angleFor (0), angleFor (3), true);
        g.setColour (juce::Colour (0xffd23a3a)); g.strokePath (red, juce::PathStrokeType (4.0f));
        g.setFont (VegaFonts::ui (8.5f, true));
        for (float m : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
        {
            const float a = angleFor (m);
            const auto p1 = pivot.getPointOnCircumference (rad, a), p2 = pivot.getPointOnCircumference (rad + 6, a), pl = pivot.getPointOnCircumference (rad + 13, a);
            g.setColour (m > 0 ? juce::Colour (0xffc02a2a) : juce::Colour (0xff1b2340));
            g.drawLine ({ p1, p2 }, 1.2f);
            g.drawText (juce::String ((int) std::abs (m)), juce::Rectangle<float> (16, 10).withCentre (pl), juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff1b2340)); g.setFont (VegaFonts::ui (11.0f, true));
        g.drawText ("VU", face.withTrimmedTop (face.getHeight() * 0.55f).withHeight (14), juce::Justification::centred);
        g.setFont (VegaFonts::ui (7.5f)); g.drawText ("Vega Productions", face.withTrimmedTop (face.getHeight() * 0.72f).withHeight (10), juce::Justification::centred);
        g.setFont (VegaFonts::ui (12.0f, true)); g.drawText ("-", face.reduced (6, 4).toNearestInt(), juce::Justification::bottomLeft);
        g.setColour (juce::Colour (0xffc02a2a)); g.drawText ("+", face.reduced (6, 4).toNearestInt(), juce::Justification::bottomRight);
        // needle (clipped to the face)
        g.saveState(); g.reduceClipRegion (face.toNearestInt());
        const float a = angleFor (vu);
        const auto tip = pivot.getPointOnCircumference (rad + 4, a);
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawLine ({ pivot.translated (2, 2), tip.translated (2, 2) }, 1.6f);
        g.setColour (juce::Colour (0xff111318)); g.drawLine ({ pivot, tip }, 1.5f);
        g.restoreState();
        g.setColour (juce::Colour (0xff111318)); g.fillEllipse (face.getRight() - 12, face.getY() + 5, 6, 6);   // peak lamp housing
        // glass
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), face.getX(), face.getY(), juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
        g.fillRoundedRectangle (face, 2.0f);
    }

    juce::Image makePlate()
    {
        juce::Image img (juce::Image::RGB, CW, CH, true);
        juce::Graphics g (img);
        // same finish as the master section's side panels
        g.setGradientFill (juce::ColourGradient (console::panelTop, 0, 0, console::panelBot, 0, (float) CH, false));
        g.fillAll();
        juce::Random rnd (7);
        for (int y = 0; y < CH; ++y)         // very fine grain
        {
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.008f + rnd.nextFloat() * 0.012f));
            g.drawHorizontalLine (y, 0, (float) CW);
        }
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.drawRect (0, 0, CW, CH, 2);
        for (auto [x, y] : { std::pair<int, int> { CW - 26, 170 }, { CW - 26, 640 } })   // screws
        {
            g.setColour (juce::Colour (0xff1e1e20)); g.fillEllipse ((float) x - 9, (float) y - 9, 18, 18);
            g.setColour (juce::Colour (0xff55555a)); g.fillEllipse ((float) x - 6, (float) y - 6, 12, 12);
            g.setColour (juce::Colour (0xff1a1b1d)); g.drawLine ((float) x - 4, (float) y, (float) x + 4, (float) y, 1.5f);
        }
        return img;
    }

    MixerView& owner; ViewState& vs;
    Canvas canvas; juce::Image plate;
    uf::SoftKey soft[4];
    uf::Encoder enc[4], channel;
    uf::Key mode { uf::Key::text, "MODE", juce::Colour (0xffffd98a), false }, page58 { uf::Key::text, "5-8", juce::Colour (0xffffd98a), false },
            chanL { uf::Key::left, {}, juce::Colour (0xffffd98a), false }, chanR { uf::Key::right, {}, juce::Colour (0xffffd98a), false },
            bankL { uf::Key::left }, bankR { uf::Key::right }, layer { uf::Key::text, juce::String::fromUTF8 ("360\xc2\xb0"), juce::Colour (0xffffd98a), false },
            scrub { uf::Key::none, {}, juce::Colour (0xffff8a7a), false },
            navUp { uf::Key::up, {}, juce::Colour (0xffffd98a), false }, navDown { uf::Key::down, {}, juce::Colour (0xffffd98a), false },
            navLeft { uf::Key::left, {}, juce::Colour (0xffffd98a), false }, navRight { uf::Key::right, {}, juce::Colour (0xffffd98a), false },
            navDot { uf::Key::dot, {}, juce::Colour (0xffffd98a), false },
            autoOff { uf::Key::left, {}, juce::Colour (0xffffd98a) }, autoRead { uf::Key::right, {}, juce::Colour (0xff7dff5a) }, autoWrite { uf::Key::write, {}, juce::Colour (0xffff6a6a) },
            cycle { uf::Key::loop, {}, juce::Colour (0xffffd98a) }, click { uf::Key::metro, {}, juce::Colour (0xffffd98a) }, drop { uf::Key::dropIn, {}, juce::Colour (0xffff8a7a) },
            shiftKey { uf::Key::shiftUp, {}, juce::Colour (0xffffd98a), false };
    uf::TransportKey rew { uf::TransportKey::rew }, ffwd { uf::TransportKey::ffwd }, stop { uf::TransportKey::stop }, play { uf::TransportKey::play }, rec { uf::TransportKey::rec };
    tp::Jog jog;
    juce::String line1 { "Vega Productions" }, line2 { "Mixer" };
    float vuRef = -18.0f, vuDb[2] { -25, -25 }, current[2] { -100, -100 }, peak[2] { -100, -100 };
    double chanAcc = 0; int holdTicks[2] {}; bool held = false;
    te::LevelMeasurer::Client client; bool masterAdded = false; te::EditPlaybackContext* masterCtx = nullptr;
};

//==============================================================================
MixerView::MixerView (ViewState& v) : vs (v)
{
    panel = std::make_unique<ControlPanel> (*this, vs);
    addAndMakeVisible (*panel);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    rebuild();
    startTimerHz (25);
}
MixerView::~MixerView() { clearAll(); }
void MixerView::clearAll() { strips.clear(); master.reset(); if (panel != nullptr) panel->forgetEdit(); lastEdit = nullptr; lastLayout.clear(); }

void MixerView::rebuild()
{
    strips.clear();
    int i = 0;
    for (auto* t : vs.session.getTracks (true)) content.addAndMakeVisible (strips.add (new Strip (*this, vs, t, i++)));
    if (master == nullptr || &vs.edit() != lastEdit) { master = std::make_unique<MasterSection> (vs); addAndMakeVisible (*master); lastEdit = &vs.edit(); }
    resized();
}

void MixerView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    const int h = r.getHeight() - 10;
    panel->setBounds (r.removeFromLeft (panel->widthForHeight (h)).withHeight (h));
    r.removeFromLeft (6);
    master->setBounds (r.removeFromRight (master->widthForHeight (h)).withHeight (h));
    r.removeFromRight (10);
    viewport.setBounds (r);
    int editH = 0;
    if (showEdit) for (auto* s : strips) editH = std::max (editH, s->editNeeded());
    int x = 0;
    for (auto* s : strips) { s->editH = editH; s->setBounds (x, 0, Strip::W, h); x += Strip::W + 4; }
    content.setSize (std::max (x, r.getWidth()), h);
    for (auto* s : strips) s->resized();
    repaint();
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121213));
    // the console body: a matte panel with a soft edge, like the hardware
    auto body = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (body.translated (0, 2.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff262628), body.getX(), body.getY(), juce::Colour (0xff1b1b1d), body.getX(), body.getBottom(), false));
    g.fillRoundedRectangle (body, 12.0f);
    g.setColour (juce::Colour (0xff3a3a3e)); g.drawRoundedRectangle (body.reduced (0.5f), 12.0f, 1.0f);
    if (master != nullptr)
    {   // divider before the master
        g.setColour (juce::Colour (0xff111113)); g.fillRect (master->getX() - 7, master->getY() + 8, 2, master->getHeight() - 16);
    }
}

void MixerView::touched (const juce::String& title, const juce::String& value) { if (panel) panel->show (title, value); }

void MixerView::pageBy (int dir)
{
    const int step = 8 * (Strip::W + 4);
    viewport.setViewPosition (juce::jlimit (0, std::max (0, content.getWidth() - viewport.getWidth()), viewport.getViewPositionX() + dir * step), 0);
    const int first = viewport.getViewPositionX() / (Strip::W + 4) + 1;
    touched ("Page", "Ch " + juce::String (first) + " - " + juce::String (first + 7));
}

void MixerView::scrollStrips (int n)
{
    const int step = Strip::W + 4;
    viewport.setViewPosition (juce::jlimit (0, std::max (0, content.getWidth() - viewport.getWidth()), viewport.getViewPositionX() + n * step), 0);
    const int first = viewport.getViewPositionX() / step + 1;
    touched ("Channel", "Ch " + juce::String (first) + " - " + juce::String (first + 7));
}

void MixerView::selectNeighbour (int dir)
{
    auto tracks = vs.session.getTracks (true);
    if (tracks.isEmpty()) return;
    int i = tracks.indexOf (vs.getSelectedTrack());
    i = juce::jlimit (0, tracks.size() - 1, i < 0 ? 0 : i + dir);
    vs.selectTrack (tracks[i]);
    if (auto* s = strips[i]) viewport.setViewPosition (juce::jlimit (0, std::max (0, content.getWidth() - viewport.getWidth()),
        s->getX() < viewport.getViewPositionX() ? s->getX() : (s->getRight() > viewport.getViewPositionX() + viewport.getWidth() ? s->getRight() - viewport.getWidth() : viewport.getViewPositionX())), 0);
    touched (tracks[i]->getName(), "Selected");
}

void MixerView::timerCallback()
{
    if (! isShowing() || master == nullptr) { if (master == nullptr && isShowing()) rebuild(); return; }
    juce::String layout;
    for (auto* t : vs.session.getTracks (true)) layout << t->itemID.toString() << ",";
    for (auto* s : strips) layout << s->layoutKey();
    if (layout != lastLayout) { lastLayout = layout; rebuild(); }
    for (auto* s : strips) s->refresh();
}

void MixerView::itemDropped (const SourceDetails& d)
{
    te::AudioTrack* t = nullptr;
    const auto sp = localPointToGlobal (d.localPosition.toInt());
    for (auto* s : strips) if (s->getScreenBounds().contains (sp)) t = s->getTrack();
    if (vs.onPluginDropped) vs.onPluginDropped (d.description, t);
}
