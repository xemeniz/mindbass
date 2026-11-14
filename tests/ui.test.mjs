// Prueba de la interfaz de MIND BASS con un puente JUCE simulado: node tests/ui.test.mjs [ui/index.html] [/tmp/mb]
// Incluye 200 generaciones del estilo BCN SUB contra los números del análisis de Mindshake (2023-2026).
import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
import path from 'node:path';
const file = 'file://' + path.resolve(process.argv[2] || 'ui/index.html');
const out = process.argv[3] || '/tmp/mb';
let fails = 0; const ok = (c, m) => { if (!c) { fails++; console.log('  FALLO: ' + m); } else console.log('  ok: ' + m); };

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1038, height: 640 }, deviceScaleFactor: 2 });   // la parte web del plugin en un Mac Retina
const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
await page.addInitScript(() => {
  window.__sent = []; window.__ls = {};
  window.__JUCE__ = { backend: { emitEvent: (e, o) => window.__sent.push([e, JSON.parse(JSON.stringify(o || {}))]), addEventListener: (e, f) => { window.__ls[e] = f; } } };
  window.__fire = (e, v) => window.__ls[e] && window.__ls[e](v);
});
await page.goto(file);
await page.waitForTimeout(300);
const sent = () => page.evaluate(() => window.__sent);
const clear = () => page.evaluate(() => { window.__sent = []; });
const fire = (e, v) => page.evaluate(([e, v]) => window.__fire(e, v), [e, v]);
const lastPat = async () => { const p = (await sent()).filter(([e]) => e === 'mbPattern').pop(); return p && p[1]; };
const bars = st => [0, 1, 2, 3].map(b => JSON.stringify(st.slice(b * 16, b * 16 + 16)));

ok((await sent()).some(([e]) => e === 'mbReady'), 'avisa al plugin al arrancar (mbReady)');

// =============== 1) GENERADOR: 200 generaciones contra el análisis ===============
console.log('Generador BCN SUB');
const G = await page.evaluate(() => {
  const D = { density: .7, move: .4, kick: .45, scale: 0 };
  const res = { perBarA: [], perBarV: [], runsBad: 0, gapsBad: 0, tonic: 0, notes: 0, rowsBad: 0, bar13Same: 0, a1Diff: 0, a2Diff: 0, oneFree: 0,
    first0: 0, scaleBad: 0, phr1: 0, dor9: 0, phrNotes: 0, dorNotes: 0, varKeep: 0, varChange: 0, quartersK8: 0, oneK0: 0, maj3: 0, min3InMaj: 0, lenOk: 0, d0: [], d1: [], lockOk: 0 };
  const allowed = new Set(SCALES[0].rows);
  const on = (st, b) => st.slice(b * 16, b * 16 + 16).map(s => s[0]);
  for (let n = 0; n < 200; n++) {
    const sa = 1000 + n * 7919, sv = 77 + n * 104729;
    const st = MB.generate(sa, sv, D);
    for (const b of [0, 2]) {
      const o = on(st, b); res.perBarA.push(o.reduce((a, x) => a + x, 0));
      // grupos de 2-4 semicorcheas
      let run = 0; for (let i = 0; i <= 16; i++) { if (i < 16 && o[i]) run++; else { if (run && (run < 2 || run > 4)) res.runsBad++; run = 0; } }
      // 1-3 silencios por compás (contando el bucle: el del final y el del principio son el mismo)
      let gaps = 0; for (let i = 0; i < 16; i++) if (!o[i] && o[(i + 15) % 16]) gaps++;
      if (gaps < 1 || gaps > 3) res.gapsBad++;
      if (!o[0]) res.oneFree++;
    }
    for (const b of [1, 3]) res.perBarV.push(on(st, b).reduce((a, x) => a + x, 0));
    st.forEach(s => { if (s[0]) { res.notes++; if (s[1] === 0) res.tonic++; if (!allowed.has(s[1])) res.rowsBad++; if (s[3] === 1 || s[3] === 1.2) res.lenOk++; } });
    const bs = [0, 1, 2, 3].map(b => JSON.stringify(st.slice(b * 16, b * 16 + 16)));
    if (bs[0] === bs[2]) res.bar13Same++;
    if (bs[1] !== bs[0]) res.a1Diff++;
    if (bs[3] !== bs[0]) res.a2Diff++;
    const firstOn = st.findIndex(s => s[0]); if (firstOn >= 0 && st[firstOn][1] === 0) res.first0++;
    // VARIATION: misma semilla A, otra semilla V -> compases 1 y 3 iguales, 2 o 4 cambian
    const st2 = MB.generate(sa, sv + 1, D), bs2 = [0, 1, 2, 3].map(b => JSON.stringify(st2.slice(b * 16, b * 16 + 16)));
    if (bs2[0] === bs[0] && bs2[2] === bs[2]) res.varKeep++;
    if (bs2[1] !== bs[1] || bs2[3] !== bs[3]) res.varChange++;
    // KICK alto: negras libres en los 4 compases
    const k8 = MB.generate(sa, sv, { ...D, kick: .8 }); if ([0, 4, 8, 12].every(q => [0, 1, 2, 3].every(b => !k8[b * 16 + q][0]))) res.quartersK8++;
    const k0 = MB.generate(sa, sv, { ...D, kick: 0 }); if (k0[0][0] && k0[0][1] === 0) res.oneK0++;
    // mayor: 3ª mayor, nunca menor
    const mj = MB.generate(sa, sv, { ...D, scale: 1, move: 1 }); mj.forEach(s => { if (s[0] && s[1] === 4) res.maj3++; if (s[0] && s[1] === 3) res.min3InMaj++; });
    // frigia y dórica: todas las notas en sus filas y con su color (b2 en frigia, 6ª en dórica)
    for (const sc of [1, 2, 3]) { const g = MB.generate(sa, sv, { ...D, scale: sc, move: .6 }), rows = new Set(SCALES[sc].rows);
      g.forEach(s => { if (s[0]) { if (!rows.has(s[1])) res.scaleBad++; if (sc === 2) { res.phrNotes++; if (s[1] === 1) res.phr1++; } if (sc === 3) { res.dorNotes++; if (s[1] === 9) res.dor9++; } } }); }
    if (n < 40) {
      res.d0.push(on(MB.generate(sa, sv, { ...D, density: 0 }), 0).reduce((a, x) => a + x, 0));
      res.d1.push(on(MB.generate(sa, sv, { ...D, density: 1 }), 0).reduce((a, x) => a + x, 0));
    }
    // candados: nota +7 en el paso 5 y silencio en el 32 se respetan
    const cur = Array.from({ length: 64 }, () => [0, 0, .85, 1, 0]); cur[5] = [1, 7, .9, 1, 1]; cur[32] = [0, 0, .85, 1, 1];
    const lk = MB.generate(sa, sv, D, cur); if (lk[5][0] === 1 && lk[5][1] === 7 && lk[5][4] === 1 && lk[32][0] === 0 && lk[32][4] === 1) res.lockOk++;
  }
  return res;
});
const avg = a => a.reduce((x, y) => x + y, 0) / a.length;
ok(avg(G.perBarA) >= 10 && avg(G.perBarA) <= 12, 'notas por compás (DENSITY 70 %): ' + avg(G.perBarA).toFixed(2) + ' (análisis: 10-12 de 16)');
ok(avg(G.perBarV) >= 9.5 && avg(G.perBarV) <= 12.5, 'en las variaciones también: ' + avg(G.perBarV).toFixed(2));
ok(G.runsBad === 0, 'grupos de 2-4 semicorcheas en los 400 compases A (' + G.runsBad + ' mal)');
ok(G.gapsBad === 0, '1-3 silencios por compás (' + G.gapsBad + ' mal)');
const tonic = G.tonic / G.notes;
ok(tonic >= 0.45 && tonic <= 0.75, 'tónica = ' + (tonic * 100).toFixed(1) + ' % de las notas (análisis: 40-75 %, objetivo 60 %)');
ok(G.rowsBad === 0, 'todas las notas caen en las filas de la rejilla');
ok(G.lenOk === G.notes, 'largos 1 o 1,2 (última de cada grupo algo más larga)');
ok(G.first0 === 200, 'la primera nota siempre es la tónica');
ok(G.bar13Same === 200, 'frase A A\' A A\'\': compás 3 = compás 1 en las 200');
ok(G.a1Diff === 200 && G.a2Diff === 200, 'A\' y A\'\' siempre distintos de A');
ok(G.varKeep === 200, 'VARIATION no toca los compases 1 y 3');
ok(G.varChange >= 195, 'VARIATION cambia el 2 o el 4 (' + G.varChange + '/200)');
const free1 = G.oneFree / 400;
ok(free1 > 0.12 && free1 < 0.34, 'KICK 45 %: el 1 queda libre en el ' + (free1 * 100).toFixed(1) + ' % (kick·0,5 = 22,5 %)');
ok(G.quartersK8 === 200, 'KICK > 50 %: las negras quedan libres siempre');
ok(G.oneK0 === 200, 'KICK 0: siempre toca en el 1');
ok(G.maj3 > 0 && G.min3InMaj === 0, 'en mayor usa la 3ª mayor (' + G.maj3 + ') y nunca la menor');
ok(G.scaleBad === 0, 'MAJOR, PHRYGIAN y DORIAN: todas las notas en las filas de su escala');
ok(G.phr1 / G.phrNotes > 0.08, 'PHRYGIAN suena frigio: b2 en el ' + (G.phr1 / G.phrNotes * 100).toFixed(1) + ' % de las notas');
ok(G.dor9 / G.dorNotes > 0.02, 'DORIAN suena dórico: 6ª mayor en el ' + (G.dor9 / G.dorNotes * 100).toFixed(1) + ' % de las notas');
ok(G.d0.every(x => x === 4) && G.d1.every(x => x === 14), 'DENSITY 0 = 4 notas, 100 % = 14');
ok(G.lockOk === 200, 'los pasos bloqueados (nota y silencio) se respetan al generar');

// =============== 2) INTERFAZ ===============
console.log('Interfaz');
await fire('mbSettings', { theme: 'dark', drag: 'midi', standalone: false });
const params = { key: 5, scale: 0, swing: 0.57, gate: 0.7, density: 0.7, move: 0.4, kick: 0.45, shape: 0, drive: 0.3, mid: 0.15, filter: 0.4, decay: 0.9, sbpm: 128 };
await fire('mbState', { p: params, play: 0, pos: 0, bpm: 126, midiIn: false });
const steps0 = await page.evaluate(() => MB.generate(4242, 99, { density: .7, move: .4, kick: .45, minor: true }));
await fire('mbPattern', { steps: steps0, seedA: 4242, seedV: 99 });
ok((await page.textContent('#keyt')) === 'F', 'KEY F');
ok((await page.textContent('#scalet')) === 'MINOR', 'escala MINOR');
ok((await page.textContent('#bpmt')) === '126', 'BPM de Ableton');
ok((await page.textContent('#swt')) === '57%', 'SWING 57 %');
ok((await page.textContent('#stylet')) === 'BCN SUB', 'estilo BCN SUB en lista');
ok(!(await page.textContent('body')).includes('LDN ROLLER') && !(await page.textContent('body')).includes('from Ableton'), 'sin los textos de LDN ROLLER / IBZ BOUNCE ni "from Ableton"');
ok((await page.textContent('#pinfo')).startsWith('F MIN · ' + steps0.filter(s => s[0]).length + ' NOTES'), 'resumen del patrón (' + await page.textContent('#pinfo') + ')');
ok((await page.locator('#k_filter').locator('..').locator('.kv').textContent()) === '244 Hz', 'FILTER 40 % = 244 Hz');
ok((await page.textContent('.logo b')) === 'MIND BASS' && (await page.textContent('.logo small')).toUpperCase() === 'BY DJ NANÉ', 'logo MIND BASS / BY DJ NANÉ');
const gridPix = await page.evaluate(() => { const c = document.querySelector('#gcv'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; });
ok(gridPix > 20000, 'dibuja la rejilla y las notas');
const crisp = await page.evaluate(() => { const w = document.querySelector('#wrap'), c = document.querySelector('#gcv'); return { zoom: getComputedStyle(w).zoom, cw: c.width, ccw: c.clientWidth, dpr: devicePixelRatio, h: w.scrollHeight }; });
ok(crisp.zoom === '1' && crisp.cw === Math.round(crisp.ccw * crisp.dpr), 'nítido: sin escalar y rejilla a resolución Retina');
ok(crisp.h <= 640 - 38, 'cabe en la ventana sin cortar (' + crisp.h + ' px)');
const overflow = await page.evaluate(() => [...document.querySelectorAll('.panel')].filter(p => p.scrollWidth > p.clientWidth + 1).length);
ok(overflow === 0, 'ningún panel se sale');
await page.screenshot({ path: out + '-dark.png' });

// tamaños iguales por fila
const sizes = async sel => page.$$eval(sel, els => els.map(e => Math.round(e.getBoundingClientRect().width) + 'x' + Math.round(e.getBoundingClientRect().height)));
for (const sel of ['.genp .col button', '.sndp .col button', 'header .val', 'header .ico', '.kn']) { const s = await sizes(sel); ok(new Set(s).size === 1, 'mismo tamaño en ' + sel + ' (' + s[0] + ')'); }
ok(JSON.stringify(await sizes('.genp .col button')) === JSON.stringify(await sizes('.sndp .col button')), 'columnas de GENERATE y SOUND iguales');
// SWING: la caja no se mueve al cambiar el valor
const swPos = async () => page.evaluate(() => { const b = document.querySelector('#swv').getBoundingClientRect(), l = document.querySelector('#swv small').getBoundingClientRect(); return [b.left, b.width, l.left].map(Math.round).join(); });
const sw0 = await swPos();
for (const v of [0.575, 0.6, 0.705, 0.5]) { await fire('mbState', { p: { ...params, swing: v } }); ok(await swPos() === sw0, 'SWING ' + (v * 100) + ' %: la caja y su etiqueta no se mueven'); }
await fire('mbState', { p: params });

// GENERATE + Undo
await clear();
await page.click('#gen');
let pt = await lastPat();
ok(pt && pt.steps.length === 64 && (pt.seedA !== 4242 || pt.seedV !== 99), 'GENERATE manda un patrón nuevo con semillas nuevas');
ok(JSON.stringify(pt.steps) !== JSON.stringify(steps0), 'el patrón cambia');
await clear();
await page.keyboard.press('Meta+z');
pt = await lastPat();
ok(pt && JSON.stringify(pt.steps) === JSON.stringify(steps0) && pt.seedA === 4242, 'Cmd+Z vuelve al patrón anterior');

// VARIATION: solo compases 2 y 4
await clear();
await page.click('#var');
pt = await lastPat();
const b0 = bars(steps0), b1 = bars(pt.steps);
ok(b1[0] === b0[0] && b1[2] === b0[2], 'VARIATION deja los compases 1 y 3');
ok(b1[1] !== b0[1] || b1[3] !== b0[3], 'VARIATION cambia el 2 o el 4');
ok(pt.seedA === 4242, 'VARIATION mantiene la semilla A');
await page.keyboard.press('Meta+z');

// rejilla: poner, mover, quitar y bloquear
const box = await page.locator('#gcv').boundingBox();
const geo = { L: 52, T: 18, cw: (box.width - 52 - 4) / 64, rh: (box.height - 18 - 4) / 11 };
const cellXY = (i, r) => [box.x + geo.L + (i + 0.5) * geo.cw, box.y + geo.T + (r + 0.5) * geo.rh];
const cur = () => page.evaluate(() => PAT.steps.map(s => s.slice()));
let st = await cur();
let emptyI = st.findIndex((s, i) => !s[0] && !s[4] && i > 0);
await clear();
let [x, y] = cellXY(emptyI, 2);   // fila +7
await page.mouse.click(x, y);
st = await cur();
ok(st[emptyI][0] === 1 && st[emptyI][1] === 7 && st[emptyI][4] === 1, 'clic en hueco: pone una nota (+7) bloqueada');
ok(!!(await lastPat()), 'y se la manda al plugin');
// mover arrastrando a otra fila y paso
const dst = emptyI + 1 < 64 && !st[emptyI + 1][0] ? emptyI + 1 : emptyI;
await page.mouse.move(x, y); await page.mouse.down();
const [x2, y2] = cellXY(dst, 6);   // fila +1
await page.mouse.move(x2, y2, { steps: 4 }); await page.mouse.up();
st = await cur();
ok(st[dst][0] === 1 && st[dst][1] === 1 && st[dst][4] === 1, 'arrastrar mueve la nota (+1) y sigue bloqueada');
ok(dst === emptyI || st[emptyI][0] === 0, 'el sitio de origen queda vacío');
// quitar con clic
await page.mouse.click(x2, y2);
st = await cur();
ok(st[dst][0] === 0 && st[dst][4] === 1, 'clic en una nota la quita (silencio bloqueado)');
// shift+clic desbloquea
await page.keyboard.down('Shift'); await page.mouse.click(x2, y2); await page.keyboard.up('Shift');
st = await cur();
ok(st[dst][4] === 0, 'Shift+clic desbloquea el paso');
// el bloqueo sobrevive a GENERATE
const noteI = st.findIndex(s => s[0] && s[1] === 0);
[x, y] = cellXY(noteI, 7);
await page.keyboard.down('Shift'); await page.mouse.click(x, y); await page.keyboard.up('Shift');
const lockedBefore = (await cur())[noteI];
await page.click('#gen');
st = await cur();
ok(lockedBefore[4] === 1 && JSON.stringify(st[noteI]) === JSON.stringify(lockedBefore), 'una nota bloqueada sigue igual tras GENERATE');
const lockedRows = await page.evaluate(() => PAT.steps.filter(s => s[4]).length);
ok((await page.textContent('#pinfo')).includes(lockedRows + ' LOCKED'), 'el resumen cuenta los pasos bloqueados');

// knobs: DENSITY rehace el mismo patrón con más notas
await clear();
const before = (await cur()).filter(s => s[0]).length;
const kd = await page.locator('#k_density').boundingBox();
await page.mouse.move(kd.x + kd.width / 2, kd.y + kd.height / 2);
for (let i = 0; i < 4; i++) await page.mouse.wheel(0, -100);
let s = await sent();
const dset = s.filter(([e, o]) => e === 'mbSet' && o.id === 'density').pop();
ok(dset && Math.abs(dset[1].v - 0.9) < 1e-6, 'DENSITY con la rueda sube a 90 % (' + (dset && dset[1].v) + ')');
ok(s.some(([e, o]) => e === 'mbGesture' && o.id === 'density'), 'con su gesto (automatización)');
const after = (await cur()).filter(s => s[0]).length;
ok(after > before, 'más DENSITY = más notas (' + before + ' -> ' + after + ')');
ok(s.some(([e]) => e === 'mbPattern'), 'y manda el patrón nuevo');

// KEY y escala
await clear();
await page.click('#keyv');
ok(await page.isVisible('#keypop') && (await page.locator('#keypop button').count()) === 12, 'KEY abre la lista de 12 tonalidades');
const blues = await page.$$eval('#keypop button.blue', b => b.map(x => x.querySelector('span').textContent + ':' + (x.classList.contains('soft') ? 's' : 'f')));
ok(JSON.stringify(blues.sort()) === JSON.stringify(['A#:s', 'B:f', 'D:f', 'F#:s', 'F:f']), 'en azul las de Mindshake: B, F y D fuertes; A# y F# suaves (' + blues + ')');
ok((await page.textContent('#keypop button:nth-child(12) em')) === '5 OF 12', 'B: 5 de 12 temas');
await page.screenshot({ path: out + '-key.png' });
await page.click('#keypop button:nth-child(10)');   // A
s = await sent();
ok(s.some(([e, o]) => e === 'mbSet' && o.id === 'key' && o.v === 9), 'elegir A manda key=9');
ok((await page.textContent('#keyt')) === 'A' && await page.isHidden('#keypop'), 'la caja enseña A y se cierra');
await page.evaluate(() => { PAT.steps[40] = [1, 3, .8, 1, 0]; PAT.steps[41] = [1, -1, .8, 1, 0]; });
await page.click('#scalev');
ok(JSON.stringify(await page.$$eval('#keypop button span', b => b.map(x => x.textContent))) === JSON.stringify(['MINOR', 'MAJOR', 'PHRYGIAN', 'DORIAN']), 'SCALE: MINOR, MAJOR, PHRYGIAN y DORIAN');
await clear();
await page.click('#keypop button:nth-child(2)');
s = await sent(); pt = await lastPat();
ok(s.some(([e, o]) => e === 'mbSet' && o.id === 'scale' && o.v === 1), 'MAJOR manda scale=1');
ok(pt && pt.steps[40][1] === 4 && !pt.steps.some(x => x[0] && x[1] === 3), 'en mayor la 3ª pasa de +3 a +4');
await page.click('#scalev'); await page.click('#keypop button:nth-child(4)');
pt = await lastPat();
const dorRows = await page.evaluate(() => SCALES[3].rows);
ok((await page.textContent('#scalet')) === 'DORIAN' && pt.steps.every(x => !x[0] || dorRows.includes(x[1])), 'DORIAN: las notas pasan a sus filas');
const lbl = await page.evaluate(() => ROWS().join());
ok(lbl === dorRows.join(), 'la rejilla enseña las filas de DORIAN (' + lbl + ')');
await page.keyboard.press('Meta+z'); await page.keyboard.press('Meta+z');
ok((await page.textContent('#scalet')) === 'MINOR', 'Cmd+Z vuelve a MINOR');

// SWING
await clear();
const sw = await page.locator('#swv').boundingBox();
await page.mouse.move(sw.x + sw.width / 2, sw.y + sw.height / 2); await page.mouse.wheel(0, -100);
s = await sent();
ok(s.some(([e, o]) => e === 'mbSet' && o.id === 'swing' && Math.abs(o.v - 0.58) < 1e-6), 'SWING con la rueda: 58 %');
await page.mouse.dblclick(sw.x + sw.width / 2, sw.y + sw.height / 2);
ok((await page.textContent('#swt')) === '57%', 'doble clic vuelve al 57 %');

// SOUND
await clear();
await page.click('#soundv');
const snds = await page.$$eval('#keypop button span', b => b.map(x => x.textContent));
ok(snds[0] === 'SINE' && snds.length === 9 && snds.includes('FM SUB') && snds.includes('CUSTOM'), 'SOUND: SINE primero y 8 sonidos más (' + snds.join(', ') + ')');
await page.click('#keypop button:nth-child(4)');   // tras la línea: SAW
s = await sent();
ok(s.some(([e, o]) => e === 'mbSet' && o.id === 'shape' && o.v === 2) && (await page.textContent('#soundt')) === 'SAW', 'elegir SAW');
await page.click('#sine');
ok((await page.textContent('#soundt')) === 'SINE' && await page.locator('#sine.on').count() === 1, 'el botón SINE vuelve al seno');
await clear();
await page.click('#newsnd');
s = await sent(); pt = await lastPat();
ok(s.some(([e, o]) => e === 'mbSet' && o.id === 'shape' && o.v === 8) && (await page.textContent('#soundt')) === 'CUSTOM', 'NEW SOUND crea un sonido CUSTOM');
const c1 = pt && pt.custom;
ok(Array.isArray(c1) && c1.length === 10 && c1[4] >= .5 && c1[4] <= 3 && c1[5] <= 2.8 && c1[8] <= 12, 'el timbre nuevo va al plugin y está en rango (' + c1 + ')');
await page.click('#newsnd');
const c2 = (await lastPat()).custom;
ok(JSON.stringify(c2) !== JSON.stringify(c1), 'cada NEW SOUND es distinto');
await page.keyboard.press('Meta+z');
ok(JSON.stringify((await lastPat()).custom) === JSON.stringify(c1), 'Cmd+Z vuelve al sonido anterior');

// PLAY
await clear();
await page.click('#play');
ok((await sent()).some(([e]) => e === 'mbPlay'), 'PLAY manda mbPlay');
await fire('mbState', { play: 1, pos: 0.3, bpm: 126, midiIn: false });
ok((await page.textContent('#play span')) === 'STOP', 'sonando: el botón pasa a STOP');
await fire('mbState', { play: 2, pos: 0.5, bpm: 126, midiIn: true });
ok((await page.textContent('#play span')) === 'ABLETON', 'con Ableton en marcha enseña ABLETON');
ok(await page.isVisible('#midichip'), 'aviso MIDI IN cuando llegan notas');
await clear();
await page.keyboard.press('Space');
ok(!(await sent()).some(([e]) => e === 'mbPlay'), 'con Ableton en marcha el espacio no lanza el PLAY propio');
await fire('mbState', { play: 0, pos: 0, bpm: 126, midiIn: false });
await clear();
await page.keyboard.press('Space');
ok((await sent()).some(([e]) => e === 'mbPlay'), 'barra espaciadora = PLAY');

// Settings
await page.click('#gear');
ok(await page.isVisible('#modal'), 'Settings se abre');
ok((await page.textContent('#ver')) === '0.2.0', 'versión 0.2.0');
ok(!(await page.textContent('#modal')).includes('PLAY WITH ABLETON') && !(await page.textContent('#modal')).includes('HOW TO USE'), 'Settings sin PLAY WITH ABLETON ni el tutorial');
await clear();
await page.click('#dragas button:nth-child(2)');
s = await sent();
ok(s.some(([e, o]) => e === 'mbSettings' && o.drag === 'audio'), 'DRAG AS AUDIO');
await page.click('#theme button:nth-child(2)');
s = await sent();
ok(s.some(([e, o]) => e === 'mbSettings' && o.theme === 'light') && await page.evaluate(() => document.documentElement.classList.contains('light')), 'tema claro');
await page.screenshot({ path: out + '-settings.png' });
await page.click('#mclose');
await page.click('#help');
ok(await page.isVisible('#hmodal') && (await page.textContent('#hmodal')).includes('Choose KEY and SCALE'), 'el botón (i) abre HOW TO USE');
await page.screenshot({ path: out + '-help.png' });
await page.click('#hclose');
await page.screenshot({ path: out + '-light.png' });

// patrón desde el plugin (abrir proyecto)
const steps1 = Array.from({ length: 64 }, (_, i) => [i % 4 === 2 ? 1 : 0, 0, .8, 1, 0]);
await fire('mbPattern', { steps: steps1, seedA: 5, seedV: 6 });
ok((await page.textContent('#pinfo')).includes('16 NOTES'), 'al abrir un proyecto enseña su patrón');

ok(errors.length === 0, 'sin errores en la consola' + (errors.length ? ': ' + errors.join(' | ') : ''));
await browser.close();
console.log(fails ? fails + ' FALLOS' : 'TODO OK');
process.exit(fails ? 1 : 0);
