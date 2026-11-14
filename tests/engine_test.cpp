// Prueba del motor de MIND BASS sin host ni interfaz.
#include "../Source/BassEngine.h"
#include <cstdio>
#include <cmath>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FALLO: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static const double SR = 48000.0;
static const double kPi = 3.14159265358979323846;

// Una nota sostenida (DECAY 100 %) durante "sec" segundos, sin sidechain.
static std::vector<float> hold (int note, mbass::SoundParams sp, double sec, double sr = SR)
{
    mbass::Synth s; s.prepare (sr); s.setParams (sp);
    std::vector<float> out ((size_t) (sec * sr), 0.0f);
    s.noteOn (note, 1.0f);
    s.render (out.data(), (int) out.size(), nullptr);
    return out;
}

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
    return std::sqrt (e / (double) juce::jmax ((size_t) 1, b - a));
}

// Frecuencia por cruces por cero (subida) con interpolación lineal.
static double freqZC (const std::vector<float>& x, size_t a, size_t b, double sr)
{
    double first = -1, last = -1; int n = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
            if (first < 0) first = t; else ++n;
            last = t;
        }
    return n > 0 ? n * sr / (last - first) : 0.0;
}

// Energía de una frecuencia concreta (Goertzel), para ver qué armónico manda.
static double tone (const std::vector<float>& x, size_t a, size_t b, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) { const double ph = 2 * kPi * f * (double) (i - a) / sr; re += x[i] * std::cos (ph); im += x[i] * std::sin (ph); }
    return std::sqrt (re * re + im * im) / (double) (b - a);
}

static float maxStep (const float* x, int n)
{
    float m = 0; for (int i = 1; i < n; ++i) m = juce::jmax (m, std::abs (x[i] - x[i - 1])); return m;
}

int main()
{
    std::printf ("MIND BASS · prueba del motor\n");

    // ---------- tónica y afinación ----------
    CHECK (mbass::rootNote (5) == 29, "F = MIDI 29 (F0 de Ableton), da %d", mbass::rootNote (5));
    CHECK (mbass::rootNote (0) == 24, "C = MIDI 24 (C0 de Ableton)");
    CHECK (mbass::keyName (5, true) == "F MIN" && mbass::keyName (10, false) == "A# MAJ", "nombres de tonalidad");
    {
        mbass::SoundParams sp; sp.decay = 1.0f; sp.drive = 0.3f; sp.filter = 0.4f;
        for (int shape = 0; shape < 3; ++shape)
        {
            sp.shape = shape;
            auto x = hold (29, sp, 2.0);
            const double f = freqZC (x, (size_t) (0.3 * SR), x.size(), SR);
            CHECK (std::abs (f - 43.654) < 0.15, "F0 suena a 43,65 Hz (forma %d): %.3f Hz", shape, f);
            const double f0 = tone (x, (size_t) (0.3 * SR), x.size(), 43.654, SR), f1 = tone (x, (size_t) (0.3 * SR), x.size(), 87.307, SR);
            CHECK (f0 > f1 * 2.0, "la fundamental manda (forma %d): F0 %.4f vs 2.º armónico %.4f", shape, f0, f1);
        }
        std::printf ("  F0 (seno): %.3f Hz\n", freqZC (hold (29, mbass::SoundParams { 0, 0.3f, 0.15f, 0.4f, 1.0f }, 2.0), (size_t) (0.3 * SR), (size_t) (2.0 * SR), SR));
    }

    // ---------- DRIVE nunca sube el volumen ----------
    for (int shape = 0; shape < 3; ++shape)
        for (int note : { 24, 29, 35, 41 })
        {
            mbass::SoundParams a; a.shape = shape; a.decay = 1.0f; a.drive = 0.0f; a.mid = 0.0f; a.filter = 1.0f;
            mbass::SoundParams b = a; b.drive = 1.0f;
            auto xa = hold (note, a, 1.0), xb = hold (note, b, 1.0);
            const double ra = rms (xa, (size_t) (0.1 * SR), xa.size()), rb = rms (xb, (size_t) (0.1 * SR), xb.size());
            CHECK (rb <= ra * 1.03, "DRIVE 100 %% no sube el volumen (forma %d, nota %d): %.4f -> %.4f", shape, note, ra, rb);
            // con el filtro por defecto tampoco
            a.filter = b.filter = 0.4f; a.mid = b.mid = 0.15f;
            xa = hold (note, a, 1.0); xb = hold (note, b, 1.0);
            const double ra2 = rms (xa, (size_t) (0.1 * SR), xa.size()), rb2 = rms (xb, (size_t) (0.1 * SR), xb.size());
            CHECK (rb2 <= ra2 * 1.03, "DRIVE 100 %% con FILTER/MID por defecto no sube (forma %d, nota %d): %.4f -> %.4f", shape, note, ra2, rb2);
        }

    // ---------- nunca satura la salida ----------
    {
        mbass::SoundParams sp; sp.shape = 2; sp.drive = 1.0f; sp.mid = 1.0f; sp.filter = 1.0f; sp.decay = 1.0f;
        auto x = hold (36, sp, 0.5);
        float pk = 0; for (auto v : x) pk = juce::jmax (pk, std::abs (v));
        CHECK (pk < 0.9f, "pico por debajo de 0 dBFS con todo al máximo: %.3f", pk);
    }

    // ---------- sin clics ----------
    {
        mbass::SoundParams sp;   // por defecto
        auto ref = hold (29, mbass::SoundParams { 0, sp.drive, sp.mid, sp.filter, 1.0f }, 1.0);
        const float steady = maxStep (ref.data() + (int) (0.2 * SR), (int) (0.8 * SR));
        // patrón por defecto con todas sus notas, retriggers y saltos de octava
        auto pat = mbass::defaultPattern();
        juce::AudioBuffer<float> buf;
        for (int shape = 0; shape < 2; ++shape)
        {
            sp.shape = shape; sp.decay = 1.0f;
            mbass::renderAudio (buf, SR, pat, 5, 0.57, 0.7, 128.0, sp, 0.0f);
            // el salto de +12 dobla la pendiente; 2,6x da margen a la octava y nada más
            const float m = maxStep (buf.getReadPointer (0), buf.getNumSamples());
            CHECK (m < steady * 2.6f, "sin clics en el patrón (forma %d): salto máx %.4f vs nota sostenida %.4f", shape, m, steady);
        }
        // notas pegadas (retrigger sin hueco): fundido de 2 ms y fase a 0
        mbass::Synth s; s.prepare (SR); sp.shape = 0; s.setParams (sp);
        std::vector<float> out ((size_t) SR, 0.0f);
        s.noteOn (29, 1.0f); s.render (out.data(), 4000, nullptr);
        s.noteOn (36, 1.0f); s.render (out.data() + 4000, 4000, nullptr);
        s.noteOn (41, 1.0f); s.render (out.data() + 8000, 4000, nullptr);
        s.allOff(); s.render (out.data() + 12000, 8000, nullptr);
        const float m = maxStep (out.data(), 20000);
        const float st41 = maxStep (hold (41, sp, 0.3).data() + 4000, 8000);
        CHECK (m < st41 * 1.3f, "retrigger y note-off sin clics: %.4f vs %.4f", m, st41);
        CHECK (! s.active(), "tras allOff el sinte se apaga");
        CHECK (std::abs (out[19999]) < 1.0e-4f, "silencio al final");
    }

    // ---------- sidechain ----------
    {
        CHECK (mbass::duckDepth (0.0f) == 0.0f && std::abs (mbass::duckDepth (0.5f) - 0.85f) < 1e-6f && std::abs (mbass::duckDepth (1.0f) - 0.85f) < 1e-6f, "profundidad = min(1, kick·2)·0,85");
        const double spb = 60.0 / 128.0;
        const float d = mbass::duckDepth (0.45f);
        CHECK (std::abs (mbass::duckGain (0.0, spb, d) - 1.0f) < 1e-6f, "el bajo arranca la negra entero (ataque de 4 ms)");
        CHECK (std::abs (mbass::duckGain (0.004 / spb, spb, d) - (1.0f - d)) < 0.01f, "a los 4 ms está abajo del todo: %.3f", mbass::duckGain (0.004 / spb, spb, d));
        CHECK (mbass::duckGain (0.45, spb, d) > 0.95f, "a mitad de la negra ha vuelto: %.3f", mbass::duckGain (0.45, spb, d));
        CHECK (std::abs (mbass::duckGain (2.0 + 0.004 / spb, spb, d) - mbass::duckGain (0.004 / spb, spb, d)) < 1e-4f, "se repite en cada negra");
        CHECK (mbass::duckGain (0.004 / spb, spb, 0.0f) == 1.0f, "KICK 0 = sin sidechain");

        // en el audio: nota larga, la energía justo después de cada negra es mucho menor que a mitad
        mbass::Pattern p; p[0].on = true; p[0].len = 2.0f;
        for (int i = 4; i < 64; i += 4) { p[(size_t) i].on = true; p[(size_t) i].len = 2.0f; }
        juce::AudioBuffer<float> buf;
        mbass::SoundParams sp; sp.decay = 1.0f;
        mbass::renderAudio (buf, SR, p, 5, 0.5, 1.0, 128.0, sp, 0.6f);
        std::vector<float> x (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        const size_t beat = (size_t) (spb * SR);
        const double dip = rms (x, beat * 4 + (size_t) (0.006 * SR), beat * 4 + (size_t) (0.030 * SR));
        const double mid = rms (x, beat * 4 + (size_t) (0.12 * SR), beat * 4 + (size_t) (0.22 * SR));   // la nota dura media negra
        CHECK (dip < mid * 0.6, "sidechain audible: %.4f justo después del kick vs %.4f a mitad", dip, mid);
        CHECK (std::abs (buf.getSample (0, 1000) - buf.getSample (1, 1000)) < 1e-9f, "WAV mono en los dos canales");
        CHECK (buf.getNumSamples() == (int) std::round (16 * spb * SR), "WAV de 4 compases exactos a 128 BPM");
    }

    // ---------- notas: swing, gate y nunca se pisan ----------
    {
        auto pat = mbass::defaultPattern();
        for (double sw : { 0.5, 0.57, 0.75 })
            for (double gate : { 0.3, 0.7, 1.0 })
            {
                auto ns = mbass::buildNotes (pat, 5, sw, gate);
                int on = 0; for (auto& s : pat) on += s.on ? 1 : 0;
                CHECK ((int) ns.size() == on, "una nota por paso activo (%d vs %d)", (int) ns.size(), on);
                bool mono = true, inLoop = true;
                for (size_t i = 0; i < ns.size(); ++i)
                {
                    const double next = i + 1 < ns.size() ? ns[i + 1].beat : ns[0].beat + 16.0;
                    if (ns[i].beat + ns[i].len > next - 0.005) mono = false;
                    if (ns[i].beat < 0 || ns[i].beat >= 16.0) inLoop = false;
                }
                CHECK (mono, "las notas nunca se pisan (swing %.2f, gate %.1f)", sw, gate);
                CHECK (inLoop, "todas dentro del bucle de 4 compases");
            }
        auto ns = mbass::buildNotes (pat, 5, 0.57, 0.7);
        const double sw = (0.57 - 0.5) * 0.5;
        bool okSw = true;
        for (auto& n : ns)
        {
            const double st = n.beat / 0.25, fr = st - std::floor (st + 1e-9);
            const int step = (int) std::floor (st + 1e-9);
            if ((step & 1) ? std::abs (fr - sw / 0.25) > 1e-6 : fr > 1e-6) okSw = false;
        }
        CHECK (okSw, "swing 57 %%: solo se retrasan las semicorcheas pares (%.1f ms a 128 BPM)", sw * 60.0 / 128.0 * 1000.0);
        const auto& first = ns.front();
        CHECK (first.note == 29 && std::abs (first.len - 0.25 * 0.7) < 1e-9, "primera nota F0 al 70 %% de la semicorchea");
    }

    // ---------- eventos del bucle ----------
    {
        auto ev = mbass::buildEvents (mbass::buildNotes (mbass::defaultPattern(), 5, 0.57, 0.7));
        bool sorted = true; for (size_t i = 1; i < ev.size(); ++i) if (ev[i].beat < ev[i - 1].beat) sorted = false;
        CHECK (sorted, "eventos ordenados");
        int ons = 0, offs = 0; for (auto& e : ev) (e.on ? ons : offs)++;
        CHECK (ons == offs && ons > 0, "un note-off por cada note-on (%d/%d)", ons, offs);
    }

    // ---------- MIDI ----------
    {
        auto pat = mbass::defaultPattern();
        auto mf = mbass::makeMidi (pat, 5, 0.57, 0.7);
        juce::MemoryBlock mb; { juce::MemoryOutputStream os (mb, false); CHECK (mf.writeTo (os), "el MIDI se escribe"); }
        juce::MidiFile rd; juce::MemoryInputStream is (mb, false);
        CHECK (rd.readFrom (is), "el MIDI se vuelve a leer");
        CHECK (rd.getTimeFormat() == 960, "960 ticks por negra");
        CHECK (rd.getNumTracks() == 1, "una pista");
        auto* tr = rd.getTrack (0);
        int ons = 0, offs = 0, badPair = 0, badSwing = 0, badRange = 0;
        double lastTick = 0;
        for (int i = 0; i < tr->getNumEvents(); ++i)
        {
            const auto& m = tr->getEventPointer (i)->message;
            lastTick = juce::jmax (lastTick, m.getTimeStamp());
            if (m.isNoteOn())
            {
                ++ons;
                const double t = m.getTimeStamp(), rem = std::fmod (t, 240.0);
                const int step = (int) (t / 240.0);
                if ((step & 1) ? std::abs (rem - 34.0) > 1.0 : rem > 0.5) ++badSwing;
                if (m.getNoteNumber() < 24 || m.getNoteNumber() > 48) ++badRange;
                auto off = tr->getIndexOfMatchingKeyUp (i);
                if (off < 0 || tr->getEventPointer (off)->message.getTimeStamp() <= t) ++badPair;
            }
            else if (m.isNoteOff()) ++offs;
        }
        int on = 0; for (auto& s : pat) on += s.on ? 1 : 0;
        CHECK (ons == on && offs == on, "%d notas en el MIDI (%d note-on, %d note-off)", on, ons, offs);
        CHECK (badPair == 0, "cada nota tiene su final después del principio");
        CHECK (badSwing == 0, "swing aplicado en el MIDI: pares +34 ticks (%d mal)", badSwing);
        CHECK (badRange == 0, "todas las notas entre C0 y C2 de Ableton");
        CHECK (lastTick <= 16 * 960, "el clip dura 4 compases (%.0f ticks)", lastTick);
    }

    std::printf ("%d comprobaciones, %d fallos\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
