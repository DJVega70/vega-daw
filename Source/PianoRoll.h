#pragma once
#include "DawUI.h"

/** Note editor for the selected MIDI clip (bottom panel) */
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    explicit PianoRoll (ViewState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::function<void()> onClose;
private:
    void timerCallback() override { repaint(); }
    te::MidiClip* clip();
    juce::Rectangle<int> gridArea() const { return { keysW, toolbarH, getWidth() - keysW, getHeight() - toolbarH - velH }; }
    juce::Rectangle<int> velArea() const { return { keysW, getHeight() - velH, getWidth() - keysW, velH }; }
    float beatToX (double b) const { return (float) (keysW + (b - scroll) * ppb); }
    double xToBeat (float x) const { return scroll + (x - keysW) / ppb; }
    float noteToY (int n) const { return (float) (toolbarH + (topNote - n) * rowH); }
    int yToNote (float y) const { return topNote - (int) std::floor ((y - toolbarH) / rowH); }
    te::MidiNote* noteAt (juce::Point<float>, bool& onRightEdge);
    double snap (double b, bool bypass) const;
    void audition (int note, int vel);
    ViewState& vs;
    static constexpr int keysW = 64, toolbarH = 32, velH = 70;
    float rowH = 12.0f; int topNote = 84; double scroll = 0, ppb = 80;
    enum class Mode { none, move, resize, create, velocity } mode = Mode::none;
    te::MidiNote* dragNote = nullptr; double grabBeat = 0, origStart = 0, origLen = 0; int grabNote = 0, origNote = 0;
    double lastLen = 0.25;
    juce::ComboBox gridBox; juce::TextButton quantBtn, closeBtn, clearBtn; juce::Label title;
    te::EditItemID lastClip;
};
