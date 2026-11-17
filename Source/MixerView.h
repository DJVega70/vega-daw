#pragma once
#include "DawUI.h"

/** Console: one strip per track and bus, plus the master */
class MixerView : public juce::Component, private juce::Timer
{
public:
    explicit MixerView (ViewState&);
    ~MixerView() override;
    void rebuild();
    void clearAll();      // call before the Edit is destroyed
    void resized() override;
    void paint (juce::Graphics&) override;
    class Strip;
private:
    void timerCallback() override;
    ViewState& vs;
    juce::Viewport viewport; juce::Component content;
    juce::OwnedArray<Strip> strips; std::unique_ptr<Strip> master;
    juce::String lastLayout; te::Edit* lastEdit = nullptr;
};
