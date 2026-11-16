#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "BassEngine.h"

// MIND BASS: instrumento. Toca su propio patrón de 4 compases sincronizado con Ableton (o con PLAY en la app),
// y también las notas MIDI que le lleguen (si llegan notas, el patrón propio se calla). El MIDI o el audio se arrastran a Ableton.
class MBProcessor : public juce::AudioProcessor,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::AsyncUpdater
{
public:
    MBProcessor();
    ~MBProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;

    // ---- hilo de mensajes ----
    void setParam (const juce::String& id, float value);
    void gesture (const juce::String& id, bool begin);
    juce::var stateVar() const;
    juce::var patternVar() const;
    void setPatternVar (const juce::var&);
    void togglePlay();
    void stopPlay();
    juce::File exportClip (bool audio);
    juce::String clipName() const;
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    double currentBpm() const;
    int playState() const;              // 0 parado, 1 PLAY propio, 2 sonando con Ableton
    float playPos() const { return (float) (posBeats.load() / mbass::kLoopBeats); }
    bool midiIn() const { return midiActive.load(); }

    std::function<void()> onPatternChanged;

    bool lightTheme = false, dragAudio = false;
    void saveGlobal();

    // pruebas: patrón y estado directos
    mbass::Pattern getPattern() const { return pattern; }
    void setPattern (const mbass::Pattern&);

private:
    struct Events { std::vector<mbass::Event> ev; };
    // KEY, SWING y GATE rehacen los eventos en el hilo de mensajes (la automatización llega por el hilo de audio)
    void parameterChanged (const juce::String&, float) override
    {
        if (juce::MessageManager::existsAndIsCurrentThread()) rebuild(); else triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override { rebuild(); }
    void rebuild();                     // patrón + swing + gate + key -> eventos para el audio
    void loadGlobal();
    mbass::SoundParams sound() const;
    void handleEvent (const mbass::Event&);

    mbass::Pattern pattern = mbass::defaultPattern();     // solo hilo de mensajes
    juce::int64 seedA = 1, seedV = 1;
    juce::String style { "bcn" };   // estilo del generador (lo usa la interfaz): bcn, solid, rio, bailesito, sintos
    // sonido CUSTOM (NEW SOUND): h2 h3 h4 h5 fmRatio fmIndex fmSus fmDecay pitch pitchDecay
    std::array<std::atomic<float>, 10> custom { 0.3f, 0.1f, 0.0f, 0.0f, 1.0f, 0.8f, 0.4f, 0.08f, 0.0f, 0.02f };

    // tres búferes de eventos que rotan: el audio lee el último publicado
    std::array<Events, 3> evBufs;
    std::atomic<int> evIdx { 0 };
    int writeIdx = 1;

    mbass::Synth synth;
    std::vector<float> gainBuf;
    double sr = 48000.0;
    std::atomic<bool> previewOn { false }, hostPlaying { false }, midiActive { false };
    std::atomic<double> hostBpm { 128.0 }, posBeats { 0.0 };
    std::atomic<bool> hostInfo { false };
    double previewPpq = 0.0, lastEnd = -1.0;
    bool wasRunning = false;
    juce::int64 samplesSinceMidi = 1 << 30;
    int midiHeld = 0;

    std::atomic<float>* pKey = nullptr; std::atomic<float>* pScale = nullptr; std::atomic<float>* pSwing = nullptr;
    std::atomic<float>* pGate = nullptr; std::atomic<float>* pKick = nullptr; std::atomic<float>* pShape = nullptr;
    std::atomic<float>* pDrive = nullptr; std::atomic<float>* pMid = nullptr; std::atomic<float>* pFilter = nullptr;
    std::atomic<float>* pDecay = nullptr; std::atomic<float>* pSbpm = nullptr; std::atomic<float>* pOct = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBProcessor)
};
