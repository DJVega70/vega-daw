#include "MixerView.h"

class MixerView::Strip : public juce::Component
{
public:
    static constexpr int W = 128;
    Strip (ViewState& v, te::AudioTrack* t) : vs (v), track (t)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical); fader.setRange (-60.0, 6.0, 0.1); fader.setSkewFactorFromMidPoint (-10.0);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18); fader.setTextValueSuffix (" dB");
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.onValueChange = [this] { if (auto vp = volume()) vp->setVolumeDb ((float) fader.getValue()); };
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); pan.setRange (-1.0, 1.0, 0.01); pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.onValueChange = [this] { if (auto vp = volume()) vp->setPan ((float) pan.getValue()); };
        addAndMakeVisible (fader); addAndMakeVisible (pan);
        if (track != nullptr)
        {
            for (auto* b : { &mute, &solo }) { b->setClickingTogglesState (false); addAndMakeVisible (b); }
            mute.setButtonText ("M"); solo.setButtonText ("S");
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0a030)); solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff48e1e8));
            mute.onClick = [this] { track->setMute (! track->isMuted (false)); };
            solo.onClick = [this] { track->setSolo (! track->isSolo (false)); };
            output.onChange = [this] { const int i = output.getSelectedItemIndex(); vs.session.routeToBus (*track, i <= 0 ? nullptr : outBuses[i - 1]); };
            addAndMakeVisible (output);
            if (DawSession::kindOf (*track) == DawSession::TrackKind::midi)
            {
                instrument.onClick = [this] { chooseInstrument(); };
                addAndMakeVisible (instrument);
            }
            addFx.setButtonText ("+ FX"); addFx.onClick = [this] { chooseFx(); }; addAndMakeVisible (addFx);
            addSend.setButtonText ("+ SEND"); addSend.onClick = [this] { chooseSend(); }; addAndMakeVisible (addSend);
            if (auto* lm = track->getLevelMeterPlugin()) { lm->measurer.addClient (client); measurer = &lm->measurer; }
        }
        rebuildSlots();
    }
    ~Strip() override { if (measurer) measurer->removeClient (client); if (masterAdded && masterCtx) masterCtx->masterLevels.removeClient (client); }

    te::VolumeAndPanPlugin* volume() { return track != nullptr ? track->getVolumePlugin() : vs.edit().getMasterVolumePlugin().get(); }

    void rebuildSlots()
    {
        slots.clear(); sends.clear();
        if (track == nullptr) { resized(); return; }
        for (auto* p : vs.session.getInsertPlugins (*track))
        {
            if (p->isSynth() && DawSession::kindOf (*track) == DawSession::TrackKind::midi && track->pluginList.indexOf (p) == 0) continue;
            auto* b = slots.add (new juce::TextButton (p->getName()));
            b->setColour (juce::TextButton::buttonColourId, p->isEnabled() ? juce::Colour (0xff2b3334) : juce::Colour (0xff262626));
            b->setColour (juce::TextButton::textColourOffId, p->isEnabled() ? VegaColours::text : VegaColours::textDim);
            b->onClick = [this, pp = te::Plugin::Ptr (p), b]
            {
                if (juce::ModifierKeys::currentModifiers.isPopupMenu()) slotMenu (pp, b); else pp->showWindowExplicitly();
            };
            b->addMouseListener (this, false);
            addAndMakeVisible (b);
        }
        for (auto* bus : vs.session.getBuses())
            if (auto* s = vs.session.findSend (*track, *bus))
            {
                auto* row = sends.add (new SendRow (*s, bus->getName()));
                addAndMakeVisible (row->knob); addAndMakeVisible (row->label);
            }
        if (DawSession::kindOf (*track) == DawSession::TrackKind::midi)
        {
            auto* first = track->pluginList[0];
            instrument.setButtonText (first != nullptr && first->isSynth() ? first->getName() : "Choose instrument...");
        }
        // output list
        outBuses.clear(); output.clear (juce::dontSendNotification);
        output.addItem ("Master", 1);
        for (auto* b : vs.session.getBuses()) if (b != track) { outBuses.add (b); output.addItem ("> " + b->getName(), outBuses.size() + 1); }
        auto* cur = vs.session.getOutputBus (*track);
        output.setSelectedItemIndex (cur == nullptr ? 0 : outBuses.indexOf (cur) + 1, juce::dontSendNotification);
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
        if (track != nullptr) { mute.setToggleState (track->isMuted (false), juce::dontSendNotification); solo.setToggleState (track->isSolo (false), juce::dontSendNotification); }
        for (auto* s : sends) if (! s->knob.isMouseButtonDown()) s->knob.setValue (s->send.getGainDb(), juce::dontSendNotification);
        // meter
        auto* m = measurer;
        if (track == nullptr) if (auto* ctx = vs.edit().getCurrentPlaybackContext()) { if (! masterAdded) { ctx->masterLevels.addClient (client); masterAdded = true; masterCtx = ctx; } m = &ctx->masterLevels; }
        if (m != nullptr)
            for (int c = 0; c < 2; ++c) { const float db = client.getAndClearAudioLevel (c).dB; level[c] = std::max (db, level[c] - 1.5f); }
        repaint (meterArea);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (track == nullptr ? 0xff252321 : 0xff222222)); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 4.0f);
        const auto col = track != nullptr ? DawSession::trackColour (*track) : VegaColours::gold;
        g.setColour (col); g.fillRect (4, 4, getWidth() - 8, 4);
        g.setColour (VegaColours::text); g.setFont (VegaFonts::ui (12.0f, true));
        g.drawText (track != nullptr ? track->getName() : juce::String ("MASTER"), 6, 10, getWidth() - 12, 18, juce::Justification::centred, true);
        g.setFont (VegaFonts::ui (9.0f, true)); g.setColour (VegaColours::textDim);
        if (track != nullptr)
        {
            g.drawText ("INSERTS", 6, insertsY - 13, 60, 12, juce::Justification::left);
            g.drawText ("SENDS", 6, sendsY - 13, 60, 12, juce::Justification::left);
        }
        // meter
        auto r = meterArea.toFloat();
        g.setColour (juce::Colour (0xff111111)); g.fillRect (r);
        for (int c = 0; c < 2; ++c)
        {
            const float lv = juce::jlimit (0.0f, 1.0f, (level[c] + 60.0f) / 66.0f);
            auto bar = juce::Rectangle<float> (r.getX() + 2 + c * (r.getWidth() - 4) / 2, r.getY() + 2, (r.getWidth() - 6) / 2, r.getHeight() - 4);
            auto fill = bar.withTop (bar.getBottom() - lv * bar.getHeight());
            g.setGradientFill (juce::ColourGradient (VegaColours::accent, 0, bar.getBottom(), juce::Colour (0xffff4040), 0, bar.getY(), false));
            g.fillRect (fill);
        }
    }
    void resized() override
    {
        int y = 32;
        if (instrument.isVisible()) { instrument.setBounds (6, y, getWidth() - 12, 22); y += 28; }
        y += 12; insertsY = y;
        for (auto* s : slots) { s->setBounds (6, y, getWidth() - 12, 20); y += 22; }
        if (track != nullptr) { addFx.setBounds (6, y, getWidth() - 12, 20); y += 34; }
        sendsY = y;
        for (auto* s : sends) { s->label.setBounds (6, y, getWidth() - 50, 18); s->knob.setBounds (getWidth() - 44, y - 6, 38, 30); y += 30; }
        if (track != nullptr) { addSend.setBounds (6, y, getWidth() - 12, 20); y += 28; output.setBounds (6, y, getWidth() - 12, 22); y += 28; }
        const int bottom = getHeight() - 6;
        pan.setBounds (getWidth() / 2 - 20, std::max (y, bottom - 330), 40, 40);
        const int fy = pan.getBottom() + 4;
        if (track != nullptr) { mute.setBounds (8, bottom - 22, (getWidth() - 20) / 2, 22); solo.setBounds (getWidth() / 2 + 2, bottom - 22, (getWidth() - 20) / 2, 22); }
        const int fbottom = track != nullptr ? bottom - 28 : bottom;
        fader.setBounds (6, fy, getWidth() - 44, fbottom - fy);
        meterArea = { getWidth() - 34, fy + 4, 26, fbottom - fy - 26 };
    }
    te::AudioTrack::Ptr track;
private:
    struct SendRow
    {
        SendRow (te::AuxSendPlugin& s, const juce::String& busName) : send (s)
        {
            label.setText ("> " + busName, juce::dontSendNotification); label.setFont (VegaFonts::ui (10.5f));
            label.setColour (juce::Label::textColourId, VegaColours::text);
            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); knob.setRange (-60.0, 6.0, 0.1); knob.setSkewFactorFromMidPoint (-12.0);
            knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); knob.setPopupDisplayEnabled (true, true, nullptr);
            knob.setTextValueSuffix (" dB"); knob.setDoubleClickReturnValue (true, 0.0);
            knob.setValue (s.getGainDb(), juce::dontSendNotification);
            knob.onValueChange = [this] { send.setGainDb ((float) knob.getValue()); };
        }
        te::AuxSendPlugin& send; juce::Slider knob; juce::Label label;
    };
    void chooseInstrument()
    {
        dawui::choosePlugin (vs.edit().engine, true, &instrument, [this, t = track] (juce::String type, std::optional<juce::PluginDescription> d)
        {
            if (d) vs.session.setInstrument (*t, {}, &*d); else vs.session.setInstrument (*t, type);
            rebuildSlots();
        });
    }
    void chooseFx()
    {
        dawui::choosePlugin (vs.edit().engine, false, &addFx, [this, t = track] (juce::String type, std::optional<juce::PluginDescription> d)
        {
            auto p = d ? vs.session.addPlugin (*t, {}, &*d) : vs.session.addPlugin (*t, type);
            rebuildSlots();
            if (p) p->showWindowExplicitly();
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
            if (r == 1) p->showWindowExplicitly();
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
    ViewState& vs;
    juce::Slider fader, pan; juce::TextButton mute, solo, instrument, addFx, addSend; juce::ComboBox output;
    juce::OwnedArray<juce::TextButton> slots; juce::OwnedArray<SendRow> sends; juce::Array<te::AudioTrack*> outBuses;
    te::LevelMeasurer::Client client; te::LevelMeasurer* measurer = nullptr;
    bool masterAdded = false; te::EditPlaybackContext* masterCtx = nullptr;
    float level[2] { -100, -100 }; juce::Rectangle<int> meterArea; int insertsY = 0, sendsY = 0;
};

MixerView::MixerView (ViewState& v) : vs (v)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    rebuild();
    startTimerHz (25);
}
MixerView::~MixerView() { clearAll(); }
void MixerView::clearAll() { strips.clear(); master.reset(); lastEdit = nullptr; lastLayout.clear(); }

void MixerView::rebuild()
{
    strips.clear();
    for (auto* t : vs.session.getTracks (true)) content.addAndMakeVisible (strips.add (new Strip (vs, t)));
    if (master == nullptr || &vs.edit() != lastEdit) { master = std::make_unique<Strip> (vs, nullptr); addAndMakeVisible (*master); lastEdit = &vs.edit(); }
    resized();
}

void MixerView::resized()
{
    master->setBounds (getWidth() - Strip::W - 6, 6, Strip::W, getHeight() - 12);
    viewport.setBounds (0, 0, getWidth() - Strip::W - 14, getHeight());
    int x = 6;
    for (auto* s : strips) { s->setBounds (x, 6, Strip::W, getHeight() - 22); x += Strip::W + 4; }
    content.setSize (x + 6, getHeight() - 10);
}

void MixerView::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff161616)); }

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
