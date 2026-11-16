#pragma GCC diagnostic ignored "-Wfloat-equal"
// Prueba del procesador de MIND BASS (sin interfaz): reloj de Ableton, PLAY propio, MIDI de entrada, guardar/abrir y exportar.
#include "../Source/PluginProcessor.h"
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FALLO: " __VA_ARGS__); std::printf ("\n"); } } while (0)

struct FakeHost : juce::AudioPlayHead
{
    double bpm = 128.0, ppq = 0.0; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        return p;
    }
};

static constexpr double SR = 48000.0;
static constexpr int BS = 512;

// Procesa "beats" tiempos y devuelve el canal izquierdo (y comprueba que el derecho es igual).
static std::vector<float> run (MBProcessor& p, FakeHost* host, double beats, bool& mono, juce::MidiBuffer* firstMidi = nullptr)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, BS);
    const int blocks = (int) std::ceil (beats * 60.0 / (host ? host->bpm : 128.0) * SR / BS);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0 && firstMidi != nullptr) midi = *firstMidi;
        p.processBlock (buf, midi);
        for (int i = 0; i < BS; ++i) { out.push_back (buf.getSample (0, i)); if (buf.getSample (0, i) != buf.getSample (1, i)) mono = false; }
        if (host != nullptr && host->playing) host->ppq += BS * host->bpm / 60.0 / SR;
    }
    return out;
}

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b && i < x.size(); ++i) e += (double) x[i] * x[i];
    return std::sqrt (e / (double) juce::jmax ((size_t) 1, b - a));
}

static double freqZC (const std::vector<float>& x, size_t a, size_t b)
{
    double first = -1, last = -1; int n = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) { const double t = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]); if (first < 0) first = t; else ++n; last = t; }
    return n > 0 ? n * SR / (last - first) : 0.0;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("MIND BASS · prueba del procesador\n");

    MBProcessor p;
    FakeHost host;
    p.setPlayHead (&host);
    p.prepareToPlay (SR, BS);
    CHECK (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "es un instrumento que acepta MIDI");
    CHECK (p.getTotalNumOutputChannels() == 2 && p.getTotalNumInputChannels() == 0, "sin entrada de audio, salida estéreo");

    // ---------- parado: silencio ----------
    bool mono = true;
    auto x = run (p, &host, 4, mono);
    CHECK (rms (x, 0, x.size()) < 1e-6, "con Ableton parado no suena");
    CHECK (p.playState() == 0, "estado parado");

    // ---------- Ableton en marcha: toca el patrón a tiempo ----------
    host.playing = true; host.ppq = 0.0;
    x = run (p, &host, 16, mono);
    CHECK (p.playState() == 2, "estado: sonando con Ableton");
    CHECK (rms (x, 0, x.size()) > 0.05, "suena con Ableton (rms %.3f)", rms (x, 0, x.size()));
    CHECK (mono, "bajo mono (L = R)");
    {
        // el patrón por defecto empieza en el 1 con la tónica F0: primeros 200 ms
        const size_t spb = (size_t) (60.0 / 128.0 * SR);
        CHECK (rms (x, 10, spb / 8) > 0.02, "la primera nota cae en el 1");
        float pk = 0; for (auto v : x) pk = juce::jmax (pk, std::abs (v));
        CHECK (pk < 0.9f, "sin saturar la salida (pico %.3f)", pk);
    }
    CHECK (std::abs (p.currentBpm() - 128.0) < 1e-6, "BPM de Ableton");
    host.bpm = 124.0;
    run (p, &host, 1, mono);
    CHECK (std::abs (p.currentBpm() - 124.0) < 1e-6, "sigue el cambio de BPM");
    host.bpm = 128.0;

    // ---------- MIDI de entrada: el patrón se calla y suena la nota que llega ----------
    {
        host.playing = true;
        juce::MidiBuffer in; in.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 110), 0);   // A0 = 55 Hz
        x = run (p, &host, 2, mono, &in);
        CHECK (p.midiIn(), "detecta MIDI de entrada");
        const double f = freqZC (x, (size_t) (0.1 * SR), (size_t) (0.4 * SR));
        CHECK (std::abs (f - 55.0) < 1.0, "suena la nota que llega (A0 = 55 Hz): %.2f Hz", f);
        juce::MidiBuffer offm; offm.addEvent (juce::MidiMessage::noteOff (1, 33), 0);
        run (p, &host, 1, mono, &offm);
        CHECK (p.midiIn(), "sigue callado justo después de soltar");
        run (p, &host, 6, mono);
        CHECK (! p.midiIn(), "4 tiempos después vuelve el patrón");
        x = run (p, &host, 4, mono);
        CHECK (rms (x, 0, x.size()) > 0.05, "el patrón vuelve a sonar");
    }

    // ---------- PLAY propio (sin Ableton) ----------
    host.playing = false;
    run (p, &host, 1, mono);
    p.togglePlay();
    x = run (p, &host, 4, mono);
    CHECK (p.playState() == 1, "PLAY propio con Ableton parado");
    CHECK (rms (x, 0, x.size()) > 0.05, "suena con su PLAY");
    CHECK (p.playPos() > 0.2f && p.playPos() < 0.3f, "cabezal en el compás 2 (%.3f)", p.playPos());
    host.playing = true; host.ppq = 0;
    run (p, &host, 1, mono);
    CHECK (p.playState() == 2, "si arranca Ableton, manda Ableton");
    host.playing = false;
    run (p, &host, 1, mono);
    CHECK (p.playState() == 0, "y al parar Ableton se para");
    p.togglePlay(); run (p, &host, 1, mono); p.stopPlay();
    x = run (p, &host, 1, mono);
    CHECK (rms (x, (size_t) (0.15 * SR), x.size()) < 1e-4, "STOP lo calla (release de 10 ms)");

    // ---------- patrón desde la interfaz, KEY, guardar y abrir ----------
    {
        mbass::Pattern pat;
        pat[0] = { true, 0, 0.9f, 1.0f, true }; pat[3] = { true, 7, 0.8f, 1.2f, false }; pat[18] = { true, -2, 0.7f, 1.0f, true };
        p.setPattern (pat);
        p.setParam ("key", 9.0f);   // A
        p.setParam ("swing", 0.6f);
        p.setParam ("scale", 3.0f);   // DORIAN
        p.setParam ("shape", 8.0f);   // CUSTOM
        auto pv = p.patternVar();
        pv.getDynamicObject()->setProperty ("custom", juce::JSON::parse ("[0.5,0.2,0.1,0,2,1.5,0.3,0.05,5,0.01]"));
        pv.getDynamicObject()->setProperty ("style", "bailesito");
        p.setParam ("octave", 1.0f);
        p.setPatternVar (pv);
        juce::MemoryBlock st; p.getStateInformation (st);
        MBProcessor q;
        int notified = 0; q.onPatternChanged = [&] { ++notified; };
        q.setStateInformation (st.getData(), (int) st.getSize());
        auto qp = q.getPattern();
        CHECK (qp[0].on && qp[3].on && qp[3].off == 7 && qp[18].off == -2 && qp[18].lock && ! qp[3].lock && ! qp[1].on, "el patrón se guarda con el proyecto (notas, intervalos y candados)");
        CHECK (std::abs (qp[3].len - 1.2f) < 1e-6f && std::abs (qp[18].vel - 0.7f) < 1e-6f, "y también velocidad y largo");
        CHECK ((int) q.apvts.getRawParameterValue ("key")->load() == 9, "KEY se guarda");
        CHECK (std::abs (q.apvts.getRawParameterValue ("swing")->load() - 0.6f) < 1e-3f, "SWING se guarda");
        CHECK (notified == 1, "al abrir el proyecto avisa a la interfaz");
        CHECK ((int) q.apvts.getRawParameterValue ("scale")->load() == 3, "la escala (DORIAN) se guarda");
        CHECK ((int) q.apvts.getRawParameterValue ("shape")->load() == 8, "el sonido CUSTOM se guarda");
        CHECK (q.patternVar().getProperty ("style", "").toString() == "bailesito", "el estilo (BAILESITO) se guarda con el proyecto");
        CHECK ((int) q.apvts.getRawParameterValue ("octave")->load() == 1, "la octava se guarda");
        auto qc = q.patternVar()["custom"];
        CHECK (qc.size() == 10 && std::abs ((double) qc[0] - 0.5) < 1e-6 && std::abs ((double) qc[8] - 5.0) < 1e-6, "y su timbre (NEW SOUND) también");
        auto bad2 = q.patternVar(); bad2.getDynamicObject()->setProperty ("custom", juce::JSON::parse ("[9,9,9,9,99,99,9,9,99,9]"));
        q.setPatternVar (bad2);
        auto qc2 = q.patternVar()["custom"];
        CHECK ((double) qc2[5] <= 4.0 && (double) qc2[8] <= 12.0 && (double) qc2[4] <= 4.0, "un timbre fuera de rango se limita");

        // patrón como JSON (lo que manda la interfaz)
        auto v = p.patternVar();
        CHECK (v["steps"].size() == 128, "la interfaz recibe 128 pasos (hasta 8 compases)");
        {
            auto old = juce::JSON::parse (juce::JSON::toString (v));
            auto* st = old.getProperty ("steps", juce::var()).getArray();
            st->removeRange (64, 64);
            MBProcessor o; o.setPatternVar (old);
            CHECK (o.getPattern()[18].off == -2 && ! o.getPattern()[70].on, "abre proyectos de la 0.3 (64 pasos)");
        }
        MBProcessor r; r.setPatternVar (v);
        CHECK (r.getPattern()[18].off == -2, "y lo puede devolver tal cual");
        juce::var bad = juce::JSON::parse ("{\"steps\":[[1,0,1,1,0]]}");
        r.setPatternVar (bad);
        CHECK (r.getPattern()[18].off == -2, "un patrón roto no se carga");

        // al tocar con KEY = A la primera nota es A1 con OCTAVE 1 (110 Hz) y A0 con OCTAVE 0 (55 Hz)
        host.playing = true; host.ppq = 0;
        x = run (p, &host, 1, mono);
        const double f1 = freqZC (x, (size_t) (0.01 * SR), (size_t) (0.07 * SR));
        CHECK (std::abs (f1 - 110.0) < 5.0, "KEY A + OCTAVE 1: la primera nota es A1 (%.1f Hz)", f1);
        p.setParam ("octave", 0.0f);
        host.playing = false; run (p, &host, 1, mono);
        host.playing = true; host.ppq = 0;
        x = run (p, &host, 1, mono);
        const double f = freqZC (x, (size_t) (0.01 * SR), (size_t) (0.07 * SR));
        CHECK (std::abs (f - 55.0) < 3.0, "KEY A: la primera nota es A0 (%.1f Hz)", f);
        host.playing = false;
    }

    // ---------- exportar (arrastrar a Ableton) ----------
    {
        auto mid = p.exportClip (false), wav = p.exportClip (true);
        CHECK (mid.existsAsFile() && mid.getFileExtension() == ".mid", "MIDI exportado: %s", mid.getFileName().toRawUTF8());
        CHECK (wav.existsAsFile() && wav.getFileExtension() == ".wav", "WAV exportado: %s", wav.getFileName().toRawUTF8());
        CHECK (mid.getFileName().startsWith ("MIND BASS A DOR"), "el nombre lleva la tonalidad y la escala (%s)", mid.getFileName().toRawUTF8());
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatReader> rd (fmt.createReaderFor (wav.createInputStream().release(), true));
        CHECK (rd != nullptr && rd->numChannels == 2 && rd->bitsPerSample == 24, "WAV estéreo de 24 bits");
        if (rd) CHECK (std::abs ((double) rd->lengthInSamples - 16 * 60.0 / 128.0 * SR) < 2, "4 compases a 128 BPM (%lld muestras)", (long long) rd->lengthInSamples);
        juce::MidiFile mf; juce::FileInputStream is (mid);
        CHECK (mf.readFrom (is) && mf.getTrack (0) != nullptr, "el MIDI se abre");
        auto again = p.exportClip (false);
        CHECK (again.existsAsFile() && again != mid, "cada arrastre con nombre distinto (%s)", again.getFileName().toRawUTF8());
        mid.deleteFile(); wav.deleteFile(); again.deleteFile();
    }

    p.setPlayHead (nullptr);
    std::printf ("%d comprobaciones, %d fallos\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
