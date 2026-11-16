#include "BassEngine.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace mbass
{
static constexpr double kPi = 3.14159265358979323846;

Pattern defaultPattern()
{
    // Grupos de 2-4 semicorcheas con silencios, tónica casi siempre, un semitono y una 5ª; 4.º compás variado.
    Pattern p;
    const int on[] = { 0, 2, 3, 5, 6, 8, 10, 11, 13, 14 };
    for (int bar = 0; bar < 4; ++bar)
        for (int s : on)
        {
            auto& st = p[(size_t) (bar * 16 + s)];
            st.on = true; st.off = 0;
            st.vel = (s == 0 || s == 5 || s == 10) ? 0.95f : 0.8f;
            st.len = (s == 3 || s == 8 || s == 14) ? 1.2f : 1.0f;
        }
    p[6].off = 1; p[22].off = -1; p[38].off = 1;
    p[13].off = 7; p[45].off = 7;
    p[56].off = 3; p[59].off = 1; p[61].on = true; p[61].off = 2; p[61].vel = 0.85f; p[62].off = 10; p[63].on = true; p[63].off = 12; p[63].vel = 0.8f;
    return p;
}

juce::String keyName (int key, int scale)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* sc[] = { " MIN", " MAJ", " PHRYG", " DOR" };
    return juce::String (n[juce::jlimit (0, 11, key)]) + sc[juce::jlimit (0, 3, scale)];
}

const Timbre& presetTimbre (int shape)
{
    static const Timbre none;
    static const Timbre t[] = {
        none, none, none, none,
        { 0.25f, 0.08f, 0, 0,        1, 0, 1, 0.1f,           0, 0.02f },     // ROUND: seno redondo con algo de 2.º y 3.º (se oye en altavoces pequeños)
        { 0.5f, 0.25f, 0.2f, 0.06f,  1, 0, 1, 0.1f,           0, 0.02f },     // ORGAN: octava, quinta y doble octava
        { 0, 0, 0, 0,                1, 1.4f, 0.3f, 0.09f,    0, 0.02f },     // FM SUB: gruñido al empezar que se queda en sub
        { 0.1f, 0, 0, 0,             2, 2.6f, 0.0f, 0.035f,   7, 0.012f },    // PLUCK: golpe corto con tono que cae
    };
    return (shape >= 4 && shape <= 7) ? t[shape] : none;
}

// Una muestra del oscilador "seno + armónicos + FM" para la fase ph (0-1) e índice idx.
static inline double timbreWave (const Timbre& t, double ph, double idx)
{
    const double w = 2.0 * kPi * ph;
    double y = std::sin (w + idx * std::sin (w * t.fmRatio));
    if (t.h2 > 0) y += t.h2 * std::sin (2 * w);
    if (t.h3 > 0) y += t.h3 * std::sin (3 * w);
    if (t.h4 > 0) y += t.h4 * std::sin (4 * w);
    if (t.h5 > 0) y += t.h5 * std::sin (5 * w);
    return y;
}

// Forma de onda base para calcular la compensación del DRIVE (sin polyBLEP).
static double rawWave (int shape, const Timbre& t, double ph, double idx)
{
    if (shape == 1) { double u = ph + 0.25; u -= std::floor (u); return 1.0 - 4.0 * std::abs (u - 0.5); }
    if (shape == 2) { double u = ph + 0.5; u -= std::floor (u); return 2.0 * u - 1.0; }
    if (shape == 3) return ph < 0.5 ? 0.8 : -0.8;
    if (shape >= 4) return timbreWave (t, ph, idx);
    return std::sin (2.0 * kPi * ph);
}

std::vector<Note> buildNotes (const Pattern& pat, int key, double swing, double gate, int octave, int bars)
{
    std::vector<Note> notes;
    const int steps = 16 * juce::jlimit (1, 8, bars);
    const double loop = loopBeats (bars);
    const double sw = (juce::jlimit (0.5, 0.75, swing) - 0.5) * 0.5;   // retraso de las semicorcheas pares, en tiempos
    auto onset = [sw] (int i) { return i * 0.25 + ((i & 1) ? sw : 0.0); };
    const int root = rootNote (key) + 12 * juce::jlimit (0, 2, octave);
    for (int i = 0; i < steps; ++i)
    {
        const auto& s = pat[(size_t) i];
        if (! s.on) continue;
        const double b = onset (i);
        // siguiente nota (puede estar en la vuelta del bucle)
        double nb = b + loop;
        for (int j = 1; j <= steps; ++j)
            if (pat[(size_t) ((i + j) % steps)].on) { nb = onset ((i + j) % steps) + ((i + j) >= steps ? loop : 0.0); break; }
        double len = 0.25 * juce::jlimit (0.1, 1.0, gate) * juce::jlimit (0.3f, 2.0f, s.len);
        len = juce::jmax (0.02, juce::jmin (len, nb - b - 0.006));
        notes.push_back ({ b, len, juce::jlimit (0, 127, root + s.off), juce::jlimit (0.05f, 1.0f, s.vel) });
    }
    return notes;
}

std::vector<Event> buildEvents (const std::vector<Note>& notes, int bars)
{
    std::vector<Event> ev;
    ev.reserve (notes.size() * 2);
    for (auto& n : notes)
    {
        ev.push_back ({ n.beat, n.note, n.vel, true });
        ev.push_back ({ std::fmod (n.beat + n.len, loopBeats (bars)), n.note, 0.0f, false });
    }
    std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b)
    {
        if (std::abs (a.beat - b.beat) > 1.0e-9) return a.beat < b.beat;
        return ! a.on && b.on;
    });
    return ev;
}

float duckGain (double beatPos, double secPerBeat, float depth)
{
    if (depth <= 0.0f) return 1.0f;
    const double t = (beatPos - std::floor (beatPos)) * secPerBeat;
    const double a = juce::jmin (1.0, t / 0.004);
    const double d = std::exp (-juce::jmax (0.0, t - 0.004) / 0.075);
    return (float) (1.0 - depth * a * d);
}

// ------------------------------------------------------------------ sinte
void Synth::SVF::set (double fc, double srate, float q)
{
    g = (float) std::tan (kPi * juce::jlimit (10.0, srate * 0.45, fc) / srate);
    k = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
}

void Synth::SVF::tick (float x, float& low, float& band)
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
    low = v2; band = v1;
}

void Synth::prepare (double sampleRate)
{
    sr = sampleRate;
    paramsReady = false;
    atkInc = (float) (1.0 / (0.0025 * sr));
    relMul = (float) std::exp (-1.0 / (0.010 * sr));
    reset();
    setParams (p);
}

static bool sameSound (const SoundParams& a, const SoundParams& b)
{
    static_assert (sizeof (SoundParams) == sizeof (int) + 4 * sizeof (float) + sizeof (Timbre), "SoundParams sin relleno");
    return std::memcmp (&a, &b, sizeof (SoundParams)) == 0;
}

void Synth::setParams (const SoundParams& np)
{
    // se llama en cada bloque: si nada ha cambiado no se recalcula (la compensación del DRIVE cuesta)
    if (paramsReady && sameSound (np, p)) return;
    paramsReady = true;
    p = np;
    lp.set (60.0 * std::pow (2000.0 / 60.0, (double) juce::jlimit (0.0f, 1.0f, p.filter)), sr, 0.707f);
    mid.set (280.0, sr, 0.8f);
    sustain = p.decay >= 0.98f;
    const double tau = 0.03 + (double) p.decay * p.decay * 0.8;
    decMul = (float) std::exp (-1.0 / (tau * sr));
    driveG = 1.0f + juce::jlimit (0.0f, 1.0f, p.drive) * 6.0f;
    driveNorm = 1.0f / std::tanh (driveG);
    {
        const auto& t = timbre();
        // los armónicos se normalizan a la energía del seno: cambiar de sonido no sube el volumen
        hNorm = p.shape >= 4 ? (float) (1.0 / std::sqrt (1.0 + t.h2 * t.h2 + t.h3 * t.h3 + t.h4 * t.h4 + t.h5 * t.h5)) : 1.0f;
        fmMul = (float) std::exp (-1.0 / (juce::jmax (0.002f, t.fmDecay) * sr));
        pitchMul = (float) std::exp (-1.0 / (juce::jmax (0.002f, t.pitchDecay) * sr));

        // DRIVE nunca suena más fuerte: se compensa con la forma de onda elegida para que ni la energía
        // ni la fundamental (lo que queda tras el filtro) suban; solo cambia el color.
        // En los sonidos con FM se mira con el índice al principio y al final, y se queda la más prudente.
        driveComp = 1.0f;
        for (double idx : { (double) t.fmIndex, (double) (t.fmIndex * t.fmSus) })
        {
            double a = 0, b = 0, fr = 0, fi = 0, gr = 0, gi = 0;
            constexpr int N = 1024;
            for (int i = 0; i < N; ++i)
            {
                const double ph = (i + 0.5) / N;
                const double x = rawWave (p.shape, t, ph, idx) * hNorm;
                const double y = std::tanh (driveG * x) * driveNorm;
                a += x * x; b += y * y;
                fr += x * std::cos (2.0 * kPi * ph); fi += x * std::sin (2.0 * kPi * ph);
                gr += y * std::cos (2.0 * kPi * ph); gi += y * std::sin (2.0 * kPi * ph);
            }
            const double fx = std::sqrt (fr * fr + fi * fi), fy = std::sqrt (gr * gr + gi * gi);
            driveComp = juce::jmin (driveComp, (float) juce::jmin (std::sqrt (a / b), fy > 1.0e-9 ? fx / fy : 1.0));
        }
    }
}

void Synth::reset()
{
    stage = Idle; env = 0; curNote = nextNote = -1; phase = 0; lp.reset(); mid.reset();
}

void Synth::start (int note, float v)
{
    curNote = note; vel = v; phase = 0.0;
    inc = 440.0 * std::pow (2.0, (note - 69) / 12.0) / sr;
    fmEnv = 1.0f; pitchEnv = 1.0f;
    stage = Attack; env = 0.0f;
}

void Synth::noteOn (int note, float v)
{
    if (stage == Idle || env < 1.0e-3f) { start (note, v); return; }
    // nota nueva encima de otra: fundido de 2 ms y vuelta a fase 0 (pegada igual en cada nota, sin clics)
    nextNote = note; nextVel = v;
    fadeLeft = juce::jmax (1, (int) (0.002 * sr));
    fadeStep = env / (float) fadeLeft;
    stage = Fade;
}

void Synth::noteOff (int note)
{
    if (stage == Fade && note == nextNote) { nextNote = -1; return; }
    if (note == curNote && (stage == Attack || stage == Body)) stage = Release;
}

void Synth::allOff()
{
    nextNote = -1;
    if (stage == Attack || stage == Body || stage == Fade) stage = Release;
}

float Synth::osc()
{
    const double ph = phase;
    switch (p.shape)
    {
        case 0: return (float) std::sin (2.0 * kPi * ph);
        case 1:   // triángulo, empieza en 0 y subiendo
        {
            double t = ph + 0.25; t -= std::floor (t);
            return (float) (1.0 - 4.0 * std::abs (t - 0.5));
        }
        case 2:   // sierra con polyBLEP, empieza en 0
        {
            double t = ph + 0.5; t -= std::floor (t);
            double y = 2.0 * t - 1.0;
            if (t < inc) { const double x = t / inc; y -= x + x - x * x - 1.0; }
            else if (t > 1.0 - inc) { const double x = (t - 1.0) / inc; y -= x * x + x + x + 1.0; }
            return (float) y;
        }
        case 3:   // cuadrada suave con polyBLEP (al 80 % para igualar energía con el seno)
        {
            auto blep = [this] (double t)
            {
                if (t < inc) { const double x = t / inc; return x + x - x * x - 1.0; }
                if (t > 1.0 - inc) { const double x = (t - 1.0) / inc; return x * x + x + x + 1.0; }
                return 0.0;
            };
            double y = ph < 0.5 ? 1.0 : -1.0;
            double t2 = ph + 0.5; t2 -= std::floor (t2);
            y += blep (ph) - blep (t2);
            return (float) (0.8 * y);
        }
        default:
        {
            const auto& t = timbre();
            const double idx = t.fmIndex * (t.fmSus + (1.0f - t.fmSus) * fmEnv);
            return (float) (timbreWave (t, ph, idx) * hNorm);
        }
    }
}

void Synth::render (float* out, int n, const float* gain)
{
    const float midAmt = juce::jlimit (0.0f, 1.0f, p.mid) * 0.75f;
    const float midNorm = 1.0f / (1.0f + midAmt * 0.35f);
    const float tn = 1.0f / std::tanh (3.5f);
    for (int i = 0; i < n; ++i)
    {
        if (stage == Idle) return;
        switch (stage)
        {
            case Attack:  env += atkInc; if (env >= 1.0f) { env = 1.0f; stage = Body; } break;
            case Body:    if (! sustain) env *= decMul; break;
            case Release: env *= relMul; if (env < 1.0e-4f) { stage = Idle; env = 0; curNote = -1; } break;
            case Fade:
                env -= fadeStep;
                if (--fadeLeft <= 0 || env <= 0.0f)
                {
                    env = 0.0f;
                    if (nextNote >= 0) { start (nextNote, nextVel); nextNote = -1; }
                    else { stage = Idle; curNote = -1; }
                }
                break;
            case Idle: break;
        }
        const float x = osc();
        double step = inc;
        if (p.shape >= 4)
        {
            const auto& t = timbre();
            if (std::abs (t.pitch) > 1.0e-6f) step *= std::pow (2.0, t.pitch * pitchEnv / 12.0);
            fmEnv *= fmMul; pitchEnv *= pitchMul;
        }
        phase += step; while (phase >= 1.0) phase -= 1.0;
        const float sat = std::tanh (driveG * x) * driveNorm * driveComp;
        float lo, bd, mlo, mbd;
        lp.tick (sat, lo, bd);
        mid.tick (std::tanh (3.5f * x) * tn, mlo, mbd);
        const float s = (lo + midAmt * mbd * 1.25f) * midNorm;
        out[i] += s * env * vel * 0.5f * (gain != nullptr ? gain[i] : 1.0f);
    }
}

// ------------------------------------------------------------------ exportar
juce::MidiFile makeMidi (const Pattern& pat, int key, double swing, double gate, int octave, int bars)
{
    constexpr int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    const double loop = loopBeats (bars);
    for (auto& n : buildNotes (pat, key, swing, gate, octave, bars))
    {
        const double end = juce::jmin (loop, n.beat + n.len);
        const auto v = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.vel * 127.0f));
        seq.addEvent (juce::MidiMessage::noteOn (1, n.note, v), std::round (n.beat * tpq));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.note), std::round (end * tpq));
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), loop * tpq);
    seq.updateMatchedPairs();
    juce::MidiFile f;
    f.setTicksPerQuarterNote (tpq);
    f.addTrack (seq);
    return f;
}

void renderAudio (juce::AudioBuffer<float>& out, double sr, const Pattern& pat, int key, double swing, double gate,
                  double bpm, const SoundParams& sp, float kick, int octave, int bars)
{
    const double spb = 60.0 / juce::jlimit (40.0, 250.0, bpm);
    const double loop = loopBeats (bars);
    const int total = (int) std::round (loop * spb * sr);
    out.setSize (2, total);
    out.clear();
    Synth s; s.prepare (sr); s.setParams (sp);
    auto notes = buildNotes (pat, key, swing, gate, octave, bars);
    // el note-off de la última nota puede pasar del final: en el clip se corta en el compás 5
    std::vector<Event> ev;
    for (auto& n : notes) { ev.push_back ({ n.beat, n.note, n.vel, true }); ev.push_back ({ juce::jmin (loop, n.beat + n.len), n.note, 0, false }); }
    std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return std::abs (a.beat - b.beat) > 1e-9 ? a.beat < b.beat : (! a.on && b.on); });
    const float depth = duckDepth (kick);
    std::vector<float> g ((size_t) total);
    for (int i = 0; i < total; ++i) g[(size_t) i] = duckGain (i / (spb * sr), spb, depth);
    auto* L = out.getWritePointer (0);
    int pos = 0;
    size_t e = 0;
    while (pos < total)
    {
        while (e < ev.size() && (int) std::round (ev[e].beat * spb * sr) <= pos)
        {
            if (ev[e].on) s.noteOn (ev[e].note, ev[e].vel); else s.noteOff (ev[e].note);
            ++e;
        }
        const int next = e < ev.size() ? juce::jmin (total, (int) std::round (ev[e].beat * spb * sr)) : total;
        const int len = juce::jmax (1, next - pos);
        s.render (L + pos, juce::jmin (len, total - pos), g.data() + pos);
        pos += len;
    }
    // fundido de 3 ms al final para que el clip en bucle no haga clic
    const int fade = juce::jmin (total, (int) (0.003 * sr));
    for (int i = 0; i < fade; ++i) L[total - 1 - i] *= (float) i / (float) fade;
    out.copyFrom (1, 0, out, 0, 0, total);   // mono en los dos canales
}
}
