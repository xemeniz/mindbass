#pragma once
#include <array>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

// MIND BASS · motor: patrón de 4 compases, sinte de sub mono, sidechain al kick y exportación (MIDI y WAV).
namespace mbass
{
constexpr int kSteps = 64;            // 4 compases de semicorcheas
constexpr double kLoopBeats = 16.0;   // 4 compases de 4/4

struct Step
{
    bool on = false;
    int off = 0;          // semitonos desde la tónica (octava 0 de Ableton)
    float vel = 0.85f;
    float len = 1.0f;     // multiplica al GATE (la última nota de cada grupo es algo más larga)
    bool lock = false;    // se respeta al generar otra vez
};
using Pattern = std::array<Step, kSteps>;

Pattern defaultPattern();                                   // línea de partida en estilo BCN SUB
inline int rootNote (int key) { return 24 + juce::jlimit (0, 11, key); }   // C0 de Ableton = MIDI 24
juce::String keyName (int key, bool minor);

struct Note { double beat, len; int note; float vel; };
// Notas de un ciclo de 4 compases con swing y gate aplicados (beat en [0,16), len recortada antes de la siguiente nota).
std::vector<Note> buildNotes (const Pattern&, int key, double swing, double gate);

struct Event { double beat; int note; float vel; bool on; };
// Lo mismo como eventos de un bucle (los note-off que pasan del final vuelven al principio). Ordenados, note-off primero.
std::vector<Event> buildEvents (const std::vector<Note>&);

// Sidechain sincronizado: en cada negra el bajo baja de golpe y vuelve en ~80 ms.
float duckGain (double beatPos, double secPerBeat, float depth);
inline float duckDepth (float kick) { return juce::jmin (1.0f, kick * 2.0f) * 0.85f; }

struct SoundParams { int shape = 0; float drive = 0.3f, mid = 0.15f, filter = 0.4f, decay = 0.9f; };

class Synth
{
public:
    void prepare (double sampleRate);
    void setParams (const SoundParams&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void allOff();                                   // release corto
    void reset();                                    // silencio inmediato
    void render (float* out, int n, const float* gain);   // mono, se SUMA a out; gain por muestra o nullptr
    bool active() const { return stage != Idle; }
    int note() const { return curNote; }

private:
    struct SVF
    {
        float g = 0, k = 1.414f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void set (double fc, double sr, float q);
        void reset() { ic1 = ic2 = 0; }
        void tick (float x, float& low, float& band);
    };
    enum Stage { Idle, Attack, Body, Release, Fade };
    void start (int note, float vel);
    float osc();

    double sr = 48000.0, phase = 0.0, inc = 0.0;
    SoundParams p;
    float driveG = 1.0f, driveNorm = 1.0f, driveComp = 1.0f;
    float env = 0.0f, vel = 0.0f, atkInc = 0.0f, decMul = 1.0f, relMul = 0.0f, fadeStep = 0.0f;
    bool sustain = true;
    Stage stage = Idle;
    int curNote = -1, nextNote = -1, fadeLeft = 0;
    float nextVel = 0.0f;
    SVF lp, mid;
};

juce::MidiFile makeMidi (const Pattern&, int key, double swing, double gate);
void renderAudio (juce::AudioBuffer<float>& out, double sr, const Pattern&, int key, double swing, double gate,
                  double bpm, const SoundParams&, float kick);
}
