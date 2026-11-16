# MIND BASS · Dj Nané (MIND SERIES)

Instrumento de bajos: genera líneas en estilos de sello, suena solo con un sub propio y se arrastra el MIDI o el audio a Ableton.

v0.4.0: piano roll estilo Ableton, BARS 1-2-4-8, GENERATE libre por estilo, notas a mano sin bloqueo.
v0.3.0: estilos SOLID, RIO, BAILESITO y SINTOS (análisis v2) y selector de octava.
v0.2.0: estilo BCN SUB (sonido Mindshake 2023-2026), escalas MINOR / MAJOR / PHRYGIAN / DORIAN y 9 sonidos (SINE fija + NEW SOUND).

- `Source/BassEngine`: patrón de 4 compases, sinte sub mono (SINE/TRI/SAW, DRIVE sin subir volumen, MID, FILTER, DECAY), sidechain, MIDI y WAV.
- `Source/PluginProcessor`, `Source/PluginEditor`: reloj de Ableton o PLAY propio, MIDI de entrada con prioridad, estado del proyecto.
- `ui/src.html`: interfaz y generador BCN SUB (`python3 tools/build_ui.py` monta `ui/index.html`).
- Pruebas: `cmake -B build -DMB_TESTS=ON && cmake --build build`, luego `MbEngineTest`, `MbProcessorTest` y `node tests/ui.test.mjs`.
