#!/usr/bin/env node
// Suckboxx UI:n savutesti + kuvakaappaukset (WP-5). Vaatii käynnissä olevan
// mock-palvelimen (python tools/mock_server.py) ja Playwrightin (Node).
//
//   NODE_PATH="$(npm root -g)" node tools/ui_screenshots.js --url http://localhost:8000 --out /tmp/ui_shots
//
// Ajaa simulaattorin käytösmatriisin tilojen läpi (§8), tarkistaa bannerin tilan ja tekstin,
// ettei konsolissa ole virheitä eikä sivu vieri vaakasuunnassa 360 px leveydellä, ja
// tallentaa kuvat. Exit 1 jos jokin tarkistus epäonnistuu, 2 jos testi kaatuu.
//
// Tärkeimmät invariantit (tarkistetaan jokaisessa tilassa, ks. checkInvariants):
//   - vihreää ei näy missään muualla kuin tilassa synced (D11, §8: väärä vihreä on pahempi
//     kuin ei tulosta): ei bannerissa, palkeissa, mittarin kaistassa eikä merkeissä
//   - AVAA/SULJE ja worst-kehys näkyvät vain, kun laitteen advice-teksti antaa ruuvineuvon
//     ("SYL n: avaa/sulje läppää"), ja silloin täsmälleen sille kanavalle ja suunnalle. UI lukee
//     neuvon snapshotin act-kentästä (WP-10), joten tämä tarkistaa samalla, että laite/mock
//     pitää act-kentän ja tekstin yhtäpitävinä.
// Lisäksi: kalibroimaton laite (tuore mock) ei näytä vihreää eikä AVAA/SULJE-tekstiä,
// kosketusalueet >= 44 px, asteikon tekstit eivät pyöristy, vikapeite tulee laitteen
// kanavaluokituksesta (ch[i].cond) eikä luvuista, "Palauta oletukset" varoittaa
// uudelleenkäynnistyksestä ja mockin drift-pysäytys ei hyppää takaisin vanhaan rpm:ään.
//
// Testi alkaa kalibroimattomasta tilasta. Jos laite on jo nollattu (testi ajettu uudelleen
// samaa mockia vasten), se unohdetaan lähteen vaihdolla (uudelleenkäynnistys, D11).

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
function section(title) { console.log('\n# ' + title); }

const SIM_DEFAULTS = 'engine_on=1&rpm=850&jitter=0.01&drift=0&base=-55&err1=0&err2=0&err3=0&depth=20&res=3' +
  '&xtalk=0&peak=0&noise=2&fault1=0&fault2=0&fault3=0';

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2,
    isMobile: true, hasTouch: true, locale: 'fi-FI' });
  const page = await ctx.newPage();
  // Konsolivirheet ja JS-poikkeukset erikseen: mockin uudelleenkäynnistys (503) ja tahallinen
  // yhteyskatko (abort) tuottavat odotettuja resurssivirheitä, mutta ei koskaan JS-poikkeuksia.
  const consoleErrors = [];
  const pageErrors = [];
  page.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(m.text()); });
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // confirm()-ikkunat: viestit talteen, oletuksena hyväksytään.
  const dialogs = [];
  let dialogAction = 'accept';
  page.on('dialog', (d) => { dialogs.push(d.message()); return dialogAction === 'accept' ? d.accept() : d.dismiss(); });

  const post = (p, data) => page.evaluate(([p2, d]) => fetch(p2, { method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: d }).then((r) => r.json()), [p, data]);
  const getJson = (p) => page.evaluate((p2) => fetch(p2).then((r) => r.json()), p);
  const shot = (name, full) => page.screenshot({ path: path.join(OUT, name), fullPage: !!full });
  // Pitkä sivu viipaleina (yksi kuva olisi liian korkea katsottavaksi).
  async function shotSlices(prefix) {
    const h = await page.evaluate(() => document.documentElement.scrollHeight);
    const vh = page.viewportSize().height;
    let n = 1;
    for (let y = 0; y < h; y += vh - 60, n++) {
      await page.evaluate((yy) => window.scrollTo(0, yy), y);
      await page.waitForTimeout(120);
      await page.screenshot({ path: path.join(OUT, `${prefix}_${n}.png`), scale: 'css' });
    }
    await page.evaluate(() => window.scrollTo(0, 0));
  }

  // ---- DOM:n tilannekuva yhdellä kertaa (ei välissä päivittyviä snapshoteja) ----
  const view = () => page.evaluate(() => {
    const rgb = (c) => (c.match(/[\d.]+/g) || []).slice(0, 3).join(',');
    const probe = document.createElement('i');
    probe.style.color = 'var(--ok)';
    document.body.appendChild(probe);
    const ok = rgb(getComputedStyle(probe).color);
    probe.remove();
    const isOk = (c) => rgb(c) === ok && !/,\s*0\)$/.test(c);
    // Mittausalueen näkyvät elementit, joiden täyttö-, reuna- tai tekstiväri on --ok:n sävy.
    const greens = [];
    document.querySelectorAll('#banner, #banner *, #rpmbox, #rpmbox *, #badges, #badges *, #cyls, #cyls *')
      .forEach((el) => {
        if (!el.getClientRects().length) return;
        const cs = getComputedStyle(el);
        if (cs.visibility === 'hidden') return;
        const hit = [];
        if (isOk(cs.backgroundColor)) hit.push('tausta');
        if (el.children.length === 0 && el.textContent.trim() && isOk(cs.color)) hit.push('teksti');
        for (const side of ['Top', 'Right', 'Bottom', 'Left']) {
          if (parseFloat(cs['border' + side + 'Width']) > 0 && cs['border' + side + 'Style'] !== 'none' &&
              isOk(cs['border' + side + 'Color'])) { hit.push('reuna'); break; }
        }
        if (hit.length) greens.push(`${el.tagName.toLowerCase()}.${String(el.className).trim().replace(/\s+/g, '.')}:${hit.join('+')}`);
      });
    const b = document.getElementById('banner');
    const cyls = Array.from(document.querySelectorAll('.cyl'));
    return {
      state: b.getAttribute('data-state'), cls: b.className,
      head: document.getElementById('head').textContent, advice: document.getElementById('advice').textContent,
      greens,
      bars: cyls.map((c) => { const bar = c.querySelector('.bar'); return { shown: bar.style.display === 'block', cls: bar.className }; }),
      // Vain näkyvät merkinnät: tyhjä tai piilotettu .act ei ole neuvo.
      acts: cyls.map((c, i) => {
        const a = c.querySelector('.act');
        return { i, text: getComputedStyle(a).visibility === 'hidden' ? '' : a.textContent };
      }).filter((x) => x.text),
      worst: cyls.map((c, i) => (c.classList.contains('w') ? i : -1)).filter((i) => i >= 0),
      meterOk: document.querySelectorAll('.meter.ok').length,
      meterOn: document.querySelectorAll('.meter.on').length,
      scale: cyls.map((c) => [c.querySelector('.sc.t').textContent, c.querySelector('.sc.b').textContent]),
      fov: cyls.map((c) => { const f = c.querySelector('.fov'); return f.classList.contains('on') ? f.textContent : ''; }),
      cylsText: document.getElementById('cyls').innerText,   // renderöity teksti
    };
  });

  // Tilasta riippumattomat säännöt. Palauttaa löydetyt rikkomukset.
  function invariantProblems(v) {
    const p = [];
    const synced = v.state === 'synced';
    if (synced !== v.cls.includes('st-ok')) p.push(`banneri ${v.cls} tilassa ${v.state}`);
    if (!synced && v.greens.length) p.push('vihreää: ' + v.greens.join(', '));
    if (!synced && v.meterOk) p.push('mittarin kaista vihreä');
    if (!synced && v.bars.some((x) => x.shown && /\bok\b/.test(x.cls))) p.push('vihreä palkki');
    const m = /^SYL (\d): (avaa|sulje) läppää/.exec(v.advice);
    if (m) {
      const ch = Number(m[1]) - 1;
      if (v.acts.length !== 1 || v.acts[0].i !== ch || v.acts[0].text.toLowerCase() !== m[2]) {
        p.push(`neuvo "${v.advice}" mutta AVAA/SULJE: ${JSON.stringify(v.acts)}`);
      }
      if (v.worst.length !== 1 || v.worst[0] !== ch) p.push(`neuvo kanavalle ${ch} mutta worst-kehys: ${JSON.stringify(v.worst)}`);
    } else {
      if (v.acts.length) p.push(`ei ruuvineuvoa ("${v.advice}") mutta AVAA/SULJE: ${JSON.stringify(v.acts)}`);
      if (v.worst.length) p.push(`ei ruuvineuvoa ("${v.advice}") mutta worst-kehys: ${JSON.stringify(v.worst)}`);
      if (/AVAA|SULJE/.test(v.cylsText)) p.push('AVAA/SULJE-teksti sylinterikorteissa ilman neuvoa');
    }
    return p;
  }
  function checkInvariants(label, v) {
    const p = invariantProblems(v);
    check(p.length === 0, `${label}: invariantit (vihreä vain synced, AVAA/SULJE vain neuvolla) ${p.join('; ')}`);
  }

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
  // Tila on vakaa, kun se on pysynyt samana holdMs. Ohittaa vanhan, ennen komentoja otetun snapshotin
  // (esim. config/reset nollaa tilakoneen, jolloin vanha tila ehtii näkyä vielä hetken).
  async function waitStable(state, timeoutMs, holdMs) {
    const t0 = Date.now();
    let since = 0;
    let b = null;
    while (Date.now() - t0 < timeoutMs) {
      b = await banner();
      if (b.state === state) {
        since = since || Date.now();
        if (Date.now() - since >= holdMs) return b;
      } else {
        since = 0;
      }
      await page.waitForTimeout(150);
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
    checkInvariants(label, await view());
    return b;
  }

  // Kosketusalueet: kaikkien näkyvien napautettavien elementtien koko >= 44 px.
  const smallTargets = () => page.evaluate(() => {
    const bad = [];
    document.querySelectorAll('button, a[href], input, select, summary').forEach((el) => {
      const cs = getComputedStyle(el);
      const r = el.getBoundingClientRect();
      if ((!r.width && !r.height) || cs.display === 'none' || cs.visibility === 'hidden') return;
      if (r.height < 43.5 || r.width < 43.5) {
        bad.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''}.${String(el.className).split(' ')[0]} ${Math.round(r.width)}x${Math.round(r.height)}`);
      }
    });
    return bad;
  });

  // ---- synteettiset snapshotit (deterministiset; SSE korvataan kiinteällä vastauksella) ----
  const chan = (d, o) => Object.assign({ d, mean: -55 - d, level: -55, min: -72, p2p: 25, rpm: 851, mv: 1323,
    pulse: true, fault: false, sat: false, cond: 'ok' }, o || {});
  const snap = (o) => Object.assign({ v: 1, t: 123456, src: 'sim', state: 'synced', fault_ch: -1, bars: true,
    verdict: true, head: 'Synkronoitu', advice: 'Valmis: ero 0,4 kPa (raja 1,5 kPa)', act: null,
    flags: { sat: false, uncal_zero: false, uncal_gain: false, drift: false }, master: 0, tol: [1.5, 3],
    rpm: 851.2, rpm_sd: 9.1, rpm_cv: 0.011, spread: 0.4, worst: 1, ch: [chan(0), chan(0.3), chan(-0.1)],
    wave: { fs: 62.5, s: waveRows() }, cal: { busy: false, zero: true, gain: false, msg: '' },
    diag: { fs: 1000, gaps: 0, heap: 180000, frozen: false } }, o);
  function waveRows() {
    const rows = [];
    for (let i = 0; i < 188; i++) {
      const ph = i / 62.5 * 2 * Math.PI * 6.8;
      rows.push([-55 - 18 * Math.max(0, Math.sin(ph)) ** 2, -55 - 18 * Math.max(0, Math.sin(ph + 2.09)) ** 2,
        -55 - 18 * Math.max(0, Math.sin(ph + 4.19)) ** 2].map((v) => Math.round(v * 100) / 100));
    }
    return rows;
  }
  // Lataa sivu niin, että SSE antaa vain annetut snapshotit; palauttaa fn():n tuloksen.
  async function synth(snaps, fn) {
    const body = 'retry: 60000\n\n' + snaps.map((s) => 'event: snap\ndata: ' + JSON.stringify(s) + '\n\n').join('');
    await page.route('**/events', (r) => r.fulfill({ status: 200, headers: { 'Content-Type': 'text/event-stream' }, body }));
    try {
      await page.goto(URL);
      const last = snaps[snaps.length - 1];
      await page.waitForFunction((st) => document.getElementById('banner').getAttribute('data-state') === st,
        String(last.state), { timeout: 5000 });
      await page.waitForTimeout(250);   // piirto ehtii valmiiksi
      return await fn();
    } finally {
      await page.unroute('**/events');
    }
  }
  async function synthView(label, s, expect, shotName) {
    const v = await synth([s], async () => {
      const r = await view();
      if (shotName) await shot(shotName);
      return r;
    });
    const p = expect(v) || [];
    check(p.length === 0, `${label}: ${p.join('; ') || 'ok'}`);
    return v;
  }

  // =======================================================================
  section('Käynnistys ja kalibroimaton laite (D11): ei vihreää, ei ruuvineuvoa');
  await page.goto(URL);
  await page.waitForFunction(() => document.getElementById('banner').getAttribute('data-state'), null, { timeout: 10000 });
  check(await page.isVisible('#simbar'), 'SIMULAATTORI-palkki näkyy kun src=sim');
  await post('/api/config/reset', '');
  await post('/api/sim', SIM_DEFAULTS);

  // Tuore laite on kalibroimaton. Jos KALIBROIMATON-merkki puuttuu, laite on jo nollattu
  // (testi ajettu uudelleen): lähteen vaihto käynnistää mockin uudelleen ja nollaus unohtuu.
  if (!(await page.isVisible('.badge >> text=KALIBROIMATON'))) {
    console.log('  (laite on jo nollattu: unohdetaan nollaus lähteen vaihdolla)');
    await post('/api/config', 'source=1');
    await page.waitForTimeout(2200);
    await post('/api/config', 'source=0');
    await page.waitForTimeout(2500);
    await post('/api/sim', SIM_DEFAULTS);
    consoleErrors.length = 0;   // uudelleenkäynnistyksen 503-vastaukset ovat odotettuja
  }
  const un = await waitStable('near', 40000, 2000);
  check(un.state === 'near' && /^Lähellä – kalibroimaton$/.test(un.head), `kalibroimaton: tila near ("${un.head}")`);
  const uv = await view();
  check(/^Nollaus puuttuu/.test(uv.advice), `kalibroimaton: neuvo on pelkkä nollausohje ("${uv.advice}")`);
  check(uv.cls.includes('st-near'), `kalibroimaton: banneri keltainen (${uv.cls})`);
  check(await page.isVisible('.badge >> text=KALIBROIMATON'), 'kalibroimaton: KALIBROIMATON-merkki näkyy');
  const shownBars = uv.bars.filter((b) => b.shown);
  check(shownBars.length === 2 && shownBars.every((b) => b.cls === 'bar neu'),
    `kalibroimaton: palkit neutraaleja, ei vihreää palkkia (${shownBars.map((b) => b.cls).join(' | ')})`);
  check(uv.greens.length === 0, `kalibroimaton: mittausalueella ei yhtään vihreää elementtiä (${uv.greens.join(', ')})`);
  check(uv.meterOk === 0 && uv.meterOn === 3, `kalibroimaton: toleranssikaistat näkyvät mutta eivät ole vihreitä (on ${uv.meterOn}, ok ${uv.meterOk})`);
  check(uv.acts.length === 0 && !/AVAA|SULJE/.test(uv.cylsText), `kalibroimaton: ei AVAA/SULJE-tekstiä (${JSON.stringify(uv.acts)})`);
  check(uv.worst.length === 0, `kalibroimaton: ei worst-kehystä (${JSON.stringify(uv.worst)})`);
  checkInvariants('kalibroimaton', uv);
  await shot('main_uncal_near.png');
  const sm = await smallTargets();
  check(sm.length === 0, `kosketusalueet >= 44 px mittausnäkymässä (${sm.join(', ')})`);

  // =======================================================================
  section('Käytösmatriisin rivit (§8)');
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
  await shot('main_unstable.png');
  await expectState('synkronoitu', null, 'synced', 'ok', 12000, async (b) => {
    check(b.head === 'Synkronoitu', `otsikko "Synkronoitu" (${b.head})`);
    check(!(await page.isVisible('.badge >> text=KALIBROIMATON')), 'KALIBROIMATON-merkki pois nollauksen jälkeen');
    check(await page.isVisible('.badge >> text=VAHVISTUS KALIBROIMATTA'), 'VAHVISTUS KALIBROIMATTA -merkki näkyy');
    const rpm = await page.textContent('#rpm');
    check(Math.abs(Number(rpm) - 850) < 30, `rpm ~850 (${rpm})`);
    check(await page.isVisible('.cyl.m .mtag'), 'MASTER-merkintä näkyy');
    // Vihreä näkyy, kun sen kuuluu: muuten negatiiviset tarkistukset läpäisisi UI, joka ei koskaan ole vihreä.
    const sv = await view();
    const sb = sv.bars.filter((x) => x.shown);
    check(sb.length === 2 && sb.every((x) => x.cls === 'bar ok'), `synkronoitu: palkit vihreitä (${sb.map((x) => x.cls).join(' | ')})`);
    check(sv.meterOk === 3 && sv.greens.length > 0, `synkronoitu: kaista ja banneri vihreitä (${sv.meterOk} mittaria)`);
    check(sv.acts.length === 0 && sv.worst.length === 0, 'synkronoitu: ei AVAA/SULJE eikä worst-kehystä');
  });
  await page.waitForTimeout(600);
  await shot('main_synced.png');

  // Rivi 4a: SYL 2 +2,5 kPa (vähemmän alipainetta) -> lähellä (spread 2,5 ≤ 3,0).
  await expectState('SYL 2 +2,5', 'err2=2.5', 'near', 'near', 8000, async (b) => {
    check(/SYL 2: sulje läppää – imee 2,5 kPa vähemmän kuin SYL 1/.test(b.advice), `säätöohje (${b.advice})`);
    const nv = await view();
    check(nv.bars[1].cls === 'bar near' && nv.bars[2].cls === 'bar neu',
      `lähellä: pielessä oleva palkki keltainen, tolerassa oleva neutraali (${nv.bars.map((x) => x.cls).join(' | ')})`);
    check(nv.acts.length === 1 && nv.acts[0].i === 1 && nv.acts[0].text === 'SULJE' && nv.worst[0] === 1,
      `lähellä: SULJE ja worst-kehys SYL 2:lla (${JSON.stringify(nv.acts)} / ${JSON.stringify(nv.worst)})`);
  });
  await page.waitForTimeout(400);
  await shot('main_near.png');

  // Rivi 4b: SYL 2 +4 -> ei synkronissa.
  await expectState('SYL 2 +4', 'err2=4', 'out_of_sync', 'bad', 8000, async () => {
    const card = await page.evaluate(() => {
      const c = document.querySelectorAll('.cyl')[1];
      return { dv: c.querySelector('.dv').textContent, act: c.querySelector('.act').textContent,
        bar: c.querySelector('.bar').style.display, barCls: c.querySelector('.bar').className,
        clip: c.querySelector('.clip.dn').className };
    });
    check(/^−4,0/.test(card.dv), `SYL 2 d = −4,0 kPa (${card.dv})`);
    check(card.act === 'SULJE', `SYL 2 säätösuunta SULJE (${card.act})`);
    check(card.bar === 'block' && card.barCls === 'bar bad', `SYL 2 palkki näkyy punaisena (${card.barCls})`);
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
  await expectState('SYL 3 letku irti', 'fault1=0&fault3=1', 'fault_hose', 'fault', 10000, async () => {
    const fov = await page.evaluate(() => Array.from(document.querySelectorAll('.cyl .fov'))
      .map((f) => (f.classList.contains('on') ? f.textContent : '')));
    check(fov[2] === 'LETKU IRTI?' && !fov[0] && !fov[1], `vikapeite vain SYL 3:lla (${JSON.stringify(fov)})`);
  });
  await shot('main_fault_hose.png');
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
  await shot('main_drift.png');

  // Mockin drift-pysäytys (kuten engine_sim.h configure): pelkkä drift=0 jää nykyiseen rpm:ään,
  // GET /api/sim näyttää sen, eikä seuraava POST hyppää takaisin 850:een.
  await post('/api/sim', 'drift=0');
  const simAfter = await getJson('/api/sim');
  check(simAfter.rpm > 1000 && simAfter.rpm < 1300, `drift=0: /api/sim kertoo todellisen rpm:n (${simAfter.rpm}), ei vanhaa 850`);
  await post('/api/sim', 'res=3');   // mikä tahansa myöhempi POST ilman rpm-avainta
  await page.waitForTimeout(2500);
  const rpmAfter = Number(await page.textContent('#rpm'));
  check(Math.abs(rpmAfter - simAfter.rpm) < 80, `drift=0 + seuraava POST: rpm pysyy (${rpmAfter} vs ${Math.round(simAfter.rpm)}), ei hyppyä 850:een`);
  await post('/api/sim', 'rpm=850&drift=0');

  // Rivi 10: saturaatio -> merkki varoitusvärillä (ei koristevärillä).
  await post('/api/sim', 'base=-30&peak=40');
  await page.waitForSelector('.badge >> text=SATUROITUU', { timeout: 8000 });
  const bcol = await page.evaluate(() => {
    const probe = document.createElement('i');
    document.body.appendChild(probe);
    const col = (v) => { probe.style.color = 'var(' + v + ')'; return getComputedStyle(probe).color; };
    const out = { nrx: col('--nrx'), cy: col('--cy') };
    probe.remove();
    const b = Array.from(document.querySelectorAll('.badge')).find((x) => x.textContent === 'SATUROITUU');
    out.sat = getComputedStyle(b).borderTopColor;
    out.satText = getComputedStyle(b).color;
    return out;
  });
  check(bcol.sat === bcol.nrx && bcol.satText === bcol.nrx && bcol.sat !== bcol.cy,
    `SATUROITUU-merkki varoitusvärillä, ei syaanilla (${bcol.sat} / varoitus ${bcol.nrx} / syaani ${bcol.cy})`);
  await shot('main_sat.png');
  await post('/api/sim', 'base=-55&peak=0');

  // Moottori seis taas -> no_signal.
  await expectState('moottori seis 2', 'engine_on=0', 'no_signal', 'idle', 8000);
  await post('/api/sim', 'engine_on=1');

  // D7: master vaihtuu vain kahdella napautuksella (ei vahingossa kesken säädön).
  const masterOf = () => page.evaluate(() =>
    Array.from(document.querySelectorAll('.cyl')).findIndex((c) => c.classList.contains('m')));
  check(await masterOf() === 0, 'master on aluksi SYL 1');
  await page.click('.cyl >> nth=1');
  check(await page.isVisible('#toast'), 'ensimmäinen napautus pyytää vahvistuksen');
  check(await page.evaluate(() => document.querySelectorAll('.cyl')[1].classList.contains('arm')),
    'ensimmäinen napautus korostaa vahvistettavan kortin');
  check(await masterOf() === 0, 'master ei vaihdu yhdellä napautuksella');
  await page.click('.cyl >> nth=1');
  await page.waitForFunction(() => document.querySelectorAll('.cyl')[1].classList.contains('m'), null, { timeout: 4000 });
  check(await masterOf() === 1, 'master vaihtui kahdella napautuksella (SYL 2)');
  await post('/api/master', 'ch=0');
  await page.waitForFunction(() => document.querySelectorAll('.cyl')[0].classList.contains('m'), null, { timeout: 4000 });

  // Päivänvalonäkymän kytkin ja sen muistaminen.
  const sunOn = () => page.evaluate(() => document.documentElement.classList.contains('sun'));
  await page.click('#sun');
  check(await sunOn(), 'päivänvalonäkymä päälle');
  await page.reload();
  await page.waitForFunction(() => document.getElementById('banner').getAttribute('data-state'), null, { timeout: 8000 });
  check(await sunOn(), 'päivänvalonäkymä säilyy latauksen yli');
  await page.click('#sun');
  check(!(await sunOn()), 'päivänvalonäkymä pois');

  // 360 px: ei vaakavieritystä.
  await page.setViewportSize({ width: 360, height: 780 });
  await page.waitForTimeout(300);
  const sw = await page.evaluate(() => [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
  check(sw[0] <= sw[1], `ei vaakavieritystä 360 px:llä (${sw[0]} / ${sw[1]})`);

  // =======================================================================
  section('Huoltosivu');
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto(URL + '/#huolto');
  await page.waitForSelector('#f_0', { timeout: 5000 });
  const nf = await page.evaluate(() => document.querySelectorAll('.frow').length);
  const cfg = await getJson('/api/config');
  check(nf === cfg.fields.length, `asetuslomakkeessa kaikki kentät (${nf} / ${cfg.fields.length})`);
  const src = await page.evaluate(() => document.querySelector('select[id^=f_] option') !== null);
  check(src, 'select-kentät (master/source) rakennettu');
  const sm2 = await smallTargets();
  check(sm2.length === 0, `kosketusalueet >= 44 px huoltosivulla (${sm2.join(', ')})`);
  const tg = cfg.fields.findIndex((f) => f.key === 'tol_green_kpa');
  await page.fill('#f_' + tg, '5');
  await page.click('#bSave');
  await page.waitForSelector('#cErr:not([hidden])', { timeout: 3000 });
  check(/tol_green_kpa/.test(await page.textContent('#cErr')), 'ristiintarkistuksen virhe näytetään');
  await page.click('#bUndo');
  await page.fill('#f_' + tg, '1,2');
  await page.click('#bSave');
  await page.waitForFunction(() => /Tallennettu/.test(document.getElementById('cOk').textContent), null, { timeout: 3000 });
  const v = await getJson('/api/config');
  check(v.fields[tg].value === 1.2, `tol_green_kpa tallentui pilkulla (${v.fields[tg].value})`);
  await post('/api/config/reset', '');
  await page.waitForTimeout(500);
  await page.reload();
  await page.waitForSelector('#f_0', { timeout: 5000 });

  // Kalibrointipainikkeet: nollaus hylätään moottorin käydessä, vahvistusmittaus näkyy käynnissä, poisto toimii.
  await page.click('#bZero');
  await page.waitForFunction(() => /hylätty/.test(document.getElementById('zMsg').textContent), null, { timeout: 3000 });
  check(await page.evaluate(() => document.getElementById('zMsg').classList.contains('er')),
    `Nollaa moottorin käydessä: virheviesti näkyy ("${await page.textContent('#zMsg')}")`);
  await page.click('#bGain');
  await page.waitForSelector('#gBusy:not([hidden])', { timeout: 3000 });
  check(await page.isDisabled('#bGain'), 'Kalibroi vahvistus: mittaus käynnissä, painike pois käytöstä');
  await page.waitForSelector('#gBusy', { state: 'hidden', timeout: 6000 });
  check(/\S/.test(await page.textContent('#cMsg')), `Kalibroi vahvistus: tulos näkyy ("${await page.textContent('#cMsg')}")`);
  await page.click('#bGRst');
  await page.waitForFunction(() => /poistettu/.test(document.getElementById('cMsg').textContent), null, { timeout: 3000 });
  check(true, 'Poista vahvistuskalibrointi: viesti näkyy');
  await shotSlices('service');

  check(consoleErrors.length === 0 && pageErrors.length === 0,
    `ei konsolivirheitä eikä JS-poikkeuksia ennen uudelleenkäynnistystestejä (${consoleErrors.length + pageErrors.length}) ${consoleErrors.concat(pageErrors).slice(0, 3).join(' | ')}`);

  // (a) "Palauta oletukset" varoittaa uudelleenkäynnistyksestä, kun lähde vaihtuu.
  await post('/api/config', 'source=1');          // mock käynnistyy uudelleen (1,5 s), nollaus unohtuu
  await page.waitForTimeout(2200);
  await page.reload();
  await page.waitForSelector('#f_0', { timeout: 5000 });
  const srcIdx = cfg.fields.findIndex((f) => f.key === 'source');
  check(await page.inputValue('#f_' + srcIdx) === '1', 'lähde on nyt ADC (lomake luki sen laitteelta)');
  dialogs.length = 0;
  await page.click('#bDef');
  check(dialogs.length === 1 && /uudelleen/.test(dialogs[0]) && /ADC/.test(dialogs[0]) && /Simulaattori/.test(dialogs[0]),
    `Palauta oletukset: lähteen vaihto varoittaa uudelleenkäynnistyksestä ("${(dialogs[0] || '').replace(/\n+/g, ' / ')}")`);
  await page.waitForFunction(() => /Oletukset palautettu/.test(document.getElementById('cOk').textContent), null, { timeout: 5000 });
  check(/uudelleen/.test(await page.textContent('#cOk')), `Palauta oletukset: tulosviesti kertoo uudelleenkäynnistyksestä ("${await page.textContent('#cOk')}")`);
  await page.waitForTimeout(3600);                 // laite käynnistyy, lomake ladataan uudelleen
  check((await getJson('/api/config')).fields[srcIdx].value === 0, 'Palauta oletukset: lähde palasi simulaattoriin');
  check(await page.inputValue('#f_' + srcIdx) === '0', 'Palauta oletukset: lomake päivittyi uuteen lähteeseen');
  dialogs.length = 0;
  dialogAction = 'dismiss';
  await page.click('#bDef');
  dialogAction = 'accept';
  check(dialogs.length === 1 && !/uudelleen/.test(dialogs[0]),
    `Palauta oletukset: ilman lähteen vaihtoa ei uudelleenkäynnistysvaroitusta ("${(dialogs[0] || '').replace(/\n+/g, ' / ')}")`);

  // Simulaattoripaneeli: esiasetus -> tila vaihtuu.
  await page.click('[data-p="3"]');   // "Letku irti SYL 2"
  const fh = await waitState('fault_hose', 12000);
  check(fh.state === 'fault_hose', `esiasetus "Letku irti SYL 2" -> fault_hose (${fh.state})`);
  await page.click('[data-p="0"]');

  // Vaaka-asento.
  await page.goto(URL);
  await page.setViewportSize({ width: 844, height: 390 });
  await page.waitForTimeout(1500);
  await shot('main_landscape.png');
  await page.setViewportSize({ width: 390, height: 844 });

  // =======================================================================
  section('Synteettiset snapshotit (kiinteä syöte, UI:n päättely snapshotin kentistä)');
  const A = (v) => v.map((x) => x.cls).join(' | ');
  await synthView('synced + tuomio: vihreä', snap({}), (v) => {
    const p = invariantProblems(v);
    const sb = v.bars.filter((x) => x.shown);
    if (!(sb.length === 2 && sb.every((x) => x.cls === 'bar ok'))) p.push('palkit eivät vihreitä: ' + A(sb));
    if (v.meterOk !== 3) p.push('kaista ei vihreä');
    return p;
  });
  await synthView('synced ilman tuomiota (verdict=false): neutraali', snap({ verdict: false }), (v) => {
    const p = invariantProblems(v.state === 'synced' ? Object.assign({}, v, { state: 'x' }) : v);
    if (v.cls.includes('st-ok')) p.push('banneri vihreä ilman tuomiota');
    if (v.bars.some((x) => x.shown && x.cls !== 'bar neu')) p.push('palkki ei neutraali: ' + A(v.bars));
    if (v.meterOk) p.push('kaista vihreä ilman tuomiota');
    if (v.greens.length) p.push('vihreää: ' + v.greens.join(', '));
    if (v.acts.length || v.worst.length) p.push('neuvo ilman tuomiota');
    return p;
  });
  await synthView('near + kalibroimaton, ero rajan sisällä: ei vihreää eikä neuvoa',
    snap({ state: 'near', head: 'Lähellä – kalibroimaton', advice: 'Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse',
      spread: 0.9, worst: 2, flags: { sat: false, uncal_zero: true, uncal_gain: false, drift: false },
      ch: [chan(0), chan(0.3), chan(-0.9)] }), (v) => {
      const p = invariantProblems(v);
      if (v.bars.some((x) => x.shown && x.cls !== 'bar neu')) p.push('palkit eivät neutraaleja: ' + A(v.bars));
      if (v.acts.length || v.worst.length) p.push('AVAA/SULJE tai worst-kehys ilman neuvoa');
      return p;
    }, 'synth_uncal_near.png');
  await synthView('near + kalibroimaton + neuvo (ero > tol_green): neuvo näkyy, ei vihreää',
    snap({ state: 'near', head: 'Lähellä – kalibroimaton', advice: 'SYL 3: sulje läppää – imee 2,1 kPa vähemmän kuin SYL 1',
      act: { ch: 2, open: false },
      spread: 2.1, worst: 2, flags: { sat: false, uncal_zero: true, uncal_gain: false, drift: false },
      ch: [chan(0), chan(0.4), chan(-2.1)] }), (v) => {
      const p = invariantProblems(v);
      if (!(v.acts.length === 1 && v.acts[0].text === 'SULJE' && v.worst[0] === 2)) p.push('neuvo ei näy kortilla');
      if (v.bars[2].cls !== 'bar near' || v.bars[1].cls !== 'bar neu') p.push('palkit: ' + A(v.bars));
      return p;
    }, 'synth_uncal_near_advice.png');
  await synthView('out_of_sync + neuvo: AVAA, punainen palkki, tolerassa oleva neutraali',
    snap({ state: 'out_of_sync', head: 'Ei synkronissa', advice: 'SYL 2: avaa läppää – imee 4,2 kPa enemmän kuin SYL 1',
      act: { ch: 1, open: true }, spread: 4.2, worst: 1, ch: [chan(0), chan(4.2), chan(1.0)] }), (v) => {
      const p = invariantProblems(v);
      if (!(v.acts.length === 1 && v.acts[0].text === 'AVAA' && v.acts[0].i === 1)) p.push('AVAA puuttuu');
      if (v.bars[1].cls !== 'bar bad' || v.bars[2].cls !== 'bar neu') p.push('palkit: ' + A(v.bars));
      return p;
    }, 'synth_out_of_sync.png');
  await synthView('near ilman tuomiota (verdict=false): neutraali, ei neuvoa',
    snap({ state: 'near', verdict: false, head: 'Lähellä', advice: 'SYL 2: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1',
      act: { ch: 1, open: true }, spread: 2.1, worst: 1, ch: [chan(0), chan(2.1), chan(0)] }), (v) => {
      const p = [];
      if (v.bars.some((x) => x.shown && x.cls !== 'bar neu')) p.push('palkki ei neutraali: ' + A(v.bars));
      if (v.acts.length || v.worst.length) p.push('AVAA/SULJE tai worst-kehys ilman tuomiota');
      if (v.cls.includes('st-near') || v.cls.includes('st-bad') || v.cls.includes('st-ok')) p.push('banneri tuomiovärillä: ' + v.cls);
      return p;
    });
  // UI toistaa laitteen neuvon (act-kenttä, WP-10), ei johda sitä luvuista eikä jäsennä tekstiä:
  // kehys ja AVAA/SULJE seuraavat act-kenttää.
  await synthView('luvut vihjaavat säätöön (ero 2,4 > raja) mutta laite ei neuvo -> ei kehystä eikä AVAA/SULJE',
    snap({ state: 'near', head: 'Lähellä – kalibroimaton', advice: 'Nollaus puuttuu – irrota letkut ja tee nollaus',
      spread: 2.4, worst: 1, flags: { sat: false, uncal_zero: true, uncal_gain: false, drift: false },
      ch: [chan(0), chan(2.4), chan(0)] }), (v) =>
      v.acts.length || v.worst.length ? ['AVAA/SULJE tai kehys vaikka laite ei neuvo: ' + JSON.stringify(v.acts)] : []);
  await synthView('suunta tulee act-kentästä, ei d:n etumerkistä (act: sulje, d > 0)',
    snap({ state: 'near', head: 'Lähellä', advice: 'SYL 2: sulje läppää – imee 2,4 kPa vähemmän kuin SYL 1',
      act: { ch: 1, open: false }, spread: 2.4, worst: 1, ch: [chan(0), chan(2.4), chan(0)] }), (v) =>
      v.acts.length === 1 && v.acts[0].i === 1 && v.acts[0].text === 'SULJE' && v.worst[0] === 1 ? [] :
        ['kortin merkintä ei seuraa act-kenttää: ' + JSON.stringify(v.acts)]);
  await synthView('UI ei jäsennä tekstiä: ruuvineuvo tekstissä mutta act null -> ei merkintää',
    snap({ state: 'near', head: 'Lähellä', advice: 'SYL 2: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1',
      spread: 2.1, worst: 1, ch: [chan(0), chan(2.1), chan(0)] }), (v) =>
      v.acts.length || v.worst.length ? ['merkintä tekstistä ilman act-kenttää: ' + JSON.stringify(v.acts)] : []);
  await synthView('act ilman ruuvitekstiä: merkintä seuraa act-kenttää (AVAA SYL 3:lle)',
    snap({ state: 'out_of_sync', head: 'Ei synkronissa', advice: 'Ero 4,0 kPa', act: { ch: 2, open: true },
      spread: 4, worst: 2, ch: [chan(0), chan(0), chan(4)] }), (v) =>
      v.acts.length === 1 && v.acts[0].i === 2 && v.acts[0].text === 'AVAA' && v.worst[0] === 2 ? [] :
        ['kortin merkintä ei seuraa act-kenttää: ' + JSON.stringify(v.acts)]);
  for (const bad of [{ ch: 3, open: true }, { ch: -1, open: true }, { ch: 1.5, open: true }, { ch: '1', open: true },
    { ch: 1, open: 'true' }, { ch: 1 }, 5, 'SYL 2']) {
    await synthView('epäkelpo act ' + JSON.stringify(bad) + ' -> ei merkintää',
      snap({ state: 'near', head: 'Lähellä', advice: 'Ero 2,1 kPa', act: bad, spread: 2.1, worst: 1,
        ch: [chan(0), chan(2.1), chan(0)] }), (v) =>
        v.acts.length || v.worst.length ? ['merkintä epäkelvosta act-kentästä: ' + JSON.stringify(v.acts)] : []);
  }
  await synthView('epävakaa (bars=false): ei palkkeja, kaistoja eikä neuvoa',
    snap({ state: 'unstable', bars: false, verdict: false, head: 'Odota, käynti tasaantuu', advice: 'Vakautta odotetaan: 1,2 / 3,0 s',
      spread: 0, worst: -1, ch: [chan(0), chan(0), chan(0)] }), (v) => {
      const p = invariantProblems(v);
      if (v.bars.some((x) => x.shown) || v.meterOn !== 0) p.push('palkkeja tai kaistoja näkyvissä');
      return p;
    });

  // (e) asteikon tekstit eivät pyöristy kokonaisluvuiksi
  await synthView('asteikko, oletus tol 1,5 / 3 -> ±6', snap({}), (v) =>
    v.scale.every((s) => s[0] === '+6' && s[1] === '−6') ? [] : ['asteikko ' + JSON.stringify(v.scale)]);
  await synthView('asteikko, tol_yellow 2,75 -> ±5,5 (ei ±6)', snap({ tol: [1.5, 2.75] }), (v) =>
    v.scale.every((s) => s[0] === '+5,5' && s[1] === '−5,5') ? [] : ['asteikko ' + JSON.stringify(v.scale)]);
  await synthView('asteikko, tol_yellow 3,3 -> ±6,6 (ei ±7)', snap({ tol: [1.5, 3.3] }), (v) =>
    v.scale.every((s) => s[0] === '+6,6' && s[1] === '−6,6') ? [] : ['asteikko ' + JSON.stringify(v.scale)]);

  // (f) Vikapeite tulee laitteen kanavaluokituksesta (ch[i].cond, WP-10), ei tasosta eikä
  // hose_off_kpa:sta: sama taso antaa eri peitteen eri luokituksella, ja luokitus ilman
  // vikaa ei peitä mittaria, vaikka luvut näyttäisivät irronneelta letkulta.
  const off = (cond, level) => chan(0, { pulse: false, mean: null, min: null, p2p: null, rpm: null, level, cond });
  const fault = (st, ch, o) => snap(Object.assign({ state: st, bars: false, verdict: false, fault_ch: 1, spread: 0,
    worst: -1, head: 'VIKA: SYL 2 – ei alipainetta, tarkista letku',
    advice: 'Letku irti tai vuotaa – kytke ja tarkista liitokset', ch }, o || {}));
  const fovRed = () => page.evaluate(() => Array.from(document.querySelectorAll('.cyl .fov'))
    .map((f) => (f.classList.contains('on') ? (f.classList.contains('grey') ? 'harmaa' : 'punainen') : '')));
  await synth([fault('fault_hose', [chan(0), off('hose', 1.5), chan(0)])], async () => {
    const v = await view();
    const col = await fovRed();
    check(v.fov[1] === 'LETKU IRTI?' && col[1] === 'punainen' && !v.fov[0] && !v.fov[2],
      `cond hose -> punainen LETKU IRTI? vain SYL 2:lla (${JSON.stringify(v.fov)} ${JSON.stringify(col)})`);
    await shot('synth_fault_hose.png');
  });
  await synthView('cond blocked, taso 1,5 kPa (hose_off_kpa:n alla) -> EI PULSSIA, ei LETKU IRTI?',
    fault('fault_hose', [chan(0), off('blocked', 1.5), chan(0)], { head: 'VIKA: SYL 2 – ei pulssia, tarkista letku' }),
    (v) => (v.fov[1] === 'EI PULSSIA' ? [] : ['vikapeite "' + v.fov[1] + '"']));
  await synthView('cond hose, taso 3 kPa (hose_off_kpa:n yli) -> LETKU IRTI? (laite päätti, ei UI)',
    fault('fault_hose', [chan(0), off('hose', 3), chan(0)]),
    (v) => (v.fov[1] === 'LETKU IRTI?' ? [] : ['vikapeite "' + v.fov[1] + '"']));
  await synth([fault('unstable', [chan(0), off('no_pulse', -2.1), chan(0)],
    { fault_ch: -1, head: 'Odota, käynti tasaantuu', advice: 'SYL 2: ei pulssia (taso -2,1 kPa)' })], async () => {
    const v = await view();
    const col = await fovRed();
    check(v.fov[1] === 'EI PULSSIA' && col[1] === 'harmaa',
      `cond no_pulse (pitoaika kesken) -> harmaa EI PULSSIA (${JSON.stringify(v.fov)} ${JSON.stringify(col)})`);
  });
  await synthView('pulssiton kanava, jonka laite luokittelee ok:ksi -> ei peitettä (UI ei päättele luvuista)',
    fault('fault_hose', [chan(0), off('hose', 0.2), off('ok', 0.2)]),
    (v) => (v.fov[1] === 'LETKU IRTI?' && v.fov[2] === '' ? [] : ['vikapeitteet ' + JSON.stringify(v.fov)]));
  await synthView('master-vika: masterin oma luokitus (anturi) näkyy kortilla',
    fault('fault_master', [off('sensor', -108), chan(0), chan(0)], { fault_ch: 0,
      head: 'Master (SYL 1) ei kelpaa viitteeksi – valitse toinen', advice: 'SYL 1: anturivika (jännite 0 mV)' }),
    (v) => (v.fov[0] === 'ANTURIVIKA' && !v.fov[1] && !v.fov[2] ? [] : ['vikapeitteet ' + JSON.stringify(v.fov)]));
  await synthView('vikatila ilman luokitusta (cond puuttuu): nimetty kanava saa VIKA-peitteen',
    fault('fault_hose', [chan(0), off(undefined, 0.2), chan(0)]),
    (v) => (v.fov[1] === 'VIKA' && !v.fov[0] && !v.fov[2] ? [] : ['vikapeitteet ' + JSON.stringify(v.fov)]));

  // Päivänvalonäkymä (aurinkoteema): kuvat lähemmäs "puhelin ulkona" -tilannetta
  await page.addInitScript(() => { try { localStorage.setItem('sbx_sun', '1'); } catch (e) { /* ei tallennusta */ } });
  await synthView('päivänvalo: synced', snap({}), () => [], 'sun_synced.png');
  await synthView('päivänvalo: near + neuvo', snap({ state: 'near', head: 'Lähellä',
    advice: 'SYL 3: sulje läppää – imee 2,1 kPa vähemmän kuin SYL 1', act: { ch: 2, open: false }, spread: 2.1,
    worst: 2, ch: [chan(0), chan(0.4), chan(-2.1)] }), () => [], 'sun_near.png');
  await synthView('päivänvalo: kalibroimaton', snap({ state: 'near', head: 'Lähellä – kalibroimaton',
    advice: 'Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse', spread: 0.9, worst: 2,
    flags: { sat: true, uncal_zero: true, uncal_gain: true, drift: false }, ch: [chan(0), chan(0.3), chan(-0.9)] }),
  () => [], 'sun_uncal_near.png');
  await synthView('päivänvalo: ei synkronissa', snap({ state: 'out_of_sync', head: 'Ei synkronissa',
    advice: 'SYL 2: avaa läppää – imee 4,2 kPa enemmän kuin SYL 1', act: { ch: 1, open: true }, spread: 4.2,
    worst: 1, ch: [chan(0), chan(4.2), chan(1.0)] }), () => [], 'sun_out_of_sync.png');
  await page.addInitScript(() => { try { localStorage.setItem('sbx_sun', '0'); } catch (e) { /* ei tallennusta */ } });
  await page.evaluate(() => { try { localStorage.setItem('sbx_sun', '0'); } catch (e) { /* ei tallennusta */ } });

  // 360 px: kapein puhelin, pahin tapaus (neuvo + kaikki merkit)
  await page.setViewportSize({ width: 360, height: 740 });
  await synthView('360 px: kaikki merkit + neuvo', snap({ state: 'out_of_sync', head: 'Ei synkronissa',
    advice: 'SYL 2: avaa läppää – imee 12,2 kPa enemmän kuin SYL 1', act: { ch: 1, open: true }, spread: 12.2,
    worst: 1, flags: { sat: true, uncal_zero: true, uncal_gain: true, drift: true },
    ch: [chan(0), chan(12.2, { cond: 'sat' }), chan(1.0)] }),
  (v) => {
    const p = [];
    if (!v.bars[1].shown) p.push('palkki puuttuu');
    if (!(v.acts.length === 1 && v.acts[0].i === 1 && v.acts[0].text === 'AVAA')) p.push('AVAA puuttuu');
    return p;
  }, 'main_360_worst.png');
  const sw360 = await page.evaluate(() => [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
  check(sw360[0] <= sw360[1], `ei vaakavieritystä 360 px:llä pahimmalla sisällöllä (${sw360[0]} / ${sw360[1]})`);
  await page.setViewportSize({ width: 390, height: 844 });

  // =======================================================================
  section('Kestävyys ja yhteyskatko');
  // Puuttuvat/väärän tyyppiset kentät ja 4 kanavaa eivät saa kaataa sivua.
  const weird = [{}, { ch: null, tol: 'x', rpm: null, flags: 3 },
    { state: 'near', verdict: true, bars: true, master: 0, worst: 1, tol: [1.5, 3], ch: [{}, { d: 'a' }, { d: null }] },
    { state: 'out_of_sync', verdict: true, bars: true, master: 5, worst: 3, rpm: 1e9, rpm_sd: -1, tol: [0, 0],
      ch: [{ d: 0 }, { d: 99 }, { d: -99, fault: true }, { d: 1, pulse: false }],
      wave: { fs: 0, s: [[1], [null, 2, 3], 'x', [-200, 50, NaN]] }, cal: null, diag: 'x' },
    { state: 'near', verdict: true, bars: true, master: 0, worst: 1, spread: 9, tol: [3, 1], ch: [{ d: 0 }, { d: 5 }, { d: 1 }] },
    { state: 'near', verdict: true, bars: true, master: 0, worst: 1, act: { ch: '__proto__', open: 1 },
      ch: [{ d: 0, cond: '__proto__' }, { d: 2, cond: 7 }, { d: 1, cond: 'constructor' }] },
    { state: 'fault_hose', fault_ch: 9, head: 7, advice: null, act: 3, ch: [{ pulse: false, level: 0, cond: null }], src: null }];
  const body = 'retry: 60000\n\n' + weird.map((w) => 'event: snap\ndata: ' + JSON.stringify(w) + '\n\n').join('') +
    'event: snap\ndata: {broken json\n\n';
  const errsBefore = pageErrors.length;
  await page.route('**/events', (r) => r.fulfill({ status: 200, headers: { 'Content-Type': 'text/event-stream' }, body }));
  await page.goto(URL);
  await page.waitForTimeout(1200);
  await page.evaluate(() => { document.getElementById('diagbox').open = true; });
  await page.waitForTimeout(300);
  check(pageErrors.length === errsBefore, `virheelliset snapshotit eivät kaada sivua (${pageErrors.slice(errsBefore).join(' | ')})`);
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

  check(pageErrors.length === 0, `ei JS-poikkeuksia koko ajon aikana (${pageErrors.length}) ${pageErrors.slice(0, 3).join(' | ')}`);

  await browser.close();
  console.log(failures ? `\nTULOS: ${failures} epäonnistui` : '\nTULOS: OK');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
