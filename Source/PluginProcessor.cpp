#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout MBProcessor::layout()
{
    APVTS::ParameterLayout l;
    using PID = juce::ParameterID;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
    l.add (std::make_unique<juce::AudioParameterInt> (PID { "key", 1 }, "Key", 0, 11, 5));                     // F
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "scale", 2 }, "Scale", juce::StringArray { "Minor", "Major", "Phrygian", "Dorian" }, 0));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "swing", 1 }, "Swing", juce::NormalisableRange<float> (0.5f, 0.75f, 0.005f), 0.57f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "gate", 1 }, "Gate", juce::NormalisableRange<float> (0.3f, 1.0f), 0.7f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "density", 1 }, "Density", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "move", 1 }, "Move", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "kick", 1 }, "Kick Room", juce::NormalisableRange<float> (0.0f, 1.0f), 0.45f, pct));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "shape", 2 }, "Sound",
        juce::StringArray { "Sine", "Tri", "Saw", "Square", "Round", "Organ", "FM Sub", "Pluck", "Custom" }, 0));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "drive", 1 }, "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "mid", 1 }, "Mid", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "filter", 1 }, "Filter", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "decay", 1 }, "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.9f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "sbpm", 1 }, "BPM (standalone)", juce::NormalisableRange<float> (60.0f, 200.0f, 0.1f), 128.0f));
    return l;
}

MBProcessor::MBProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MINDBASS", layout())
{
    pKey = apvts.getRawParameterValue ("key"); pScale = apvts.getRawParameterValue ("scale"); pSwing = apvts.getRawParameterValue ("swing");
    pGate = apvts.getRawParameterValue ("gate"); pKick = apvts.getRawParameterValue ("kick"); pShape = apvts.getRawParameterValue ("shape");
    pDrive = apvts.getRawParameterValue ("drive"); pMid = apvts.getRawParameterValue ("mid"); pFilter = apvts.getRawParameterValue ("filter");
    pDecay = apvts.getRawParameterValue ("decay"); pSbpm = apvts.getRawParameterValue ("sbpm");
    for (auto id : { "key", "swing", "gate" }) apvts.addParameterListener (id, this);
    loadGlobal();
    rebuild();
}

MBProcessor::~MBProcessor()
{
    cancelPendingUpdate();
    for (auto id : { "key", "swing", "gate" }) apvts.removeParameterListener (id, this);
}

// ------------------------------------------------------------------ ajustes globales
static juce::File globalFile()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support").getChildFile ("MIND BASS");
    d.createDirectory();
    return d.getChildFile ("settings.json");
}

void MBProcessor::loadGlobal()
{
    auto v = juce::JSON::parse (globalFile().loadFileAsString());
    if (! v.isObject()) return;
    lightTheme = v.getProperty ("theme", "dark").toString() == "light";
    dragAudio = v.getProperty ("drag", "midi").toString() == "audio";
}

void MBProcessor::saveGlobal()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("theme", lightTheme ? "light" : "dark");
    o->setProperty ("drag", dragAudio ? "audio" : "midi");
    globalFile().replaceWithText (juce::JSON::toString (juce::var (o), true));
}

// ------------------------------------------------------------------ patrón
void MBProcessor::rebuild()
{
    auto notes = mbass::buildNotes (pattern, (int) pKey->load(), pSwing->load(), pGate->load());
    auto& b = evBufs[(size_t) writeIdx];
    b.ev = mbass::buildEvents (notes);
    evIdx.store (writeIdx);
    writeIdx = (writeIdx + 1) % 3;
}

void MBProcessor::setPattern (const mbass::Pattern& p)
{
    pattern = p;
    rebuild();
}

juce::var MBProcessor::patternVar() const
{
    juce::Array<juce::var> steps;
    for (auto& s : pattern)
    {
        juce::Array<juce::var> a { s.on ? 1 : 0, s.off, std::round (s.vel * 100.0f) / 100.0f, std::round (s.len * 100.0f) / 100.0f, s.lock ? 1 : 0 };
        steps.add (juce::var (a));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("steps", steps);
    o->setProperty ("seedA", seedA);
    o->setProperty ("seedV", seedV);
    juce::Array<juce::var> c;
    for (auto& x : custom) c.add (std::round (x.load() * 1000.0f) / 1000.0f);
    o->setProperty ("custom", c);
    return juce::var (o);
}

void MBProcessor::setPatternVar (const juce::var& v)
{
    auto* a = v.getProperty ("steps", juce::var()).getArray();
    if (a == nullptr || a->size() != mbass::kSteps) return;
    mbass::Pattern p;
    for (int i = 0; i < mbass::kSteps; ++i)
    {
        const auto& s = (*a)[i];
        if (! s.isArray() || s.size() < 5) return;
        auto& st = p[(size_t) i];
        st.on = (int) s[0] != 0;
        st.off = juce::jlimit (-12, 12, (int) s[1]);
        st.vel = juce::jlimit (0.05f, 1.0f, (float) (double) s[2]);
        st.len = juce::jlimit (0.3f, 2.0f, (float) (double) s[3]);
        st.lock = (int) s[4] != 0;
    }
    if (v.hasProperty ("seedA")) seedA = (juce::int64) v.getProperty ("seedA", 1);
    if (v.hasProperty ("seedV")) seedV = (juce::int64) v.getProperty ("seedV", 1);
    if (auto* c = v.getProperty ("custom", juce::var()).getArray(); c != nullptr && c->size() == (int) custom.size())
    {
        // límites seguros para el sonido CUSTOM (NEW SOUND)
        static const float lo[] = { 0, 0, 0, 0, 0.5f, 0, 0, 0.005f, 0, 0.002f }, hi[] = { 1, 1, 1, 1, 4, 4, 1, 1, 12, 0.2f };
        for (size_t i = 0; i < custom.size(); ++i) custom[i] = juce::jlimit (lo[i], hi[i], (float) (double) (*c)[(int) i]);
    }
    setPattern (p);
}

mbass::SoundParams MBProcessor::sound() const
{
    mbass::SoundParams s;
    s.shape = (int) pShape->load(); s.drive = pDrive->load(); s.mid = pMid->load(); s.filter = pFilter->load(); s.decay = pDecay->load();
    auto& t = s.custom;
    t.h2 = custom[0]; t.h3 = custom[1]; t.h4 = custom[2]; t.h5 = custom[3]; t.fmRatio = custom[4]; t.fmIndex = custom[5];
    t.fmSus = custom[6]; t.fmDecay = custom[7]; t.pitch = custom[8]; t.pitchDecay = custom[9];
    return s;
}

// ------------------------------------------------------------------ audio
void MBProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.prepare (sampleRate);
    gainBuf.assign ((size_t) juce::jmax (512, samplesPerBlock * 2), 1.0f);
    lastEnd = -1.0; wasRunning = false; midiHeld = 0; samplesSinceMidi = 1 << 30;
}

bool MBProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& o = layouts.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

double MBProcessor::currentBpm() const
{
    return hostInfo.load() && ! isStandalone() ? hostBpm.load() : (double) pSbpm->load();
}

int MBProcessor::playState() const
{
    if (hostPlaying.load()) return 2;
    return previewOn.load() ? 1 : 0;
}

void MBProcessor::togglePlay()
{
    if (hostPlaying.load()) return;          // con Ableton en marcha manda Ableton
    if (! previewOn.load()) previewPpq = 0.0;
    previewOn = ! previewOn.load();
}

void MBProcessor::stopPlay() { previewOn = false; previewPpq = 0.0; posBeats = 0.0; }

void MBProcessor::handleEvent (const mbass::Event& e)
{
    if (e.on) synth.noteOn (e.note, e.vel); else synth.noteOff (e.note);
}

void MBProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();
    synth.setParams (sound());
    if ((int) gainBuf.size() < n) return;    // no debería pasar (prepareToPlay reserva el doble)

    // ---- reloj: Ableton o el PLAY propio ----
    double bpm = pSbpm->load(), ppq = 0.0;
    bool host = false, info = false;
    if (! isStandalone())
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) { bpm = *b; info = true; }
                if (auto q = pos->getPpqPosition()) ppq = *q; else info = false;
                host = pos->getIsPlaying() && info;
            }
    hostInfo = info; if (info) hostBpm = bpm;
    hostPlaying = host;
    if (host) previewOn = false;
    bpm = juce::jlimit (40.0, 250.0, bpm);
    const double ppqPerSample = bpm / 60.0 / sr;
    const bool preview = previewOn.load();
    const bool running = host || preview;
    if (preview && ! host) ppq = previewPpq;

    if (running != wasRunning || (running && lastEnd >= 0.0 && std::abs (ppq - lastEnd) > 0.01)) synth.allOff();
    wasRunning = running;

    // ---- MIDI que llega: si tocas notas, el patrón propio se calla (hasta 4 tiempos después de la última) ----
    struct Ev { int at; int note; float vel; bool on; };
    juce::Array<Ev> evs;
    bool gotNote = false;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) { evs.add ({ meta.samplePosition, m.getNoteNumber(), m.getFloatVelocity(), true }); ++midiHeld; gotNote = true; }
        else if (m.isNoteOff()) { evs.add ({ meta.samplePosition, m.getNoteNumber(), 0.0f, false }); midiHeld = juce::jmax (0, midiHeld - 1); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { midiHeld = 0; synth.allOff(); }
    }
    const bool wasMidi = midiActive.load();
    if (gotNote) samplesSinceMidi = 0; else samplesSinceMidi += n;
    const bool midiNow = midiHeld > 0 || samplesSinceMidi < (juce::int64) (4.0 / ppqPerSample);
    midiActive = midiNow;
    if (midiNow && ! wasMidi) synth.allOff();

    // ---- eventos del patrón dentro de este bloque ----
    if (running && ! midiNow)
    {
        const auto& ev = evBufs[(size_t) evIdx.load()].ev;
        const double p0 = ppq, p1 = ppq + n * ppqPerSample;
        for (auto& e : ev)
        {
            double t = e.beat + mbass::kLoopBeats * std::ceil ((p0 - e.beat) / mbass::kLoopBeats - 1.0e-12);
            for (; t < p1; t += mbass::kLoopBeats)
                if (t >= p0) evs.add ({ juce::jlimit (0, n - 1, (int) ((t - p0) / ppqPerSample)), e.note, e.vel, e.on });
        }
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.at != b.at ? a.at < b.at : (! a.on && b.on); });

    // ---- sidechain al kick (en cada negra) ----
    const float depth = running ? mbass::duckDepth (pKick->load()) : 0.0f;
    const double spb = 60.0 / bpm;
    for (int i = 0; i < n; ++i) gainBuf[(size_t) i] = running ? mbass::duckGain (ppq + i * ppqPerSample, spb, depth) : 1.0f;

    // ---- render por tramos entre eventos ----
    auto* out = buffer.getWritePointer (0);
    int pos = 0, ei = 0;
    while (pos < n)
    {
        while (ei < evs.size() && evs.getReference (ei).at <= pos)
        {
            const auto& e = evs.getReference (ei++);
            handleEvent ({ 0.0, e.note, e.vel, e.on });
        }
        const int next = ei < evs.size() ? juce::jmin (n, evs.getReference (ei).at) : n;
        synth.render (out + pos, next - pos, gainBuf.data() + pos);
        pos = next;
    }
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch) buffer.copyFrom (ch, 0, buffer, 0, 0, n);   // bajo mono

    const double end = ppq + n * ppqPerSample;
    lastEnd = running ? end : -1.0;
    if (preview && ! host) previewPpq = end;
    posBeats = running ? std::fmod (juce::jmax (0.0, ppq), mbass::kLoopBeats) : 0.0;
    midi.clear();
}

// ------------------------------------------------------------------ interfaz
void MBProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float norm = p->convertTo0to1 (value);
        if (std::abs (norm - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost (norm);
    }
}

void MBProcessor::gesture (const juce::String& id, bool begin)
{
    if (auto* p = apvts.getParameter (id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

juce::var MBProcessor::stateVar() const
{
    auto* o = new juce::DynamicObject();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            o->setProperty (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
    return juce::var (o);
}

juce::String MBProcessor::clipName() const
{
    return "MIND BASS " + mbass::keyName ((int) pKey->load(), (int) pScale->load()) + " " + juce::String (juce::roundToInt (currentBpm()));
}

juce::File MBProcessor::exportClip (bool audio)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("MIND BASS").getChildFile ("Drag");
    dir.createDirectory();
    // nombre único: Ableton reutiliza el clip si el archivo se llama igual
    const auto stamp = juce::Time::getCurrentTime().formatted ("%H%M%S");
    auto f = dir.getNonexistentChildFile (clipName() + " " + stamp, audio ? ".wav" : ".mid", false);
    const int key = (int) pKey->load();
    if (! audio)
    {
        auto mf = mbass::makeMidi (pattern, key, pSwing->load(), pGate->load());
        juce::FileOutputStream os (f);
        if (! os.openedOk() || ! mf.writeTo (os)) return {};
        return f;
    }
    juce::AudioBuffer<float> buf;
    const double rate = sr > 8000.0 ? sr : 48000.0;
    mbass::renderAudio (buf, rate, pattern, key, pSwing->load(), pGate->load(), currentBpm(), sound(), pKick->load());
    juce::WavAudioFormat wav;
    auto* os = new juce::FileOutputStream (f);
    if (! os->openedOk()) { delete os; return {}; }
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, rate, 2, 24, {}, 0));
    if (w == nullptr) { delete os; return {}; }
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    w.reset();
    return f;
}

void MBProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("pattern", juce::JSON::toString (patternVar(), true), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void MBProcessor::setStateInformation (const void* d, int size)
{
    auto xml = getXmlFromBinary (d, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    const auto pj = st.getProperty ("pattern", "").toString();
    st.removeProperty ("pattern", nullptr);
    apvts.replaceState (st);
    if (pj.isNotEmpty()) setPatternVar (juce::JSON::parse (pj));
    else rebuild();
    if (onPatternChanged) onPatternChanged();
}

juce::AudioProcessorEditor* MBProcessor::createEditor() { return new MBEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MBProcessor(); }
