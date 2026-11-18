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
    Knob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
    {
        setPopupDisplayEnabled (true, true, nullptr);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    void paint (juce::Graphics& g) override
    {
        const float d = std::min ((float) getWidth(), (float) getHeight()) - 6.0f;
        auto r = juce::Rectangle<float> (d, d).withCentre (getLocalBounds().toFloat().getCentre().translated (0, -1.0f));
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        for (int i = 4; i > 0; --i) { g.setColour (juce::Colours::black.withAlpha (0.10f * alpha)); g.fillEllipse (r.translated (0, (float) i * 1.4f).expanded ((float) i * 0.7f)); }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff57575b).withAlpha (alpha), r.getX(), r.getY(), juce::Colour (0xff2a2a2d).withAlpha (alpha), r.getRight(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.07f * alpha)); g.fillEllipse (r.reduced (d * 0.12f).translated (-d * 0.05f, -d * 0.07f));
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (r, 1.0f);
        if (! isEnabled()) return;
        const auto rp = getRotaryParameters();
        const float a = rp.startAngleRadians + (float) valueToProportionOfLength (getValue()) * (rp.endAngleRadians - rp.startAngleRadians);
        const auto c = r.getCentre();
        juce::Point<float> p0 (c.x + std::sin (a) * d * 0.16f, c.y - std::cos (a) * d * 0.16f), p1 (c.x + std::sin (a) * d * 0.43f, c.y - std::cos (a) * d * 0.43f);
        g.setColour (juce::Colours::white.withAlpha (0.88f)); g.drawLine ({ p0, p1 }, 2.2f);
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

//==============================================================================
namespace mixlayout
{
constexpr int padH = 36, padGap = 10, bottom = 8, knobRowH = 64, headerH = 30, numKnobs = 4;
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
        // LEDs + labels under the knobs: EQ LEDs go dark when a band is killed, the filter LED lights when it's engaged
        static const char* labels[] { "HI", "MID", "LOW", "FILTER" };
        for (int i = 0; i < mixlayout::numKnobs; ++i)
        {
            if (! knobs[i].isVisible()) continue;
            auto kb = knobs[i].getBounds().toFloat();
            const double v = knobs[i].getValue();
            const bool lit = i < 3 ? v > VegaDJEQPlugin::killDb + 0.05 : std::abs (v) >= 0.03;
            xl::drawLed (g, juce::Rectangle<float> (16.0f, 4.0f).withCentre ({ kb.getCentreX(), kb.getBottom() + 3.0f }), xl::rowLed[i], lit ? 1.0f : 0.0f);
            g.setFont (VegaFonts::ui (8.5f, true));
            const bool atDefault = i < 3 ? std::abs (v) < 0.05 : std::abs (v) < 0.03;
            g.setColour (atDefault ? VegaColours::textDim : xl::rowLed[i].brighter (0.3f));
            const juce::String lab = atDefault ? juce::String (labels[i]) : juce::String (labels[i]) + "  " + knobs[i].getTextFromValue (v).replace (" dB", "");
            g.drawText (lab, 2, (int) kb.getBottom() + 7, W - 4, 11, juce::Justification::centred, true);
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
        // meter (left of the fader)
        auto mr = meterArea.toFloat();
        g.setColour (xl::slot); g.fillRoundedRectangle (mr, 2.0f);
        for (int c = 0; c < 2; ++c)
        {
            const float lv = juce::jlimit (0.0f, 1.0f, (level[c] + 60.0f) / 66.0f);
            auto bar = juce::Rectangle<float> (mr.getX() + 1.5f + c * (mr.getWidth() - 3.0f) / 2, mr.getY() + 2, (mr.getWidth() - 4.0f) / 2, mr.getHeight() - 4);
            g.setGradientFill (juce::ColourGradient (xl::rowLed[2], 0, bar.getBottom(), juce::Colour (0xffff3a5a), 0, bar.getY(), false));
            g.fillRect (bar.withTop (bar.getBottom() - lv * bar.getHeight()));
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
            knobs[i].setBounds (W / 2 - 23, y + (i * knobRowH), 46, 42);
        }
        y += numKnobs * knobRowH;
        pan.setBounds (10, y, W - 20, 20); y += 26;
        const int fBottom = (track != nullptr ? padY (h, 0) - 20 : h - bottom - 20) - 16;
        fader.setBounds (W / 2 - 36, y, 60, std::max (60, fBottom - y));
        meterArea = { fader.getX() - 6, y + 10, 8, std::max (40, fader.getHeight() - 20) };
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
/** Left-hand control column: display, Page, Track, Record / Play, Shift / Mode, pad-row labels */
class MixerView::ControlPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 178;
    ControlPanel (MixerView& o, ViewState& v) : owner (o), vs (v)
    {
        for (auto* k : { &pageUp, &pageDown, &trackL, &trackR, &rec, &play, &shift, &mode }) addAndMakeVisible (k);
        pageUp.onClick = [this] { owner.pageBy (-1); };   pageDown.onClick = [this] { owner.pageBy (1); };
        trackL.onClick = [this] { owner.selectNeighbour (-1); }; trackR.onClick = [this] { owner.selectNeighbour (1); };
        rec.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isRecording()) t.stop (true, false); else t.record (false); };
        play.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isPlaying()) t.stop (false, false); else t.play (false); };
        shift.setClickingTogglesState (true); shift.on = juce::Colour (0xffff5577);
        shift.onClick = [this] { owner.shift = shift.getToggleState(); owner.touched ("Pads", owner.shift ? "Arm / Select" : "Solo / Mute"); repaint(); };
        mode.setClickingTogglesState (true); mode.setToggleState (owner.showEdit, juce::dontSendNotification); mode.on = VegaColours::accent;
        mode.onClick = [this] { owner.showEdit = mode.getToggleState(); owner.touched ("Mode", owner.showEdit ? "Edit: inserts shown" : "Mix: inserts hidden"); owner.layoutChanged(); };
        pageUp.setTooltip ("Previous 8 channels"); pageDown.setTooltip ("Next 8 channels");
        trackL.setTooltip ("Select previous track"); trackR.setTooltip ("Select next track");
        shift.setTooltip ("Shift: pads become Record Arm (top) and Select (bottom)"); mode.setTooltip ("Show / hide each channel's instrument and insert section");
        startTimerHz (10);
    }
    void show (const juce::String& a, const juce::String& b) { line1 = a; line2 = b; repaint (oled); }
    void paint (juce::Graphics& g) override
    {
        // logo
        g.setColour (juce::Colour (0xffe6e6e6));
        g.setFont (VegaFonts::ui (17.0f, true)); g.drawText ("VEGA", 16, 14, 60, 22, juce::Justification::left);
        g.setFont (VegaFonts::ui (17.0f, false)); g.drawText ("MIXER", 66, 14, 70, 22, juce::Justification::left);
        g.setFont (VegaFonts::ui (9.0f, true)); g.drawText ("XL", 124, 12, 20, 12, juce::Justification::left);
        // display
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (oled.toFloat().expanded (4.0f), 5.0f);
        g.setColour (juce::Colour (0xff333336)); g.drawRoundedRectangle (oled.toFloat().expanded (4.0f), 5.0f, 1.0f);
        g.setColour (juce::Colour (0xff0b0b0d)); g.fillRect (oled);
        g.setColour (juce::Colours::white.withAlpha (0.82f)); g.setFont (VegaFonts::mono (12.0f));
        g.drawFittedText (line1, oled.withHeight (oled.getHeight() / 2).withTrimmedTop (6), juce::Justification::centred, 1);
        g.setColour (juce::Colours::white); g.setFont (VegaFonts::mono (15.5f));
        g.drawFittedText (line2, oled.withTrimmedTop (oled.getHeight() / 2 - 2).withTrimmedBottom (6), juce::Justification::centred, 1);
        // key boxes + captions
        for (auto* b : { &pageBox, &trackBox, &transportBox, &shiftBox }) if (! b->isEmpty()) xl::drawKeyBox (g, b->toFloat());
        g.setFont (VegaFonts::ui (12.0f)); g.setColour (juce::Colour (0xffdcdcdc));
        g.drawText ("Page", pageBox.withY (pageBox.getY() - 22).withHeight (18), juce::Justification::centred);
        g.drawText ("Track", trackBox.withY (trackBox.getY() - 22).withHeight (18), juce::Justification::centred);
        if (! shiftBox.isEmpty()) g.drawText ("Edit", shiftBox.withY (shiftBox.getBottom() + 4).withHeight (18), juce::Justification::centred);
        g.setColour (juce::Colour (0xff111113));
        g.fillRect (pageBox.getCentreX() - 1, pageBox.getY() + 8, 2, pageBox.getHeight() - 16);
        g.fillRect (trackBox.getCentreX() - 1, trackBox.getY() + 8, 2, trackBox.getHeight() - 16);
        if (! transportBox.isEmpty()) g.fillRect (transportBox.getX() + 10, transportBox.getCentreY() - 1, transportBox.getWidth() - 20, 2);
        if (! shiftBox.isEmpty()) g.fillRect (shiftBox.getX() + 10, shiftBox.getCentreY() - 1, shiftBox.getWidth() - 20, 2);
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
    }
    void resized() override
    {
        oled = { 22, 52, W - 44, 64 };
        pageBox = { 26, 156, W - 52, 46 };
        trackBox = { 26, 232, W - 52, 46 };
        transportBox = { 26, 300, W - 52, 92 };
        shiftBox = { 26, 408, W - 52, 92 };
        pageUp.setBounds (pageBox.withWidth (pageBox.getWidth() / 2)); pageDown.setBounds (pageBox.withTrimmedLeft (pageBox.getWidth() / 2));
        trackL.setBounds (trackBox.withWidth (trackBox.getWidth() / 2)); trackR.setBounds (trackBox.withTrimmedLeft (trackBox.getWidth() / 2));
        rec.setBounds (transportBox.withHeight (transportBox.getHeight() / 2)); play.setBounds (transportBox.withTrimmedTop (transportBox.getHeight() / 2));
        shift.setBounds (shiftBox.withHeight (shiftBox.getHeight() / 2)); mode.setBounds (shiftBox.withTrimmedTop (shiftBox.getHeight() / 2));
        // hide the lower keys if the window is short so they never overlap the pad labels
        const int limit = mixlayout::padY (getHeight(), 0) - 26;
        const bool fitsShift = shiftBox.getBottom() + 22 < limit, fitsTransport = transportBox.getBottom() < limit;
        shift.setVisible (fitsShift); mode.setVisible (fitsShift); rec.setVisible (fitsTransport); play.setVisible (fitsTransport);
        if (! fitsShift) shiftBox = {};
        if (! fitsTransport) transportBox = {};
    }
private:
    void timerCallback() override
    {
        auto& t = vs.edit().getTransport();
        if (rec.getToggleState() != t.isRecording()) rec.setToggleState (t.isRecording(), juce::dontSendNotification);
        if (play.getToggleState() != t.isPlaying()) play.setToggleState (t.isPlaying(), juce::dontSendNotification);
        if (mode.getToggleState() != owner.showEdit) mode.setToggleState (owner.showEdit, juce::dontSendNotification);
    }
    MixerView& owner; ViewState& vs;
    xl::Key pageUp { xl::Key::up }, pageDown { xl::Key::down }, trackL { xl::Key::left }, trackR { xl::Key::right },
            rec { xl::Key::rec }, play { xl::Key::play }, shift { xl::Key::text, "Shift" }, mode { xl::Key::text, "Mode" };
    juce::Rectangle<int> oled, pageBox, trackBox, transportBox, shiftBox;
    juce::String line1 { "Mode Select" }, line2 { "Mixer" };
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
    if (master == nullptr || &vs.edit() != lastEdit) { master = std::make_unique<Strip> (*this, vs, nullptr, 0); addAndMakeVisible (*master); lastEdit = &vs.edit(); }
    resized();
}

void MixerView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    const int h = r.getHeight() - 10;
    panel->setBounds (r.removeFromLeft (ControlPanel::W).withHeight (h));
    r.removeFromLeft (6);
    master->setBounds (r.removeFromRight (Strip::W).withHeight (h));
    r.removeFromRight (10);
    viewport.setBounds (r);
    int editH = 0;
    if (showEdit) for (auto* s : strips) editH = std::max (editH, s->editNeeded());
    int x = 0;
    for (auto* s : strips) { s->editH = editH; s->setBounds (x, 0, Strip::W, h); x += Strip::W + 4; }
    master->editH = editH;
    master->resized();
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
    master->refresh();
}

void MixerView::itemDropped (const SourceDetails& d)
{
    te::AudioTrack* t = nullptr;
    const auto sp = localPointToGlobal (d.localPosition.toInt());
    for (auto* s : strips) if (s->getScreenBounds().contains (sp)) t = s->getTrack();
    if (vs.onPluginDropped) vs.onPluginDropped (d.description, t);
}
