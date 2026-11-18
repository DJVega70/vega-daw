#include "DawSession.h"
#include "DawUI.h"
#include "PluginWindow.h"
#include "ArrangeView.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "Controllers.h"
#include "PluginBrowser.h"

//==============================================================================
class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar (ViewState& v) : vs (v)
    {
        auto btn = [this] (juce::TextButton& b, const juce::String& t, juce::Colour on = VegaColours::accent)
        {
            b.setButtonText (t); b.setColour (juce::TextButton::buttonOnColourId, on); addAndMakeVisible (b);
        };
        btn (home, juce::String::fromUTF8 ("\xe2\x8f\xae")); btn (play, juce::String::fromUTF8 ("\xe2\x96\xb6")); btn (stop, juce::String::fromUTF8 ("\xe2\x96\xa0"));
        btn (rec, juce::String::fromUTF8 ("\xe2\x97\x8f"), juce::Colour (0xffe04848)); btn (loop, "LOOP"); btn (click, "CLICK");
        btn (view, "MIXER"); btn (browse, "PLUGINS"); btn (addAudio, "+ AUDIO"); btn (addInst, "+ INSTRUMENT"); btn (addBus, "+ BUS");
        home.onClick = [this] { vs.edit().getTransport().setPosition (0s); };
        play.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isPlaying()) t.stop (false, false); else t.play (false); };
        stop.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isPlaying()) t.stop (false, false); else t.setPosition (0s); };
        rec.onClick = [this] { auto& t = vs.edit().getTransport(); if (t.isRecording()) t.stop (true, false); else t.record (false); };
        loop.onClick = [this] { auto& t = vs.edit().getTransport(); t.looping = ! t.looping.get(); if (t.looping && t.getLoopRange().getLength() <= 0s) t.setLoopRange ({ 0s, vs.session.barLength() * 4 }); };
        click.onClick = [this] { vs.edit().clickTrackEnabled = ! vs.edit().clickTrackEnabled.get(); };
        addAudio.onClick = [this] { vs.selectTrack (vs.session.addTrack (DawSession::TrackKind::audio)); };
        addInst.onClick = [this] { vs.selectTrack (vs.session.addTrack (DawSession::TrackKind::midi)); };
        addBus.onClick = [this] { vs.selectTrack (vs.session.addTrack (DawSession::TrackKind::bus)); };
        tempo.setSliderStyle (juce::Slider::LinearBar); tempo.setRange (40.0, 240.0, 0.1); tempo.setTextValueSuffix (" BPM");
        tempo.setColour (juce::Slider::trackColourId, VegaColours::accentDim);
        tempo.onValueChange = [this] { vs.session.setTempo (tempo.getValue()); };
        addAndMakeVisible (tempo);
        sig.addItemList ({ "4/4", "3/4", "6/8", "7/8", "5/4" }, 1);
        sig.onChange = [this] { const int n[] { 4, 3, 6, 7, 5 }, d[] { 4, 4, 8, 8, 4 }; const int i = sig.getSelectedItemIndex(); if (i >= 0) vs.session.setTimeSig (n[i], d[i]); };
        addAndMakeVisible (sig);
        snap.addItemList ({ "Snap: Bar", "Snap: Beat", "Snap: 1/8", "Snap: 1/16", "Snap: Off" }, 1);
        snap.setSelectedItemIndex (1, juce::dontSendNotification);
        snap.onChange = [this] { const int d[] { -1, 1, 2, 4, 0 }; const int i = snap.getSelectedItemIndex(); vs.snapDiv = d[i] == -1 ? 0 : d[i]; barSnap = d[i] == -1; };
        addAndMakeVisible (snap);
        startTimerHz (20);
    }
    std::function<void()> onToggleView, onToggleBrowser;
    void setBrowserShown (bool b) { browse.setToggleState (b, juce::dontSendNotification); }
    bool barSnap = false;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1c1c1c));
        auto r = display.toFloat();
        vegaui::drawScreen (g, r);
        auto& tr = vs.edit().getTransport();
        g.setFont (VegaFonts::mono (19.0f)); g.setColour (VegaColours::accent);
        g.drawText (dawui::formatBarsBeats (vs.edit(), tr.getPosition()), r.removeFromLeft (r.getWidth() * 0.5f).toNearestInt(), juce::Justification::centred);
        const double secs = tr.getPosition().inSeconds();
        g.setFont (VegaFonts::mono (13.0f)); g.setColour (VegaColours::textDim);
        g.drawText (juce::String::formatted ("%d:%02d.%03d", (int) secs / 60, (int) secs % 60, (int) (std::fmod (secs, 1.0) * 1000)), r.toNearestInt(), juce::Justification::centred);
        g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (9.5f, true));
        const float cpu = (float) vs.edit().engine.getDeviceManager().getCpuUsage();
        g.drawText ("CPU", cpuArea.withHeight (12), juce::Justification::centredLeft);
        auto bar = cpuArea.withTrimmedTop (14).toFloat();
        g.setColour (juce::Colour (0xff111111)); g.fillRect (bar);
        g.setColour (cpu > 0.8f ? juce::Colour (0xffe04848) : VegaColours::accent); g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu)));
        g.setColour (VegaColours::line); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
    void resized() override
    {
        int x = 8; const int y = 8, h = getHeight() - 16;
        for (auto* b : { &home, &play, &stop, &rec }) { b->setBounds (x, y, 40, h); x += 44; }
        loop.setBounds (x + 6, y, 58, h); x += 70; click.setBounds (x, y, 60, h); x += 70;
        display = { x, y - 2, 260, h + 4 }; x += 270;
        tempo.setBounds (x, y, 110, h); x += 116; sig.setBounds (x, y, 70, h); x += 76; snap.setBounds (x, y, 112, h); x += 120;
        cpuArea = { x + 4, y, 60, h }; x += 74;
        view.setBounds (getWidth() - 96, y, 88, h);
        addBus.setBounds (getWidth() - 166, y, 66, h); addInst.setBounds (getWidth() - 280, y, 110, h); addAudio.setBounds (getWidth() - 362, y, 78, h);
        browse.setBounds (getWidth() - 452, y, 82, h);
        browse.onClick = [this] { if (onToggleBrowser) onToggleBrowser(); };
        view.onClick = [this] { if (onToggleView) onToggleView(); };
    }
    void setViewLabel (bool mixerShowing) { view.setButtonText (mixerShowing ? "ARRANGE" : "MIXER"); }
    void refreshFromEdit()
    {
        tempo.setValue (vs.session.getTempo(), juce::dontSendNotification);
        auto* ts = vs.edit().tempoSequence.getTimeSig (0);
        const juce::String s = juce::String (ts->numerator.get()) + "/" + juce::String (ts->denominator.get());
        for (int i = 0; i < sig.getNumItems(); ++i) if (sig.getItemText (i) == s) sig.setSelectedItemIndex (i, juce::dontSendNotification);
    }
private:
    void timerCallback() override
    {
        auto& tr = vs.edit().getTransport();
        play.setToggleState (tr.isPlaying(), juce::dontSendNotification);
        rec.setToggleState (tr.isRecording(), juce::dontSendNotification);
        loop.setToggleState (tr.looping.get(), juce::dontSendNotification);
        click.setToggleState (vs.edit().clickTrackEnabled.get(), juce::dontSendNotification);
        if (! tempo.isMouseButtonDown() && ! tempo.hasKeyboardFocus (true)) tempo.setValue (vs.session.getTempo(), juce::dontSendNotification);
        if (barSnap) vs.snapDiv = 0;
        repaint (display); repaint (cpuArea);
    }
    ViewState& vs;
    juce::TextButton home, play, stop, rec, loop, click, view, addAudio, addInst, addBus, browse;
    juce::Slider tempo; juce::ComboBox sig, snap;
    juce::Rectangle<int> display, cpuArea;
};

//==============================================================================
class MainComponent : public juce::Component, public juce::MenuBarModel, public juce::DragAndDropContainer, private juce::Timer
{
public:
    MainComponent (te::Engine& e) : engine (e), session (e), vs (session), hub (e, session), transport (vs), arrange (vs), mixer (vs), pianoRoll (vs), browser (vs)
    {
        setLookAndFeel (&lnf);
        menuBar.setModel (this);
        addAndMakeVisible (menuBar); addAndMakeVisible (transport); addAndMakeVisible (arrange);
        addChildComponent (mixer); addChildComponent (pianoRoll); addChildComponent (divider);
        transport.onToggleView = [this] { showMixer (! mixer.isVisible()); };
        addAndMakeVisible (browser); transport.setBrowserShown (true);
        transport.onToggleBrowser = [this] { showBrowser (! browser.isVisible()); };
        browser.onLoad = [this] (const PluginBrowser::Item& it, te::AudioTrack* t) { loadFromBrowser (it, t); };
        vs.onPluginDropped = [this] (const juce::var& d, te::AudioTrack* t)
        {
            if (auto* it = browser.itemForDragDescription (d)) { loadFromBrowser (*it, t, true); return true; }
            return false;
        };
        // pick up newly installed plug-ins in the background; first run does a full visible scan
        juce::Timer::callAfterDelay (1500, [this, sp = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (sp != nullptr) browser.startScan (engine.getPluginManager().knownPluginList.getNumTypes() > 0);
        });
        vs.onOpenPianoRoll = [this] { showPianoRoll (true); };
        vs.onSelectionChanged = [this] { hub.surface.selectedID = vs.selectedTrack; routeMidiToSelectedTrack(); arrange.repaint(); };
        hub.onSelectTrack = [this] (te::AudioTrack* t) { vs.selectTrack (t); };
        hub.onSave = [this] { save (false); };
        hub.autoConnect();
        pianoRoll.onClose = [this] { showPianoRoll (false); };
        session.onBeforeEditChange = [this] { mixer.clearAll(); };
        session.onEditChanged = [this] { arrange.editChanged(); mixer.rebuild(); hub.editChanged(); transport.refreshFromEdit(); setupInputs(); showPianoRoll (false); updateTitle(); };
        divider.onDrag = [this] (int dy) { pianoH = juce::jlimit (160, getHeight() - 240, pianoH - dy); resized(); };
        setupInputs();
        transport.refreshFromEdit();
        setWantsKeyboardFocus (true);
        setSize (1500, 900);
        startTimerHz (2);
    }
    ~MainComponent() override { menuBar.setModel (nullptr); mixer.clearAll(); setLookAndFeel (nullptr); }

    DawSession& getSession() { return session; }
    std::function<void (juce::String)> onTitle;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        auto top = getLocalBounds().removeFromTop (34).toFloat();
        g.setColour (juce::Colour (0xff222222)); g.fillRect (top);
        g.setColour (VegaColours::text); g.setFont (VegaFonts::ui (15.0f, true));
        g.drawText ("Vega Productions", top.withTrimmedLeft (12).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (10.0f, true));
        g.drawText ("DAW  VP-20", top.withTrimmedLeft (150).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (VegaColours::line); g.drawHorizontalLine (33, 0, (float) getWidth());
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (34);
        menuBar.setBounds (top.withTrimmedLeft (240).withWidth (360).reduced (0, 5));
        transport.setBounds (r.removeFromTop (52));
        if (pianoRoll.isVisible())
        {
            pianoRoll.setBounds (r.removeFromBottom (pianoH));
            divider.setBounds (r.removeFromBottom (6));
        }
        if (browser.isVisible()) browser.setBounds (r.removeFromLeft (browserW));
        arrange.setBounds (r); mixer.setBounds (r);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        auto& tr = session.edit().getTransport();
        const auto cmd = juce::ModifierKeys::commandModifier;
        if (k == juce::KeyPress::spaceKey) { if (tr.isPlaying()) tr.stop (false, false); else tr.play (false); return true; }
        if (k.getKeyCode() == 'R' && k.getModifiers().isAnyModifierKeyDown() == false) { if (tr.isRecording()) tr.stop (true, false); else tr.record (false); return true; }
        if (k.getKeyCode() == 'L' && ! k.getModifiers().isAnyModifierKeyDown()) { tr.looping = ! tr.looping.get(); return true; }
        if (k == juce::KeyPress (juce::KeyPress::tabKey)) { showMixer (! mixer.isVisible()); return true; }
        if (k.getKeyCode() == 'B' && ! k.getModifiers().isAnyModifierKeyDown()) { showBrowser (! browser.isVisible()); return true; }
        if (k == juce::KeyPress ('z', cmd, 0)) { session.edit().getUndoManager().undo(); return true; }
        if (k == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0) || k == juce::KeyPress ('y', cmd, 0)) { session.edit().getUndoManager().redo(); return true; }
        if (k == juce::KeyPress ('s', cmd, 0)) { save (false); return true; }
        if (k == juce::KeyPress (juce::KeyPress::homeKey)) { tr.setPosition (0s); return true; }
        return false;
    }

    // ---- menus
    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "Track", "Options", "Help" }; }
    juce::PopupMenu getMenuForIndex (int idx, const juce::String&) override
    {
        juce::PopupMenu m;
        if (idx == 0)
        {
            m.addItem (1, "New project"); m.addItem (2, "Open...      Ctrl+O"); m.addItem (3, "Save          Ctrl+S"); m.addItem (4, "Save as...");
            m.addSeparator(); m.addItem (5, "Import audio / MIDI..."); m.addItem (6, "Export mixdown (WAV)...");
            m.addSeparator(); m.addItem (9, "Quit");
        }
        else if (idx == 1)
        {
            m.addItem (20, "Undo   Ctrl+Z", session.edit().getUndoManager().canUndo()); m.addItem (21, "Redo   Ctrl+Shift+Z", session.edit().getUndoManager().canRedo());
            m.addSeparator(); m.addItem (22, "Split clip at playhead   Ctrl+E"); m.addItem (23, "Duplicate clip   Ctrl+D"); m.addItem (24, "Delete clip   Del");
        }
        else if (idx == 2)
        {
            m.addItem (30, "Add audio track"); m.addItem (31, "Add instrument track"); m.addItem (32, "Add bus");
            m.addSeparator(); m.addItem (33, "Delete selected track", vs.getSelectedTrack() != nullptr);
            m.addItem (34, "Show mixer   Tab", true, mixer.isVisible()); m.addItem (35, "Show piano roll", true, pianoRoll.isVisible());
            m.addItem (36, "Show plug-in browser   B", true, browser.isVisible());
            m.addSeparator();
            m.addItem (37, "Open instrument of selected track", vs.getSelectedTrack() != nullptr);
            m.addItem (38, "Plug-ins on selected track...", vs.getSelectedTrack() != nullptr);
        }
        else if (idx == 3)
        {
            m.addItem (40, "Audio / MIDI settings..."); m.addItem (41, "Scan for new plug-ins"); m.addItem (45, "Advanced plug-in list...");
            m.addItem (44, juce::String ("Controllers (FLkey 2 / HUI)...") + (hub.isConnected() ? "   - connected" : ""));
            m.addItem (42, "Metronome", true, session.edit().clickTrackEnabled.get());
            m.addItem (43, "Follow playhead", true, vs.followPlayhead);
        }
        else
        {
            m.addItem (50, "Keyboard shortcuts...");
            m.addItem (51, "About Vega Productions DAW");
        }
        return m;
    }
    void menuItemSelected (int id, int) override
    {
        switch (id)
        {
            case 1: confirmDiscard ([this] { session.newProject(); }); break;
            case 2: open(); break;
            case 3: save (false); break;
            case 4: save (true); break;
            case 5: importFiles(); break;
            case 6: exportMix(); break;
            case 9: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            case 20: session.edit().getUndoManager().undo(); break;
            case 21: session.edit().getUndoManager().redo(); break;
            case 22: arrange.splitSelectedAtPlayhead(); break;
            case 23: arrange.duplicateSelectedClip(); break;
            case 24: arrange.deleteSelectedClip(); break;
            case 30: session.addTrack (DawSession::TrackKind::audio); break;
            case 31: session.addTrack (DawSession::TrackKind::midi); break;
            case 32: session.addTrack (DawSession::TrackKind::bus); break;
            case 33: if (auto* t = vs.getSelectedTrack()) session.deleteTrack (*t); break;
            case 34: showMixer (! mixer.isVisible()); break;
            case 35: showPianoRoll (! pianoRoll.isVisible()); break;
            case 40: showAudioSettings(); break;
            case 41: showBrowser (true); browser.startScan (false); break;
            case 45: showPluginList(); break;
            case 36: showBrowser (! browser.isVisible()); break;
            case 37: if (auto* t = vs.getSelectedTrack()) { auto* f = t->pluginList[0]; if (f != nullptr && f->isSynth()) dawui::openPlugin (f); else dawui::pluginChainMenu (session, *t, &transport); } break;
            case 38: if (auto* t = vs.getSelectedTrack()) dawui::pluginChainMenu (session, *t, &transport, [this] { mixer.rebuild(); }); break;
            case 44:
            {
                juce::DialogWindow::LaunchOptions o;
                o.dialogTitle = "Controllers"; o.content.setOwned (new ControllerPanel (hub));
                o.dialogBackgroundColour = juce::Colour (0xff1e1e1e); o.resizable = true; o.useNativeTitleBar = true;
                o.launchAsync();
                break;
            }
            case 42: session.edit().clickTrackEnabled = ! session.edit().clickTrackEnabled.get(); break;
            case 43: vs.followPlayhead = ! vs.followPlayhead; break;
            case 50: juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Shortcuts",
                        "Space  play / stop\nR  record\nL  loop on/off\nHome  go to start\nTab  arrange / mixer\nB  plug-in browser\nCtrl+Z / Ctrl+Shift+Z  undo / redo\nCtrl+S  save\nCtrl+E  split clip at playhead\nCtrl+D  duplicate clip\nDel  delete clip\n\n"
                        "Arrange: drag clips to move, drag edges to trim, drag the top corners of audio clips for fades, Alt-drag to copy, Shift for no snap.\n"
                        "Ctrl+wheel zoom, Shift+wheel scroll. Double-click an instrument track to create a MIDI clip.\n"
                        "Ruler: click to move the playhead, drag the top strip (or Shift-drag) to set the loop.\n"
                        "Piano roll: click to add notes, drag to move, drag the right edge to resize, right-click or double-click to delete, drag in the velocity lane."); break;
            case 51: juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Vega Productions DAW",
                        "Version " JUCE_APPLICATION_VERSION_STRING "\n\nBuilt on Tracktion Engine (GPLv3) and JUCE.\n32-bit floating-point, multi-core audio engine."); break;
            default: break;
        }
    }

    void confirmDiscard (std::function<void()> then)
    {
        if (! session.hasUnsavedChanges()) { then(); return; }
        auto opts = juce::MessageBoxOptions().withTitle ("Unsaved changes").withMessage ("Save the current project first?")
                        .withButton ("Save", 1).withButton ("Don't save", 2).withButton ("Cancel", 0);
        juce::AlertWindow::showAsync (opts, [this, then] (int r)
        {
            if (r == 1) save (false, then);
            else if (r == 2) then();
        });
    }

private:
    void timerCallback() override { updateTitle(); }
    void updateTitle()
    {
        const auto f = session.getProjectFile();
        if (onTitle) onTitle ((f == juce::File() ? juce::String ("Untitled") : f.getFileNameWithoutExtension()) + (session.hasUnsavedChanges() ? " *" : "") + "  -  Vega Productions DAW");
    }
    void showMixer (bool m) { mixer.setVisible (m); arrange.setVisible (! m); transport.setViewLabel (m); if (m) mixer.rebuild(); }
    void showBrowser (bool show) { browser.setVisible (show); transport.setBrowserShown (show); resized(); }

    /** Load a browser item: instruments go on the target / selected instrument track (or a new one),
        effects go at the end of the target / selected track's chain. The plug-in window opens right away. */
    void loadFromBrowser (const PluginBrowser::Item& it, te::AudioTrack* target, bool dropped = false)
    {
        auto* t = target != nullptr ? target : (dropped ? nullptr : vs.getSelectedTrack());
        te::Plugin::Ptr p;
        if (it.instrument)
        {
            if (t == nullptr || DawSession::kindOf (*t) != DawSession::TrackKind::midi)
                t = session.addTrack (DawSession::TrackKind::midi, it.name);
            if (t == nullptr) return;
            p = it.desc ? session.setInstrument (*t, {}, &*it.desc) : session.setInstrument (*t, it.builtInType);
        }
        else
        {
            if (t == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, it.name,
                    "Select a track first (click its name), or drag the effect onto a track or mixer strip.");
                return;
            }
            p = it.desc ? session.addPlugin (*t, {}, &*it.desc) : session.addPlugin (*t, it.builtInType);
        }
        vs.selectTrack (t);
        if (p == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, it.name, "Couldn't load this plug-in.");
            return;
        }
        arrange.rebuildHeaders(); mixer.rebuild();
        dawui::openPlugin (p.get());
    }

    void showPianoRoll (bool show) { pianoRoll.setVisible (show); divider.setVisible (show); resized(); if (show) pianoRoll.grabKeyboardFocus(); }

    void setupInputs()
    {
        auto& dm = engine.getDeviceManager();
        for (auto& midiIn : dm.getMidiInDevices())
        {
            // controller "DAW" ports carry HUI control data, not notes - leave them to the controller script
            if (ControllerHub::looksLikeDawPort (midiIn->getName()) || (hub.inputName.isNotEmpty() && midiIn->getName() == hub.inputName)) { midiIn->setEnabled (false); continue; }
            midiIn->setMonitorMode (te::InputDevice::MonitorMode::automatic); midiIn->setEnabled (true);
        }
        session.edit().getTransport().ensureContextAllocated();
        routeMidiToSelectedTrack();
    }
    // play your keyboard into whichever instrument track is selected
    void routeMidiToSelectedTrack()
    {
        auto* t = vs.getSelectedTrack();
        if (t == nullptr || DawSession::kindOf (*t) != DawSession::TrackKind::midi)
        {
            for (auto* tr : session.getTracks()) if (DawSession::kindOf (*tr) == DawSession::TrackKind::midi) { t = tr; break; }
            if (t == nullptr) return;
        }
        for (auto* in : session.edit().getAllInputDevices())
            if (in->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice && ! te::isOnTargetTrack (*in, *t, 0))
                [[maybe_unused]] auto r = in->setTarget (t->itemID, true, nullptr, 0);
    }

    void open()
    {
        confirmDiscard ([this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Open project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.vegaproj;*.tracktionedit");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile() && ! session.openProject (fc.getResult()))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open", "Couldn't open " + fc.getResult().getFileName());
            });
        });
    }
public:
    void openFile (const juce::File& f) { if (f.existsAsFile()) session.openProject (f); }
    void save (bool as, std::function<void()> then = nullptr)
    {
        if (! as && session.saveProject()) { if (then) then(); return; }
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Vega Productions").createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Save project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Vega Productions").getChildFile ("My Song.vegaproj"), "*.vegaproj");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, then] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            if (session.saveProjectAs (fc.getResult())) { if (then) then(); }
            else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save", "Couldn't save the project.");
        });
    }
private:
    void importFiles()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import audio or MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc)
        {
            juce::StringArray paths; for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
            if (! paths.isEmpty()) arrange.filesDropped (paths, ArrangeView::headerW + 2, ArrangeView::rulerH + arrange.totalTracksHeight() + 10);
        });
    }
    void exportMix()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export mixdown", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ((session.getProjectFile() == juce::File() ? juce::String ("Mixdown") : session.getProjectFile().getFileNameWithoutExtension()) + ".wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            auto& tr = session.edit().getTransport();
            std::optional<te::TimeRange> range; if (tr.looping) range = tr.getLoopRange();
            const bool ok = session.exportWav (fc.getResult(), range);
            juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon, "Export",
                ok ? "Exported " + fc.getResult().withFileExtension ("wav").getFileName() + (range ? "\n(loop range only)" : "") : juce::String ("Nothing to export - add some clips first."));
        });
    }
    void showAudioSettings()
    {
        auto* panel = new juce::Component();
        auto* sel = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager, 0, 64, 2, 64, true, false, true, false);
        sel->setSize (560, 520); panel->addAndMakeVisible (sel);
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Audio / MIDI settings"; o.content.setOwned (sel);
        o.dialogBackgroundColour = juce::Colour (0xff1e1e1e); o.resizable = true; o.useNativeTitleBar = true;
        delete panel;
        o.launchAsync();
        juce::Timer::callAfterDelay (500, [this] { setupInputs(); });
    }
    void showPluginList()
    {
        auto& pm = engine.getPluginManager();
        auto dead = engine.getPropertyStorage().getAppCacheFolder().getChildFile ("PluginScanDeadMansPedal");
        auto* list = new juce::PluginListComponent (pm.pluginFormatManager, pm.knownPluginList, dead, nullptr, true);
        list->setSize (760, 560);
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Plugins - press Options > Scan for new or updated VST3 plug-ins";
        o.content.setOwned (list); o.dialogBackgroundColour = juce::Colour (0xff1e1e1e); o.resizable = true; o.useNativeTitleBar = true;
        o.launchAsync();
    }

    struct Divider : juce::Component
    {
        std::function<void (int)> onDrag; int last = 0;
        Divider() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff2a2a2a)); g.setColour (VegaColours::textDim); g.fillRect (getWidth() / 2 - 20, 2, 40, 2); }
        void mouseDown (const juce::MouseEvent& e) override { last = e.getScreenY(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getScreenY() - last); last = e.getScreenY(); }
    };

    te::Engine& engine;
    VegaLookAndFeel lnf;
    DawSession session; ViewState vs; ControllerHub hub;
    juce::MenuBarComponent menuBar;
    TransportBar transport; ArrangeView arrange; MixerView mixer; PianoRoll pianoRoll; PluginBrowser browser; Divider divider;
    int browserW = 250;
    int pianoH = 320;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class VegaDawApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Vega Productions DAW"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        vegawin::makeGenericEditor = [] (te::Plugin& p) -> std::unique_ptr<te::Plugin::EditorComponent> { return std::make_unique<dawui::GenericEditor> (p); };
        engine = std::make_unique<te::Engine> (getApplicationName(), std::make_unique<vegawin::ExtendedUIBehaviour>(), nullptr);
        window = std::make_unique<Window> (*engine);
        const juce::File f (commandLine.unquoted().trim());
        if (f.hasFileExtension ("vegaproj")) window->main->openFile (f);
    }
    void shutdown() override { window.reset(); engine.reset(); }
    void systemRequestedQuit() override
    {
        if (window == nullptr) { quit(); return; }
        window->main->confirmDiscard ([] { juce::JUCEApplication::getInstance()->quit(); });
    }
    void anotherInstanceStarted (const juce::String& cl) override
    {
        const juce::File f (cl.unquoted().trim());
        if (window && f.hasFileExtension ("vegaproj")) window->main->confirmDiscard ([this, f] { window->main->openFile (f); });
    }

    struct Window : public juce::DocumentWindow
    {
        explicit Window (te::Engine& e) : DocumentWindow ("Vega Productions DAW", VegaColours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            main = new MainComponent (e);
            main->onTitle = [this] (juce::String t) { setName (t); };
            setContentOwned (main, true);
            setResizable (true, false);
            setResizeLimits (1100, 640, 10000, 10000);
            centreWithSize (std::min (1600, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getWidth() - 40),
                            std::min (950, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getHeight() - 40));
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* main = nullptr;
    };
private:
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (VegaDawApp)
