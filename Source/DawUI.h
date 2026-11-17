#pragma once
#include "DawSession.h"
#include "VegaUI.h"

/** Shared view state for the arrange window, piano roll and mixer */
struct ViewState
{
    explicit ViewState (DawSession& s) : session (s) {}
    DawSession& session;
    double pixelsPerBeat = 28.0, scrollBeats = 0.0;
    int scrollY = 0, snapDiv = 4;                     // grid divisions per beat (0 = off)
    te::EditItemID selectedClip, selectedTrack;
    bool followPlayhead = true;
    std::function<void()> onSelectionChanged, onOpenPianoRoll;

    te::Edit& edit() { return session.edit(); }
    te::TempoSequence& tempo() { return session.edit().tempoSequence; }
    double timeToBeats (te::TimePosition t) { return tempo().toBeats (t).inBeats(); }
    te::TimePosition beatsToTime (double b) { return tempo().toTime (te::BeatPosition::fromBeats (b)); }
    float beatToX (double beat, int x0) const { return (float) (x0 + (beat - scrollBeats) * pixelsPerBeat); }
    double xToBeat (float x, int x0) const { return scrollBeats + (x - x0) / pixelsPerBeat; }
    double snapBeat (double b, bool bypass = false) const { return (snapDiv <= 0 || bypass) ? b : std::round (b * snapDiv) / snapDiv; }

    te::Clip* getSelectedClip()
    {
        if (! selectedClip.isValid()) return nullptr;
        for (auto* t : te::getAudioTracks (edit())) for (auto* c : t->getClips()) if (c->itemID == selectedClip) return c;
        return nullptr;
    }
    te::AudioTrack* getSelectedTrack()
    {
        for (auto* t : te::getAudioTracks (edit())) if (t->itemID == selectedTrack) return t;
        return nullptr;
    }
    void selectClip (te::Clip* c)
    {
        selectedClip = c != nullptr ? c->itemID : te::EditItemID();
        if (c != nullptr) if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack())) selectedTrack = t->itemID;
        if (onSelectionChanged) onSelectionChanged();
    }
    void selectTrack (te::AudioTrack* t) { selectedTrack = t != nullptr ? t->itemID : te::EditItemID(); if (onSelectionChanged) onSelectionChanged(); }
};

namespace dawui
{
inline juce::Colour clipColour (const te::Clip& c)
{
    if (auto* t = c.getTrack()) return DawSession::trackColour (*t);
    return VegaColours::accent;
}

/** Popup menu listing built-in effects/instruments and scanned plugins. Result is delivered asynchronously. */
inline void choosePlugin (te::Engine& engine, bool instruments, juce::Component* target,
                          std::function<void (juce::String builtInType, std::optional<juce::PluginDescription>)> done)
{
    juce::PopupMenu m, vega, other;
    auto types = engine.getPluginManager().knownPluginList.getTypes();
    std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    juce::StringArray builtIns;
    if (instruments) builtIns = { te::FourOscPlugin::xmlTypeName, te::SamplerPlugin::xmlTypeName };
    else builtIns = DawSession::builtInEffectTypes();
    juce::PopupMenu bi;
    for (int i = 0; i < builtIns.size(); ++i) bi.addItem (1 + i, DawSession::builtInDisplayName (builtIns[i]));
    m.addSubMenu ("Built-in", bi);
    for (int i = 0; i < types.size(); ++i)
    {
        if (types.getReference (i).isInstrument != instruments) continue;
        auto& menu = types.getReference (i).manufacturerName.containsIgnoreCase ("Vega") ? vega : other;
        menu.addItem (1000 + i, types.getReference (i).name + "   (" + types.getReference (i).pluginFormatName + ")");
    }
    if (vega.getNumItems() > 0) m.addSubMenu ("Vega Productions", vega);
    if (other.getNumItems() > 0) m.addSubMenu ("Plugins", other);
    if (types.isEmpty()) m.addItem (-1, "No plugins scanned yet - Options > Plugins...", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [types, builtIns, done] (int r)
    {
        if (r >= 1000) done ({}, types[r - 1000]);
        else if (r >= 1) done (builtIns[r - 1], std::nullopt);
    });
}

/** Simple slider-per-parameter editor for built-in plugins (which have no GUI of their own) */
class GenericEditor : public te::Plugin::EditorComponent, private juce::Timer
{
public:
    explicit GenericEditor (te::Plugin& p) : plugin (p)
    {
        for (auto* param : plugin.getAutomatableParameters())
        {
            auto* row = rows.add (new Row (*param));
            addAndMakeVisible (row->slider); addAndMakeVisible (row->label);
        }
        setSize (420, std::max (60, rows.size() * 34 + 20));
        startTimerHz (15);
    }
    bool allowWindowResizing() override { return false; }
    juce::ComponentBoundsConstrainer* getBoundsConstrainer() override { return nullptr; }
    void paint (juce::Graphics& g) override { g.fillAll (VegaColours::background); }
    void resized() override
    {
        int y = 10;
        for (auto* r : rows) { r->label.setBounds (10, y, 140, 26); r->slider.setBounds (150, y, getWidth() - 160, 26); y += 34; }
    }
private:
    struct Row
    {
        explicit Row (te::AutomatableParameter& p) : param (p)
        {
            label.setText (p.getParameterName(), juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, VegaColours::text);
            const auto r = p.getValueRange();
            slider.setSliderStyle (juce::Slider::LinearBar);
            slider.setRange (r.getStart(), r.getEnd(), 0.0);
            slider.setValue (p.getCurrentValue(), juce::dontSendNotification);
            slider.textFromValueFunction = [&p] (double v) { return p.valueToString ((float) v) + " " + p.getLabel(); };
            slider.onDragStart = [&p] { p.parameterChangeGestureBegin(); };
            slider.onDragEnd = [&p] { p.parameterChangeGestureEnd(); };
            slider.onValueChange = [this] { param.setParameter ((float) slider.getValue(), juce::sendNotification); };
            slider.updateText();
        }
        te::AutomatableParameter& param; juce::Slider slider; juce::Label label;
    };
    void timerCallback() override
    {
        for (auto* r : rows) if (! r->slider.isMouseButtonDown()) r->slider.setValue (r->param.getCurrentValue(), juce::dontSendNotification);
    }
    te::Plugin& plugin; juce::OwnedArray<Row> rows;
};

inline juce::String formatBarsBeats (te::Edit& edit, te::TimePosition t)
{
    auto bb = edit.tempoSequence.toBarsAndBeats (t);
    return juce::String (bb.bars + 1) + "." + juce::String (bb.getWholeBeats() + 1) + "." + juce::String ((int) (bb.getFractionalBeats().inBeats() * 4) + 1);
}
} // namespace dawui
