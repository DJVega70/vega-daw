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
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        const auto c = r.getCentre(); const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (juce::Rectangle<float> (rad * 2 + 6, rad * 2 + 6).withCentre (c.translated (0, 3)));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x, c.y - rad, juce::Colour (0xff8a8e94), c.x, c.y + rad, false));
        g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c));                   // bright outer ring
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

/** Left-hand column: display, TRANSPORT section (Marker / Nudge / Cycle / Drop / Replace / Click / Solo +
    Rew / Ffwd / Stop / Play / Rec), FADER BANK and CHANNEL, navigation cluster with zoom, SCRUB + jog wheel,
    pad-row labels. Styled after a console transport section. */
class MixerView::ControlPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 238;
    ControlPanel (MixerView& o, ViewState& v) : owner (o), vs (v)
    {
        for (auto* k : { &marker, &nudge, &cycle, &drop, &replace, &click, &solo, &scrub, &bankL, &bankR, &chanL, &chanR, &up, &down, &left, &right, &zoom })
            addAndMakeVisible (k);
        for (auto* k : { &rew, &ffwd, &stop, &play, &rec }) addAndMakeVisible (k);
        addAndMakeVisible (jog);
        // EDIT (the old Mode key): show / hide each channel's instrument, inserts and sends
        editKey.setTooltip ("Edit: show / hide each channel's instrument, inserts and sends");
        editKey.onClick = [this] { owner.showEdit = ! owner.showEdit; owner.touched ("Edit", owner.showEdit ? "Inserts shown" : "Inserts hidden"); owner.layoutChanged(); };
        addAndMakeVisible (editKey);

        marker.setTooltip ("Marker: drop a marker at the playhead (or remove the one there). Shift + rewind / fast-forward jumps between markers");
        marker.onClick = [this] { toggleMarker(); };
        nudge.setClickingTogglesState (true); nudge.setTooltip ("Nudge: rewind / fast-forward move the selected clip by one grid step");
        nudge.onClick = [this] { owner.touched ("Nudge", nudge.getToggleState() ? "On: << >> move the clip" : "Off"); };
        cycle.setTooltip ("Cycle (loop) on / off");
        cycle.onClick = [this] { auto& t = transport(); t.looping = ! t.looping.get(); if (t.looping && t.getLoopRange().getLength() <= 0s) t.setLoopRange ({ 0s, vs.session.barLength() * 4 }); owner.touched ("Cycle", t.looping ? "On" : "Off"); };
        drop.setTooltip ("Drop (punch in / out): recording only happens inside the cycle range");
        drop.onClick = [this] { auto& e = vs.edit(); e.recordingPunchInOut = ! e.recordingPunchInOut.get(); owner.touched ("Drop-in", e.recordingPunchInOut ? "Punch inside the cycle" : "Off"); };
        replace.setClickingTogglesState (true); replace.setTooltip ("Replace: MIDI recording replaces the notes underneath instead of layering");
        replace.onClick = [this] { for (auto& m : vs.edit().engine.getDeviceManager().getMidiInDevices()) m->replaceExistingClips = replace.getToggleState(); owner.touched ("MIDI record", replace.getToggleState() ? "Replace" : "Overdub"); };
        click.setTooltip ("Metronome");
        click.onClick = [this] { vs.edit().clickTrackEnabled = ! vs.edit().clickTrackEnabled.get(); owner.touched ("Click", vs.edit().clickTrackEnabled.get() ? "On" : "Off"); };
        solo.setTooltip ("Solo the selected track");
        solo.onClick = [this] { if (auto* t = vs.getSelectedTrack()) { t->setSolo (! t->isSolo (false)); owner.touched (t->getName(), t->isSolo (false) ? "Solo On" : "Solo Off"); } };

        rew.onClick = [this] { if (! held) step (-1); held = false; };
        ffwd.onClick = [this] { if (! held) step (1); held = false; };
        stop.onClick = [this] { auto& t = transport(); if (t.isPlaying() || t.isRecording()) t.stop (false, false); else t.setPosition (0s); };
        play.onClick = [this] { auto& t = transport(); if (t.isPlaying()) t.stop (false, false); else t.play (false); };
        rec.onClick = [this] { auto& t = transport(); if (t.isRecording()) t.stop (true, false); else t.record (false); };
        rew.setTooltip ("Rewind one bar (hold to keep going). Shift: previous marker"); ffwd.setTooltip ("Forward one bar (hold to keep going). Shift: next marker");
        stop.setTooltip ("Stop (press again to return to zero)");

        bankL.onClick = [this] { owner.pageBy (-1); }; bankR.onClick = [this] { owner.pageBy (1); };
        chanL.onClick = [this] { owner.scrollStrips (-1); }; chanR.onClick = [this] { owner.scrollStrips (1); };
        bankL.setTooltip ("Previous 8 channels"); bankR.setTooltip ("Next 8 channels"); chanL.setTooltip ("Shift the channels by one"); chanR.setTooltip ("Shift the channels by one");
        up.onClick = [this] { if (zoom.getToggleState()) vZoom (1); else owner.selectNeighbour (-1); };
        down.onClick = [this] { if (zoom.getToggleState()) vZoom (-1); else owner.selectNeighbour (1); };
        left.onClick = [this] { if (zoom.getToggleState()) hZoom (-1); else moveBars (-1); };
        right.onClick = [this] { if (zoom.getToggleState()) hZoom (1); else moveBars (1); };
        zoom.setClickingTogglesState (true);
        zoom.onClick = [this] { owner.touched ("Zoom", zoom.getToggleState() ? "Arrows zoom" : "Arrows navigate"); };
        zoom.setTooltip ("Zoom: the arrows zoom the arrange window instead of moving");
        up.setTooltip ("Select the previous track"); down.setTooltip ("Select the next track"); left.setTooltip ("Playhead back one bar"); right.setTooltip ("Playhead forward one bar");
        scrub.setClickingTogglesState (true); scrub.setTooltip ("Scrub: the jog wheel plays the audio as you turn it");
        scrub.onClick = [this] { owner.touched ("Jog wheel", scrub.getToggleState() ? "Scrub" : "Move playhead"); };
        jog.onTurn = [this] (double turns) { jogTurn (turns); };
        jog.onTouch = [this] (bool down) { if (scrub.getToggleState()) transport().setUserDragging (down); };
        startTimerHz (20);
    }
    void show (const juce::String& a, const juce::String& b) { line1 = a; line2 = b; repaint (oled); }

    void paint (juce::Graphics& g) override
    {
        // logo
        g.setColour (juce::Colour (0xffe6e6e6));
        g.setFont (VegaFonts::ui (17.0f, true)); g.drawText ("VEGA", 16, 12, 60, 22, juce::Justification::left);
        g.setFont (VegaFonts::ui (17.0f, false)); g.drawText ("MIXER", 66, 12, 70, 22, juce::Justification::left);
        g.setFont (VegaFonts::ui (9.0f, true)); g.drawText ("XL", 124, 10, 20, 12, juce::Justification::left);
        // display
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (oled.toFloat().expanded (4.0f), 5.0f);
        g.setColour (juce::Colour (0xff333336)); g.drawRoundedRectangle (oled.toFloat().expanded (4.0f), 5.0f, 1.0f);
        g.setColour (juce::Colour (0xff0b0b0d)); g.fillRect (oled);
        g.setColour (juce::Colours::white.withAlpha (0.82f)); g.setFont (VegaFonts::mono (12.0f));
        g.drawFittedText (line1, oled.withHeight (oled.getHeight() / 2).withTrimmedTop (5), juce::Justification::centred, 1);
        g.setColour (juce::Colours::white); g.setFont (VegaFonts::mono (15.5f));
        g.drawFittedText (line2, oled.withTrimmedTop (oled.getHeight() / 2 - 2).withTrimmedBottom (5), juce::Justification::centred, 1);

        auto section = [&g] (juce::Rectangle<int> r, juce::Colour fill)
        {
            if (r.isEmpty()) return;
            g.setColour (fill); g.fillRoundedRectangle (r.toFloat(), 7.0f);
            g.setColour (juce::Colour (0xff3a3c40)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f);
        };
        auto caption = [&g] (const juce::String& t, juce::Rectangle<int> under, float size = 8.6f)
        {
            g.setFont (VegaFonts::ui (size, true)); g.setColour (juce::Colour (0xffe8e8ea));
            g.drawText (t, under.getCentreX() - 34, under.getBottom() + 2, 68, 12, juce::Justification::centred);
        };
        // TRANSPORT
        section (transportBox, juce::Colour (0xff141517));
        g.setFont (VegaFonts::ui (11.0f, true)); g.setColour (juce::Colour (0xffe8e8ea));
        g.drawText ("TRANSPORT", transportBox.withHeight (18).translated (0, 3), juce::Justification::centred);
        caption ("MARKER", marker.getBounds()); caption ("NUDGE", nudge.getBounds()); caption ("CYCLE", cycle.getBounds()); caption ("DROP", drop.getBounds());
        caption ("REPLACE", replace.getBounds()); caption ("CLICK", click.getBounds()); caption ("SOLO", solo.getBounds()); caption ("EDIT", editKey.getBounds());
        // FADER BANK / CHANNEL
        section (bankBox, juce::Colour (0xff1e1f22));
        if (! bankBox.isEmpty())
        {
            g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (juce::Colour (0xffe8e8ea));
            g.drawText ("FADER BANK", bankBox.withHeight (16).translated (0, 3), juce::Justification::centred);
            g.drawText ("CHANNEL", juce::Rectangle<int> (bankBox.getX(), chanL.getY() - 16, bankBox.getWidth(), 14), juce::Justification::centred);
            for (auto* k : { &bankL, &chanL }) { juce::Path t; const float y = k->getBounds().toFloat().getCentreY(), x = (float) k->getX() - 9; t.addTriangle (x, y, x + 6, y - 5, x + 6, y + 5); g.setColour (juce::Colour (0xffd0d0d4)); g.fillPath (t); }
            for (auto* k : { &bankR, &chanR }) { juce::Path t; const float y = k->getBounds().toFloat().getCentreY(), x = (float) k->getRight() + 9; t.addTriangle (x, y, x - 6, y - 5, x - 6, y + 5); g.setColour (juce::Colour (0xffd0d0d4)); g.fillPath (t); }
        }
        // navigation cluster ring + direction marks
        if (up.isVisible())
        {
            const auto c = zoom.getBounds().toFloat().getCentre(); const float rr = (float) (right.getRight() - left.getX()) * 0.47f;
            g.setColour (juce::Colour (0xffcfd0d4).withAlpha (0.8f)); g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre (c), 1.4f);
            auto mark = [&g] (float x, float y, float dx, float dy)
            {
                juce::Path t; t.addTriangle (x + dx * 6, y + dy * 6, x - dy * 5, y + dx * 5, x + dy * 5, y - dx * 5);
                g.setColour (juce::Colour (0xffd0d0d4)); g.fillPath (t);
            };
            mark (c.x, (float) up.getY() - 9, 0, -1); mark (c.x, (float) down.getBottom() + 9, 0, 1);
            mark ((float) left.getX() - 9, c.y, -1, 0); mark ((float) right.getRight() + 9, c.y, 1, 0);
            g.setColour (juce::Colour (0xffd0d0d4)); g.drawEllipse ((float) zoom.getRight() + 4, (float) zoom.getY() - 16, 11, 11, 1.2f);
            g.drawText ("+", zoom.getRight() + 4, zoom.getY() - 16, 11, 11, juce::Justification::centred);
        }
        if (scrub.isVisible()) caption ("SCRUB", scrub.getBounds());
        // pad row labels, aligned with the strips' pad rows
        const int h = getHeight();
        for (int row = 0; row < 2; ++row)
        {
            auto r = juce::Rectangle<int> (8, mixlayout::padY (h, row), W - 16, mixlayout::padH);
            g.setColour (juce::Colour (0xff18181a)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xff2c2c30)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
            const juce::String a = row == 0 ? "Solo" : "Mute", b = row == 0 ? "Arm" : "Select";
            g.setFont (VegaFonts::ui (12.5f, ! owner.shift)); g.setColour (owner.shift ? VegaColours::textDim : juce::Colours::white);
            auto t = r.reduced (10, 0);
            const int wa = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), a);
            g.drawText (a, t.removeFromLeft (wa + 2), juce::Justification::centredLeft);
            g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (12.5f)); g.drawText (" / ", t.removeFromLeft (16), juce::Justification::centred);
            g.setFont (VegaFonts::ui (12.5f, owner.shift)); g.setColour (owner.shift ? juce::Colours::white : VegaColours::textDim);
            g.drawText (b, t, juce::Justification::centredLeft);
        }
        shiftArea = { 8, mixlayout::padY (h, 0) - 30, W - 16, 22 };
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (owner.shift ? juce::Colour (0xffff7a8a) : VegaColours::textDim);
        g.drawText (owner.shift ? "SHIFT ON: pads = Arm / Select   (click)" : "click here for Shift (Arm / Select)", shiftArea, juce::Justification::centred);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (shiftArea.contains (e.getPosition()))
        {
            owner.shift = ! owner.shift;
            owner.touched ("Pads", owner.shift ? "Arm / Select" : "Solo / Mute");
            repaint();
        }
    }
    void resized() override
    {
        const int limit = mixlayout::padY (getHeight(), 0) - 34;    // keep clear of the pad labels / shift line
        oled = { 18, 44, W - 36, 58 };
        transportBox = { 6, 116, W - 12, 158 };
        // small keys: 4 + 3
        const int ks = 26;
        const int row1[] { 0, 1, 2, 3 };
        juce::Button* r1[] { &marker, &nudge, &cycle, &drop };
        for (int i : row1) r1[i]->setBounds (transportBox.getX() + 18 + i * 54, transportBox.getY() + 24, ks, ks);
        juce::Button* r2[] { &replace, &click, &solo, &editKey };
        for (int i = 0; i < 4; ++i) r2[i]->setBounds (transportBox.getX() + 18 + i * 54, transportBox.getY() + 66, ks, ks);
        juce::Button* big[] { &rew, &ffwd, &stop, &play, &rec };
        const int bw = (transportBox.getWidth() - 16) / 5;
        for (int i = 0; i < 5; ++i) big[i]->setBounds (transportBox.getX() + 8 + i * bw, transportBox.getY() + 112, bw - 4, 36);

        bankBox = { 6, 284, W - 12, 108 };
        const bool fitsBank = bankBox.getBottom() < limit;
        for (auto* k : { &bankL, &bankR, &chanL, &chanR }) k->setVisible (fitsBank);
        if (fitsBank)
        {
            bankL.setBounds (bankBox.getCentreX() - 58, bankBox.getY() + 22, 50, 30); bankR.setBounds (bankBox.getCentreX() + 8, bankBox.getY() + 22, 50, 30);
            chanL.setBounds (bankBox.getCentreX() - 58, bankBox.getY() + 72, 50, 30); chanR.setBounds (bankBox.getCentreX() + 8, bankBox.getY() + 72, 50, 30);
        }
        else bankBox = {};

        // navigation cluster (left) + scrub / jog (right)
        const int navTop = 404, navC = navTop + 64, navX = 54;
        const bool fitsNav = navC + 66 < limit;
        for (auto* k : { &up, &down, &left, &right, &zoom }) k->setVisible (fitsNav);
        jog.setVisible (fitsNav); scrub.setVisible (fitsNav);
        if (fitsNav)
        {
            zoom.setBounds (navX - 13, navC - 13, 26, 26);
            up.setBounds (navX - 13, navC - 44, 26, 28); down.setBounds (navX - 13, navC + 16, 26, 28);
            left.setBounds (navX - 44, navC - 13, 28, 26); right.setBounds (navX + 16, navC - 13, 28, 26);
            scrub.setBounds (W - 38, navTop - 8, 26, 26);
            jog.setBounds (W - 124, navTop + 22, 114, 114);
        }
    }
    void scrollChanged() {}

private:
    te::TransportControl& transport() { return vs.edit().getTransport(); }
    void step (int dir)
    {
        auto& t = transport();
        if (juce::ModifierKeys::currentModifiers.isShiftDown())
        {   // jump between markers
            auto& mm = vs.edit().getMarkerManager();
            auto* m = dir > 0 ? mm.getNextMarker (t.getPosition() + te::TimeDuration::fromSeconds (0.001)) : mm.getPrevMarker (t.getPosition() - te::TimeDuration::fromSeconds (0.001));
            if (m != nullptr) { t.setPosition (m->getPosition().getStart()); owner.touched ("Marker", m->getName()); }
            return;
        }
        if (nudge.getToggleState())
        {
            if (auto* c = vs.getSelectedClip())
            {
                const double grid = vs.snapDiv > 0 ? 1.0 / vs.snapDiv : 1.0;
                const double b = std::max (0.0, vs.timeToBeats (c->getPosition().getStart()) + dir * grid);
                c->setStart (vs.beatsToTime (b), false, true);
                owner.touched ("Nudge " + c->getName(), dawui::formatBarsBeats (vs.edit(), c->getPosition().getStart()));
            }
            else owner.touched ("Nudge", "Select a clip first");
            return;
        }
        moveBars (dir);
    }
    void moveBars (int dir)
    {
        auto& t = transport();
        const double bar = vs.session.barLength().inSeconds();
        const double now = t.getPosition().inSeconds();
        const double target = dir > 0 ? (std::floor (now / bar + 1e-6) + 1) * bar : std::max (0.0, (std::ceil (now / bar - 1e-6) - 1) * bar);
        t.setPosition (te::TimePosition::fromSeconds (target));
        owner.touched ("Position", dawui::formatBarsBeats (vs.edit(), t.getPosition()));
    }
    void hZoom (int dir) { vs.pixelsPerBeat = juce::jlimit (2.0, 400.0, vs.pixelsPerBeat * (dir > 0 ? 1.25 : 0.8)); owner.touched ("Zoom", juce::String (vs.pixelsPerBeat, 0) + " px / beat"); }
    void vZoom (int dir)
    {
        for (auto* t : vs.session.getTracks (true))
            t->state.setProperty ("vegaHeight", juce::jlimit (44, 260, (int) t->state.getProperty ("vegaHeight", 84) + dir * 16), nullptr);
        owner.touched ("Track height", dir > 0 ? "Taller" : "Shorter");
    }
    void toggleMarker()
    {
        auto& mm = vs.edit().getMarkerManager();
        const auto now = transport().getPosition();
        for (auto& m : mm.getMarkers())
            if (std::abs ((m->getPosition().getStart() - now).inSeconds()) < 0.05) { m->removeFromParent(); owner.touched ("Marker", "Removed"); return; }
        int next = 1; for (auto& m : mm.getMarkers()) next = std::max (next, m->getMarkerID() + 1);
        if (auto m = mm.createMarker (next, now, vs.session.barLength(), nullptr)) owner.touched ("Marker " + juce::String (next), dawui::formatBarsBeats (vs.edit(), now));
    }
    void jogTurn (double turns)
    {
        auto& t = transport();
        if (scrub.getToggleState()) { te::scrub (t, turns * 40.0); return; }
        const double beats = std::max (0.0, vs.timeToBeats (t.getPosition()) + turns * 4.0);   // one turn = one 4/4 bar
        t.setPosition (vs.beatsToTime (beats));
        owner.touched ("Jog", dawui::formatBarsBeats (vs.edit(), t.getPosition()));
    }
    void timerCallback() override
    {
        auto& t = transport();
        auto set = [] (juce::Button& b, bool on) { if (b.getToggleState() != on) b.setToggleState (on, juce::dontSendNotification); };
        set (rec, t.isRecording()); set (play, t.isPlaying()); set (stop, ! t.isPlaying() && ! t.isRecording());
        set (cycle, t.looping.get()); set (drop, vs.edit().recordingPunchInOut.get()); set (click, vs.edit().clickTrackEnabled.get());
        auto* sel = vs.getSelectedTrack(); set (solo, sel != nullptr && sel->isSolo (false));
        set (editKey, owner.showEdit);
        // hold rewind / fast-forward to keep moving
        for (auto [k, dir] : { std::pair<juce::Button*, int> { &rew, -1 }, { &ffwd, 1 } })
        {
            if (k->isDown())
            {
                if (++holdTicks[dir > 0] > 6)
                {
                    held = true;
                    const double beats = std::max (0.0, vs.timeToBeats (t.getPosition()) + dir * 1.0);
                    t.setPosition (vs.beatsToTime (beats));
                    owner.touched (dir > 0 ? "Fast forward" : "Rewind", dawui::formatBarsBeats (vs.edit(), t.getPosition()));
                }
            }
            else holdTicks[dir > 0] = 0;
        }
    }
    MixerView& owner; ViewState& vs;
    tp::Key marker, nudge, cycle, drop, replace, click, solo, scrub, editKey { tp::Key::square, juce::Colour (0xff8ff3f0) };
    tp::Key bankL { tp::Key::pill }, bankR { tp::Key::pill }, chanL { tp::Key::pill }, chanR { tp::Key::pill };
    tp::Key up, down, left, right, zoom { tp::Key::square, juce::Colour (0xff8ff3f0) };
    tp::BigKey rew { tp::BigKey::rew }, ffwd { tp::BigKey::ffwd }, stop { tp::BigKey::stop }, play { tp::BigKey::play }, rec { tp::BigKey::rec };
    tp::Jog jog;
    juce::Rectangle<int> oled, transportBox, bankBox, shiftArea;
    juce::String line1 { "Mode Select" }, line2 { "Mixer" };
    int holdTicks[2] {}; bool held = false;
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
void MixerView::clearAll() { strips.clear(); master.reset(); lastEdit = nullptr; lastLayout.clear(); }

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
    panel->setBounds (r.removeFromLeft (ControlPanel::W).withHeight (h));
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
