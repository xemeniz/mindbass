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
juce::String keyName (int key, int scale);   // scale: 0 MINOR, 1 MAJOR, 2 PHRYGIAN, 3 DORIAN

struct Note { double beat, len; int note; float vel; };
// Notas de un ciclo de 4 compases con swing y gate aplicados (beat en [0,16), len recortada antes de la siguiente nota).
// octave: 0-2 sobre la octava 0 de Ableton (el estilo RIO, por ejemplo, va en la 1)
std::vector<Note> buildNotes (const Pattern&, int key, double swing, double gate, int octave = 0);

struct Event { double beat; int note; float vel; bool on; };
// Lo mismo como eventos de un bucle (los note-off que pasan del final vuelven al principio). Ordenados, note-off primero.
std::vector<Event> buildEvents (const std::vector<Note>&);

// Sidechain sincronizado: en cada negra el bajo baja de golpe y vuelve en ~80 ms.
float duckGain (double beatPos, double secPerBeat, float depth);
inline float duckDepth (float kick) { return juce::jmin (1.0f, kick * 2.0f) * 0.85f; }

// Timbre de los sonidos propios: seno con armónicos, FM (fase) con envolvente y golpe de tono al empezar.
struct Timbre
{
    float h2 = 0, h3 = 0, h4 = 0, h5 = 0;               // armónicos sobre el seno (0-1)
    float fmRatio = 1, fmIndex = 0, fmSus = 1, fmDecay = 0.1f;   // FM: índice que cae de fmIndex a fmIndex·fmSus en fmDecay s
    float pitch = 0, pitchDecay = 0.02f;                // golpe de tono en semitonos que cae en pitchDecay s
};
// Sonidos: 0 SINE, 1 TRI, 2 SAW, 3 SQUARE, 4 ROUND, 5 ORGAN, 6 FM SUB, 7 PLUCK, 8 CUSTOM (NEW SOUND)
constexpr int kNumShapes = 9;
const Timbre& presetTimbre (int shape);
struct SoundParams { int shape = 0; float drive = 0.3f, mid = 0.15f, filter = 0.4f, decay = 0.9f; Timbre custom; };

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
    const Timbre& timbre() const { return p.shape == 8 ? p.custom : presetTimbre (p.shape); }

    double sr = 48000.0, phase = 0.0, inc = 0.0;
    float hNorm = 1.0f, fmMul = 0.0f, pitchMul = 0.0f, fmEnv = 0.0f, pitchEnv = 0.0f;
    SoundParams p;
    float driveG = 1.0f, driveNorm = 1.0f, driveComp = 1.0f;
    float env = 0.0f, vel = 0.0f, atkInc = 0.0f, decMul = 1.0f, relMul = 0.0f, fadeStep = 0.0f;
    bool sustain = true, paramsReady = false;
    Stage stage = Idle;
    int curNote = -1, nextNote = -1, fadeLeft = 0;
    float nextVel = 0.0f;
    SVF lp, mid;
};

juce::MidiFile makeMidi (const Pattern&, int key, double swing, double gate, int octave = 0);
void renderAudio (juce::AudioBuffer<float>& out, double sr, const Pattern&, int key, double swing, double gate,
                  double bpm, const SoundParams&, float kick, int octave = 0);
}
