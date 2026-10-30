#!/usr/bin/env node
// Suckboxx UI:n savutesti + kuvakaappaukset (WP-5). Vaatii käynnissä olevan
// mock-palvelimen (python3 tools/mock_server.py) ja Playwrightin (Node).
//
//   node tools/ui_screenshots.js --url http://localhost:8000 --out /tmp/ui_shots
//
// Ajaa simulaattorin käytösmatriisin tilojen läpi (§8), tarkistaa bannerin
// tilan ja tekstin, ettei konsolissa ole virheitä eikä sivu vieri vaakasuunnassa
// 360 px leveydellä, ja tallentaa kuvat. Exit 1 jos jokin tarkistus epäonnistuu.

const path = require('path');
const fs = require('fs');
const { chromium } = require('playwright');

function arg(name, def) {
  const i = process.argv.indexOf(name);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : def;
}
const URL = arg('--url', 'http://localhost:8000');
const OUT = arg('--out', path.join(process.cwd(), 'ui_shots'));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
function check(ok, msg) {
  console.log((ok ? '  ok   ' : '  FAIL ') + msg);
  if (!ok) failures++;
}

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2,
    isMobile: true, hasTouch: true, locale: 'fi-FI' });
  const page = await ctx.newPage();
  const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e));
  page.on('dialog', (d) => d.accept());

  const post = (p, data) => page.evaluate(([p2, d]) => fetch(p2, { method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: d }).then((r) => r.json()), [p, data]);
  const banner = () => page.evaluate(() => {
    const b = document.getElementById('banner');
    return { state: b.getAttribute('data-state'), cls: b.className,
      head: document.getElementById('head').textContent, advice: document.getElementById('advice').textContent };
  });
  async function waitState(state, timeoutMs) {
    const t0 = Date.now();
    let b = null;
    while (Date.now() - t0 < timeoutMs) {
      b = await banner();
      if (b.state === state) return b;
      await page.waitForTimeout(200);
    }
    return b;
  }
  async function expectState(label, sim, state, cls, timeoutMs, extra) {
    if (sim) {
      const r = await post('/api/sim', sim);
      check(r.ok === true, `POST /api/sim ${sim} -> ok`);
    }
    const b = await waitState(state, timeoutMs || 12000);
    check(b.state === state, `${label}: tila ${state} (saatiin ${b.state}: "${b.head}" / "${b.advice}")`);
    check(b.cls.indexOf('st-' + cls) >= 0, `${label}: bannerin luokka st-${cls} (${b.cls})`);
    if (extra) {
      // Tila vaihtuu heti kun ero ylittää rajan; avg_cycles-keskiarvo (8 sykliä
      // ≈ 1,1 s @ 850 rpm) asettuu lopulliseen lukuun vasta hetken päästä.
      await page.waitForTimeout(1800);
      await extra(await banner());
    }
    return b;
  }
  const shot = (name, full) => page.screenshot({ path: path.join(OUT, name), fullPage: !!full });

  await page.goto(URL);
  await page.waitForFunction(() => document.getElementById('banner').getAttribute('data-state'), null, { timeout: 10000 });
  check(await page.isVisible('#simbar'), 'SIMULAATTORI-palkki näkyy kun src=sim');
  await post('/api/config/reset', '');

  // Rivi 1: moottori seis -> ei signaalia; nollaus onnistuu vain nyt.
  await expectState('moottori seis', 'engine_on=0&err1=0&err2=0&err3=0&fault1=0&fault2=0&fault3=0&rpm=850&drift=0',
    'no_signal', 'idle', 8000);
  await page.waitForTimeout(1500);   // EMA-taso asettuu ilmanpaineeseen
  const z = await post('/api/cal/zero', '');
  check(z.ok === true, `nollaus moottori seis: ${z.msg}`);
  await shot('main_no_signal.png');

  // Rivi 3: käynnistys -> hetken epävakaa -> synkronoitu.
  await post('/api/sim', 'engine_on=1');
  const u = await waitState('unstable', 4000);
  check(u.state === 'unstable', `käynnistyksen jälkeen epävakaa (${u.advice})`);
  await expectState('synkronoitu', null, 'synced', 'ok', 12000, async (b) => {
    check(b.head === 'Synkronoitu', `otsikko "Synkronoitu" (${b.head})`);
    check(!(await page.isVisible('.badge >> text=KALIBROIMATON')), 'KALIBROIMATON-merkki pois nollauksen jälkeen');
    check(await page.isVisible('.badge >> text=VAHVISTUS KALIBROIMATTA'), 'VAHVISTUS KALIBROIMATTA -merkki näkyy');
    const rpm = await page.textContent('#rpm');
    check(Math.abs(Number(rpm) - 850) < 30, `rpm ~850 (${rpm})`);
    check(await page.isVisible('.cyl.m .mtag'), 'MASTER-merkintä näkyy');
  });
  await page.waitForTimeout(600);
  await shot('main_synced.png');

  // Rivi 4a: SYL 2 +2,5 kPa (vähemmän alipainetta) -> lähellä (spread 2,5 ≤ 3,0).
  await expectState('SYL 2 +2,5', 'err2=2.5', 'near', 'near', 8000, async (b) => {
    check(/SYL 2: sulje läppää – imee 2,5 kPa vähemmän kuin SYL 1/.test(b.advice), `säätöohje (${b.advice})`);
  });
  // Rivi 4b: SYL 2 +4 -> ei synkronissa.
  await expectState('SYL 2 +4', 'err2=4', 'out_of_sync', 'bad', 8000, async () => {
    const card = await page.evaluate(() => {
      const c = document.querySelectorAll('.cyl')[1];
      return { dv: c.querySelector('.dv').textContent, act: c.querySelector('.act').textContent,
        bar: c.querySelector('.bar').style.display, clip: c.querySelector('.clip.dn').className };
    });
    check(/^−4,0/.test(card.dv), `SYL 2 d = −4,0 kPa (${card.dv})`);
    check(card.act === 'SULJE', `SYL 2 säätösuunta SULJE (${card.act})`);
    check(card.bar === 'block', 'SYL 2 palkki näkyy');
  });
  await page.waitForTimeout(600);
  await shot('main_out_of_sync.png');

  // Rivi 12: masterin (SYL 1) letku irti -> master-vika, kanavan vikapeite.
  await expectState('SYL 1 letku irti', 'err2=0&fault1=1', 'fault_master', 'fault', 8000, async (b) => {
    check(/Master \(SYL 1\)/.test(b.head), `otsikko kertoo master-viasta (${b.head})`);
    const fov = await page.evaluate(() => document.querySelectorAll('.cyl')[0].querySelector('.fov').textContent);
    check(fov === 'LETKU IRTI?', `SYL 1 vikapeite (${fov})`);
    check(await page.isVisible('#mhint.hot'), 'master-valitsin korostettu');
  });
  await page.waitForTimeout(400);
  await shot('main_fault_master.png');

  // Rivi 6: muu kuin master -> letkuvika.
  await expectState('SYL 3 letku irti', 'fault1=0&fault3=1', 'fault_hose', 'fault', 10000);
  await post('/api/sim', 'fault3=0');

  // Rivi 9: 4000 rpm -> arvio ja palkit piiloon, rpm näkyy.
  await expectState('4000 rpm', 'rpm=4000', 'rpm_too_high', 'idle', 10000, async () => {
    const vis = await page.evaluate(() => Array.from(document.querySelectorAll('.bar')).some((b) => b.style.display === 'block'));
    check(!vis, 'palkit piilossa (bars=false)');
    const rpm = await page.textContent('#rpm');
    check(Math.abs(Number(rpm) - 4000) < 100, `rpm ~4000 (${rpm})`);
  });
  await shot('main_rpm_too_high.png');

  // Rivi 5: ajautuminen -> merkki.
  await post('/api/sim', 'rpm=850&drift=80');
  await page.waitForTimeout(3500);
  check(await page.isVisible('.badge >> text=KIERROKSET MUUTTUVAT'), 'KIERROKSET MUUTTUVAT -merkki ajautuessa');
  await post('/api/sim', 'rpm=850&drift=0');

  // Moottori seis taas -> no_signal.
  await expectState('moottori seis 2', 'engine_on=0', 'no_signal', 'idle', 8000);
  await post('/api/sim', 'engine_on=1');

  // 360 px: ei vaakavieritystä.
  await page.setViewportSize({ width: 360, height: 780 });
  await page.waitForTimeout(300);
  const sw = await page.evaluate(() => [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
  check(sw[0] <= sw[1], `ei vaakavieritystä 360 px:llä (${sw[0]} / ${sw[1]})`);

  // Huoltosivu: lomake rakentuu /api/config:sta, virhe näkyy.
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto(URL + '/#huolto');
  await page.waitForSelector('#f_0', { timeout: 5000 });
  const nf = await page.evaluate(() => document.querySelectorAll('.frow').length);
  const cfg = await page.evaluate(() => fetch('/api/config').then((r) => r.json()));
  check(nf === cfg.fields.length, `asetuslomakkeessa kaikki kentät (${nf} / ${cfg.fields.length})`);
  const src = await page.evaluate(() => document.querySelector('select[id^=f_] option') !== null);
  check(src, 'select-kentät (master/source) rakennettu');
  const tg = cfg.fields.findIndex((f) => f.key === 'tol_green_kpa');
  await page.fill('#f_' + tg, '5');
  await page.click('#bSave');
  await page.waitForSelector('#cErr:not([hidden])', { timeout: 3000 });
  check(/tol_green_kpa/.test(await page.textContent('#cErr')), 'ristiintarkistuksen virhe näytetään');
  await page.click('#bUndo');
  await page.fill('#f_' + tg, '1,2');
  await page.click('#bSave');
  await page.waitForFunction(() => /Tallennettu/.test(document.getElementById('cOk').textContent), null, { timeout: 3000 });
  const v = await page.evaluate(() => fetch('/api/config').then((r) => r.json()));
  check(v.fields[tg].value === 1.2, `tol_green_kpa tallentui pilkulla (${v.fields[tg].value})`);
  await post('/api/config/reset', '');
  await page.waitForTimeout(500);
  await shot('service.png', true);

  // Simulaattoripaneeli: esiasetus -> tila vaihtuu.
  await page.click('[data-p="3"]');   // "Letku irti SYL 2"
  const fh = await waitState('fault_hose', 10000);
  check(fh.state === 'fault_hose', `esiasetus "Letku irti SYL 2" -> fault_hose (${fh.state})`);
  await page.click('[data-p="0"]');

  check(errors.length === 0, `ei konsolivirheitä (${errors.length}) ${errors.slice(0, 3).join(' | ')}`);

  // Vaaka-asento.
  await page.goto(URL);
  await page.setViewportSize({ width: 844, height: 390 });
  await page.waitForTimeout(1500);
  await shot('main_landscape.png');

  // Kestävyys: puuttuvat/väärän tyyppiset kentät ja 4 kanavaa eivät saa kaataa sivua.
  await page.setViewportSize({ width: 390, height: 844 });
  const weird = [{}, { ch: null, tol: 'x', rpm: null, flags: 3 },
    { state: 'near', verdict: true, bars: true, master: 0, worst: 1, tol: [1.5, 3], ch: [{}, { d: 'a' }, { d: null }] },
    { state: 'out_of_sync', verdict: true, bars: true, master: 5, worst: 3, rpm: 1e9, rpm_sd: -1, tol: [0, 0],
      ch: [{ d: 0 }, { d: 99 }, { d: -99, fault: true }, { d: 1, pulse: false }],
      wave: { fs: 0, s: [[1], [null, 2, 3], 'x', [-200, 50, NaN]] }, cal: null, diag: 'x' },
    { state: 'fault_hose', fault_ch: 9, head: 7, advice: null, ch: [{ pulse: false, level: 0 }], src: null }];
  const body = 'retry: 60000\n\n' + weird.map((w) => 'event: snap\ndata: ' + JSON.stringify(w) + '\n\n').join('') +
    'event: snap\ndata: {broken json\n\n';
  const pageErrs = [];
  page.on('pageerror', (e) => pageErrs.push(String(e)));
  await page.route('**/events', (r) => r.fulfill({ status: 200, headers: { 'Content-Type': 'text/event-stream' }, body }));
  await page.goto(URL);
  await page.waitForTimeout(1200);
  await page.evaluate(() => { document.getElementById('diagbox').open = true; });
  await page.waitForTimeout(300);
  check(pageErrs.length === 0, `virheelliset snapshotit eivät kaada sivua (${pageErrs.join(' | ')})`);
  const nCards = await page.evaluate(() => document.querySelectorAll('.cyl').length);
  check(nCards === 1, `kanavakortit rakennetaan kanavamäärän mukaan (${nCards})`);
  await page.unroute('**/events');

  // Yhteyskatko: SSE estetty -> "Ei yhteyttä laitteeseen" ja harmaa data.
  await page.route('**/events', (r) => r.abort());
  await page.reload();
  await page.waitForSelector('#offline:not([hidden])', { timeout: 8000 });
  check(await page.isVisible('#offline'), 'yhteyskatko näytetään');
  await shot('main_offline.png');
  await page.unroute('**/events');
  await page.waitForSelector('#offline', { state: 'hidden', timeout: 15000 });
  check(!(await page.isVisible('#offline')), 'yhteys palautuu automaattisesti');

  await browser.close();
  console.log(failures ? `TULOS: ${failures} epäonnistui` : 'TULOS: OK');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
