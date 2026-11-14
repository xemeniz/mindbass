#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    // Colores de MIND BASS en la skin v2: grafito con sombra carbón.
    const mind::SkinColours kSkin { juce::Colour (0xffe4e4e7), juce::Colour (0xffb9b9bf), juce::Colour (0xff1d1d22), juce::Colour (0xff3a3a41) };

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url)
    {
        const auto path = url.upToFirstOccurrenceOf ("?", false, false);
        if (path != "/" && path != "/index.html" && path.isNotEmpty())
            return std::nullopt;
        const auto* begin = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        std::vector<std::byte> data (begin, begin + BinaryData::index_htmlSize);
        return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
    }
}

MBEditor::MBEditor (MBProcessor& p)
    : AudioProcessorEditor (&p), proc (p), bar (kSkin)
{
    frame = juce::ImageCache::getFromMemory (BinaryData::frame_jpg, BinaryData::frame_jpgSize);
    bar.font = juce::Typeface::createSystemTypefaceFor (BinaryData::NunitoExtraBold_ttf, BinaryData::NunitoExtraBold_ttfSize);
    bar.acceptDrops = false;
    updateBar();

    auto options = juce::WebBrowserComponent::Options{}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const juce::String& url) { return getResource (url); })
        .withEventListener ("mbReady", [this] (const juce::var&) { ready = true; lastState = {}; sendSettings(); sendPattern(); })
        .withEventListener ("mbSet", [this] (const juce::var& v) { proc.setParam (v.getProperty ("id", "").toString(), (float) (double) v.getProperty ("v", 0.0)); })
        .withEventListener ("mbGesture", [this] (const juce::var& v) { proc.gesture (v.getProperty ("id", "").toString(), (bool) v.getProperty ("begin", false)); })
        .withEventListener ("mbPattern", [this] (const juce::var& v) { proc.setPatternVar (v); })
        .withEventListener ("mbPlay", [this] (const juce::var&) { proc.togglePlay(); })
        .withEventListener ("mbStop", [this] (const juce::var&) { proc.stopPlay(); })
        .withEventListener ("mbSettings", [this] (const juce::var& v)
        {
            if (v.hasProperty ("theme")) proc.lightTheme = v.getProperty ("theme", "dark").toString() == "light";
            if (v.hasProperty ("drag")) proc.dragAudio = v.getProperty ("drag", "midi").toString() == "audio";
            updateBar();
            repaint();
            proc.saveGlobal();
        });

    bar.makeFile = [this]
    {
        auto f = proc.exportClip (proc.dragAudio);
        if (! f.existsAsFile()) toast (proc.dragAudio ? "Could not write the WAV" : "Could not write the MIDI file");
        return f;
    };

    proc.onPatternChanged = [this] { sendPattern(); };

    web = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*web);
    addAndMakeVisible (bar);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    const double ratio = (double) kW / (kWebH + kBarH);
    setResizeLimits (800, (int) (800 / ratio), 2400, (int) (2400 / ratio));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);
    setSize (kW, kWebH + kBarH);
    startTimerHz (30);
}

MBEditor::~MBEditor()
{
    stopTimer();
    proc.onPatternChanged = nullptr;
}

void MBEditor::updateBar()
{
    bar.enabled = true;
    bar.label = proc.dragAudio ? "DRAG THE AUDIO TO ABLETON" : "DRAG THE MIDI TO ABLETON";
    bar.repaint();
}

void MBEditor::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (frame.isValid()) g.drawImage (frame, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    else g.fillAll (juce::Colour (0xff0b0a0e));
    g.setColour (juce::Colours::black);
    g.drawRect (getLocalBounds(), 3);
}

void MBEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    const int webH = juce::roundToInt (kWebH * s);
    if (web != nullptr) web->setBounds (0, 0, getWidth(), webH);
    const int side = juce::roundToInt (35 * s);
    bar.setBounds (side, webH + juce::roundToInt (2 * s), getWidth() - 2 * side, getHeight() - webH - juce::roundToInt (21 * s));
}

void MBEditor::emit (const juce::Identifier& ev, const juce::var& v)
{
    if (web != nullptr) web->emitEventIfBrowserIsVisible (ev, v);
}

void MBEditor::toast (const juce::String& msg)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("msg", msg);
    emit ("mbToast", juce::var (o));
}

void MBEditor::sendSettings()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("theme", proc.lightTheme ? "light" : "dark");
    o->setProperty ("drag", proc.dragAudio ? "audio" : "midi");
    o->setProperty ("standalone", proc.isStandalone());
    emit ("mbSettings", juce::var (o));
}

void MBEditor::sendPattern() { emit ("mbPattern", proc.patternVar()); }

void MBEditor::timerCallback()
{
    if (! ready) return;
    auto* o = new juce::DynamicObject();
    const auto st = proc.stateVar();
    const auto js = juce::JSON::toString (st, true);
    if (js != lastState) { lastState = js; o->setProperty ("p", st); }
    o->setProperty ("play", proc.playState());
    o->setProperty ("pos", std::round (proc.playPos() * 1000.0f) / 1000.0f);
    o->setProperty ("bpm", std::round (proc.currentBpm() * 10.0) / 10.0);
    o->setProperty ("midiIn", proc.midiIn());
    emit ("mbState", juce::var (o));
}
