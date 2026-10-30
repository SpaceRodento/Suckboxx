#!/usr/bin/env node
// Suckboxx UI:n Playwright-testi + kuvakaappaukset (WP-5). Vaatii TUOREENA
// käynnistetyn mock-palvelimen (ei --calibrated: testi tarkistaa ensin
// kalibroimattoman tilan, D11) ja Playwrightin (Node).
//
//   python3 tools/mock_server.py [--minified] &
//   node tools/ui_screenshots.js --url http://localhost:8000 --out /tmp/ui_shots
//
// Ajaa simulaattorin käytösmatriisin tilojen läpi (§8), tarkistaa bannerin,
// palkkien värit ja säätövihjeen laitteen arviota vasten, ajaa kiinteät
// snapshot-fixturet (reunatapaukset ilman simulaattorin satunnaisuutta),
// kosketuskohteiden koon, oletusten palautuksen uudelleenkäynnistyksineen ja
// sen, ettei konsolissa ole virheitä. Jos mock tarjoilee minimoidun sivun
// (--minified), sitä verrataan lähteeseen: DOM, CSSOM ja JS:n AST (typescript).
// Exit 1 jos jokin tarkistus epäonnistuu.

const path = require('path');
const fs = require('fs');
const { chromium } = require('playwright');

function arg(name, def) {
  const i = process.argv.indexOf(name);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : def;
}
const URL = arg('--url', 'http://localhost:8000');
const OUT = arg('--out', path.join(process.cwd(), 'ui_shots'));
const SRC = path.join(__dirname, '..', 'web', 'index.html');
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
function check(ok, msg) {
  console.log((ok ? '  ok   ' : '  FAIL ') + msg);
  if (!ok) failures++;
}

// JS:n rakenne ilman välilyöntejä ja kommentteja: minimoinnin on säilytettävä
// tämä täsmälleen (myös ASI:n lauserajat näkyvät puussa).
function astSig(ts, code) {
  const sf = ts.createSourceFile('page.js', code, ts.ScriptTarget.ES2017, true, ts.ScriptKind.JS);
  const out = [];
  (function walk(n, depth) {
    let t = ts.SyntaxKind[n.kind];
    if (n.text !== undefined && n.kind !== ts.SyntaxKind.SourceFile) t += ':' + n.text;
    if (n.operator !== undefined) t += ':' + n.operator;
    out.push(depth + ' ' + t);
    ts.forEachChild(n, (c) => walk(c, depth + 1));
  })(sf, 0);
  return { sig: out, diags: sf.parseDiagnostics.length };
}

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2,
    isMobile: true, hasTouch: true, locale: 'fi-FI' });
  const page = await ctx.newPage();
  const errors = [];
  const pageErrors = [];
  const dialogs = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => { errors.push('pageerror: ' + e); pageErrors.push(String(e)); });
  page.on('dialog', (d) => { dialogs.push(d.message()); d.accept(); });

  const post = (p, data) => page.evaluate(([p2, d]) => fetch(p2, { method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: d }).then((r) => r.json()), [p, data]);
  const banner = () => page.evaluate(() => {
    const b = document.getElementById('banner');
    return { state: b.getAttribute('data-state'), cls: b.className,
      head: document.getElementById('head').textContent, advice: document.getElementById('advice').textContent };
  });
  // Korttien näkyvä tila: palkin luokka, säätövihje, korostus, vikapeite.
  const cards = () => page.evaluate(() => Array.from(document.querySelectorAll('.cyl')).map((c) => {
    const bar = c.querySelector('.bar');
    const act = c.querySelector('.act');
    const fov = c.querySelector('.fov');
    return { bar: bar.style.display === 'block' ? bar.className.replace('bar ', '') : '',
      act: act.classList.contains('on') ? act.textContent : '', worst: c.classList.contains('w'),
      master: c.classList.contains('m'), dv: c.querySelector('.dv').textContent,
      fov: fov.classList.contains('on') ? fov.textContent : '',
      top: c.querySelector('.sc.t').textContent, bot: c.querySelector('.sc.b').textContent };
  }));
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
      await extra(await banner(), await cards());
    }
    return b;
  }
  // Palkkien värin on seurattava laitteen arviota (spread, D2) eikä kanavan
  // omaa |d|:tä: säädettävä kanava = tilan väri, muut neutraaleja.
  function expectAdjust(label, b, cs, color) {
    const adj = cs.map((c, i) => (c.act ? i : -1)).filter((i) => i >= 0);
    check(adj.length === 1, `${label}: täsmälleen yksi säätövihje (${JSON.stringify(cs.map((c) => c.act))})`);
    const k = adj[0];
    if (k === undefined) return;
    check(cs[k].bar === color, `${label}: säädettävän SYL ${k + 1} palkki ${color} (${cs[k].bar})`);
    check(cs[k].worst, `${label}: säädettävä kortti korostettu`);
    const dir = cs[k].act === 'AVAA' ? 'avaa' : 'sulje';
    check(b.advice.indexOf('SYL ' + (k + 1) + ': ' + dir) === 0, `${label}: vihje vastaa ohjetta (${cs[k].act} / ${b.advice})`);
    cs.forEach((c, i) => {
      if (i !== k && !c.master && c.bar) check(c.bar === 'neu', `${label}: SYL ${i + 1} palkki neutraali (${c.bar})`);
    });
    check(cs.every((c) => c.bar !== 'ok'), `${label}: ei vihreää palkkia (${cs.map((c) => c.bar).join(',')})`);
  }
  const shot = (name, full) => page.screenshot({ path: path.join(OUT, name), fullPage: !!full });

  // ---- tarjoiltu sivu vs. lähde (minimointi ei saa muuttaa mitään) ---------
  const src = fs.readFileSync(SRC, 'utf8').replace(/\r\n?/g, '\n');
  const served = await (await fetch(URL + '/')).text();
  const bytes = Buffer.byteLength(served);
  const minified = served !== src;
  console.log(`  info tarjoiltu sivu ${bytes} tavua (${minified ? 'minimoitu' : 'lähde sellaisenaan'})`);
  check(!/https?:\/\//.test(served), 'sivulla ei ulkoisia osoitteita');
  if (minified) {
    check(bytes < 40000, `minimoitu sivu < 40 000 tavua (${bytes})`);
    await page.goto('about:blank');
    const sig = await page.evaluate(([a, b]) => {
      // DOM ilman script/style-sisältöä; peräkkäinen teksti yhdistetään ja
      // välilyönnit tiivistetään kuten selain renderöi.
      function dom(html) {
        const d = new DOMParser().parseFromString(html, 'text/html');
        const out = [];
        let text = null;
        const flush = () => { if (text !== null) out.push('#' + text.replace(/\s+/g, ' ')); text = null; };
        (function walk(n) {
          for (let c = n.firstChild; c; c = c.nextSibling) {
            if (c.nodeType === 3) { text = (text || '') + c.data; continue; }
            if (c.nodeType !== 1) continue;
            flush();
            out.push('<' + c.tagName + ' ' + Array.from(c.attributes).map((x) => x.name + '=' + x.value).sort().join(' '));
            if (c.tagName !== 'SCRIPT' && c.tagName !== 'STYLE') walk(c);
            flush();
            out.push('</' + c.tagName);
          }
        })(d);
        flush();
        const sh = new CSSStyleSheet();
        sh.replaceSync(d.querySelector('style').textContent);
        // var()-arvot sarjallistetaan raakoina tokeneina; välilyönti pilkun
        // ympärillä ei ole CSS:ssä merkitsevä, joten se normalisoidaan.
        const css = Array.from(sh.cssRules).map((r) => r.cssText.replace(/\s*,\s*/g, ',')).join('\n');
        return { dom: out.join('\n'), css, n: sh.cssRules.length };
      }
      const x = dom(a);
      const y = dom(b);
      return { dom: x.dom === y.dom, css: x.css === y.css, rules: [x.n, y.n] };
    }, [src, served]);
    check(sig.dom, 'minimoitu DOM (elementit, attribuutit, teksti) = lähde');
    check(sig.css, `minimoitu CSSOM = lähde (${sig.rules[0]} / ${sig.rules[1]} sääntöä)`);
    let ts = null;
    try {
      ts = require('typescript');
    } catch (e) {
      console.log('  info typescript puuttuu: JS:n AST-vertailu ohitetaan');
    }
    if (ts) {
      const js = (h) => /<script>([\s\S]*?)<\/script>/.exec(h)[1];
      const a = astSig(ts, js(src));
      const b = astSig(ts, js(served));
      check(a.diags === 0 && b.diags === 0, `JS jäsentyy virheettä (lähde ${a.diags}, minimoitu ${b.diags})`);
      let diff = -1;
      for (let i = 0; i < Math.max(a.sig.length, b.sig.length); i++) if (a.sig[i] !== b.sig[i]) { diff = i; break; }
      check(diff < 0, `minimoidun JS:n AST = lähde (${a.sig.length} solmua${diff >= 0 ? ', ero kohdassa ' + diff + ': ' + a.sig[diff] + ' / ' + b.sig[diff] : ''})`);
    }
  }

  await page.goto(URL);
  await page.waitForFunction(() => document.getElementById('banner').getAttribute('data-state'), null, { timeout: 10000 });
  check(await page.isVisible('#simbar'), 'SIMULAATTORI-palkki näkyy kun src=sim');
  await post('/api/config/reset', '');

  // ---- D11 päästä päähän: kalibroimaton, pieni ero -> ei vihreää, ei ruuvia --
  const cal0 = await page.evaluate(() => new Promise((res) => {
    const es = new EventSource('/events');
    es.addEventListener('snap', (e) => { es.close(); res(JSON.parse(e.data)); });
  }));
  check(cal0.cal && cal0.cal.zero === false, 'mock on tuore (nollaus puuttuu) – käynnistä mock uudelleen ennen testiä');
  await expectState('kalibroimaton SYL 3 +1,0', 'engine_on=1&err1=0&err2=0&err3=1&fault1=0&fault2=0&fault3=0&rpm=850&drift=0',
    'near', 'near', 15000, async (b, cs) => {
      check(b.head === 'Lähellä – kalibroimaton', `D11 otsikko (${b.head})`);
      check(/^Nollaus puuttuu/.test(b.advice), `D11 ohje pyytää nollausta (${b.advice})`);
      check(cs.every((c) => c.bar !== 'ok'), `D11: ei vihreää palkkia (${cs.map((c) => c.bar).join(',')})`);
      check(cs.every((c) => !c.act), `D11: ei säätövihjettä (${cs.map((c) => c.act).join(',')})`);
      check(cs.every((c) => !c.worst), 'D11: ei korostettua korttia');
      check(cs[2].bar === 'neu', `D11: SYL 3 palkki neutraali (${cs[2].bar})`);
    });
  await shot('main_uncal.png');

  // Rivi 1: moottori seis -> ei signaalia; nollaus onnistuu vain nyt.
  await expectState('moottori seis', 'engine_on=0&err3=0', 'no_signal', 'idle', 8000);
  await page.waitForTimeout(1500);   // EMA-taso asettuu ilmanpaineeseen
  const z = await post('/api/cal/zero', '');
  check(z.ok === true, `nollaus moottori seis: ${z.msg}`);
  await shot('main_no_signal.png');

  // Rivi 3: käynnistys -> hetken epävakaa -> synkronoitu.
  await post('/api/sim', 'engine_on=1');
  const u = await waitState('unstable', 4000);
  check(u.state === 'unstable', `käynnistyksen jälkeen epävakaa (${u.advice})`);
  await expectState('synkronoitu', null, 'synced', 'ok', 12000, async (b, cs) => {
    check(b.head === 'Synkronoitu', `otsikko "Synkronoitu" (${b.head})`);
    check(!(await page.isVisible('.badge >> text=KALIBROIMATON')), 'KALIBROIMATON-merkki pois nollauksen jälkeen');
    check(await page.isVisible('.badge >> text=VAHVISTUS KALIBROIMATTA'), 'VAHVISTUS KALIBROIMATTA -merkki näkyy');
    const rpm = await page.textContent('#rpm');
    check(Math.abs(Number(rpm) - 850) < 30, `rpm ~850 (${rpm})`);
    check(await page.isVisible('.cyl.m .mtag'), 'MASTER-merkintä näkyy');
    check(cs[1].bar === 'ok' && cs[2].bar === 'ok', `synkronoitu ja nollattu: palkit vihreät (${cs[1].bar},${cs[2].bar})`);
    check(cs.every((c) => !c.act), 'synkronoitu: ei säätövihjettä');
  });
  await page.waitForTimeout(600);
  await shot('main_synced.png');

  // Spread (D2) vs. kanavakohtainen |d|: kumpikin |d| = 1,4 < tol_green,
  // mutta ero 2,8 kPa -> keltainen. Palkit eivät saa näyttää vihreää.
  await expectState('SYL 2 −1,4 / SYL 3 +1,4', 'err2=-1.4&err3=1.4', 'near', 'near', 10000, async (b, cs) => {
    expectAdjust('spread 2,8', b, cs, 'near');
  });
  await page.waitForTimeout(400);
  await shot('main_near_spread.png');
  await expectState('SYL 2 −2,9 / SYL 3 +2,9', 'err2=-2.9&err3=2.9', 'out_of_sync', 'bad', 10000, async (b, cs) => {
    expectAdjust('spread 5,8', b, cs, 'bad');
  });
  await post('/api/sim', 'err2=0&err3=0');
  await waitState('synced', 10000);

  // Rivi 4a: SYL 2 +2,5 kPa (vähemmän alipainetta) -> lähellä (spread 2,5 ≤ 3,0).
  await expectState('SYL 2 +2,5', 'err2=2.5', 'near', 'near', 8000, async (b, cs) => {
    check(/SYL 2: sulje läppää – imee 2,5 kPa vähemmän kuin SYL 1/.test(b.advice), `säätöohje (${b.advice})`);
    expectAdjust('SYL 2 +2,5', b, cs, 'near');
  });
  // Rivi 4b: SYL 2 +4 -> ei synkronissa.
  await expectState('SYL 2 +4', 'err2=4', 'out_of_sync', 'bad', 8000, async (b, cs) => {
    check(/^−4,0/.test(cs[1].dv), `SYL 2 d = −4,0 kPa (${cs[1].dv})`);
    check(cs[1].act === 'SULJE', `SYL 2 säätösuunta SULJE (${cs[1].act})`);
    expectAdjust('SYL 2 +4', b, cs, 'bad');
  });
  await page.waitForTimeout(600);
  await shot('main_out_of_sync.png');

  // Rivi 12: masterin (SYL 1) letku irti -> master-vika, kanavan vikapeite.
  await expectState('SYL 1 letku irti', 'err2=0&fault1=1', 'fault_master', 'fault', 8000, async (b, cs) => {
    check(/Master \(SYL 1\)/.test(b.head), `otsikko kertoo master-viasta (${b.head})`);
    check(cs[0].fov === 'LETKU IRTI?', `SYL 1 vikapeite (${cs[0].fov})`);
    check(await page.isVisible('#mhint.hot'), 'master-valitsin korostettu');
  });
  await page.waitForTimeout(400);
  await shot('main_fault_master.png');

  // Rivi 6: muu kuin master -> letkuvika.
  await expectState('SYL 3 letku irti', 'fault1=0&fault3=1', 'fault_hose', 'fault', 10000);
  await post('/api/sim', 'fault3=0');

  // Rivi 9: 4000 rpm -> arvio ja palkit piiloon, rpm näkyy.
  await expectState('4000 rpm', 'rpm=4000', 'rpm_too_high', 'idle', 10000, async (b, cs) => {
    check(cs.every((c) => !c.bar), 'palkit piilossa (bars=false)');
    check(cs.every((c) => !c.act), 'ei säätövihjettä ilman arviota');
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

  // ---- 360 px: ei vaakavieritystä, kosketuskohteet ≥ 44 px ------------------
  async function smallTargets() {
    return page.evaluate(() => {
      const out = [];
      document.querySelectorAll('button, a, input, select, summary').forEach((e) => {
        const r = e.getBoundingClientRect();
        if (!r.width && !r.height) return;
        if (r.height < 43.5 || r.width < 43.5) out.push((e.id || e.tagName) + ' ' + Math.round(r.width) + 'x' + Math.round(r.height));
      });
      return out;
    });
  }
  await page.setViewportSize({ width: 360, height: 780 });
  await page.waitForTimeout(300);
  let sw = await page.evaluate(() => [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
  check(sw[0] <= sw[1], `ei vaakavieritystä 360 px:llä (${sw[0]} / ${sw[1]})`);
  let small = await smallTargets();
  check(small.length === 0, `päänäkymän kosketuskohteet ≥ 44 px (${small.join(', ')})`);

  // Huoltosivu: lomake rakentuu /api/config:sta, virhe näkyy.
  await page.goto(URL + '/#huolto');
  await page.waitForSelector('#f_0', { timeout: 5000 });
  await page.waitForSelector('#s_rpm', { timeout: 5000 });
  sw = await page.evaluate(() => [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
  check(sw[0] <= sw[1], `huoltosivu: ei vaakavieritystä 360 px:llä (${sw[0]} / ${sw[1]})`);
  small = await smallTargets();
  check(small.length === 0, `huoltosivun kosketuskohteet ≥ 44 px (${small.join(', ')})`);
  await page.setViewportSize({ width: 390, height: 844 });
  const nf = await page.evaluate(() => document.querySelectorAll('.frow').length);
  const cfg = await page.evaluate(() => fetch('/api/config').then((r) => r.json()));
  check(nf === cfg.fields.length, `asetuslomakkeessa kaikki kentät (${nf} / ${cfg.fields.length})`);
  const srcSel = await page.evaluate(() => document.querySelector('select[id^=f_] option') !== null);
  check(srcSel, 'select-kentät (master/source) rakennettu');
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

  // ---- oletusten palautus, kun lähde ei ole oletus -> uudelleenkäynnistys -----
  // Ennen korjausta lomake jäi pysyvästi "lataus epäonnistui (HTTP 503)" -tilaan.
  async function waitConfig(ms) {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) {
      const r = await page.evaluate(() => fetch('/api/config').then((x) => (x.ok ? x.json() : null)).catch(() => null));
      if (r) return r;
      await page.waitForTimeout(300);
    }
    return null;
  }
  const pe0 = pageErrors.length;
  dialogs.length = 0;
  await page.click('#bDef');   // lähde on jo oletus: ei uudelleenkäynnistysvaroitusta
  await page.waitForTimeout(500);
  check(dialogs.length === 1 && !/uudelleen/.test(dialogs[0]), `palautus ilman lähteen vaihtoa ei varoita turhaan (${dialogs[0]})`);
  // Lähde vaihdetaan "toisesta puhelimesta" (suoraan API:lla) huoltosivun
  // ollessa auki: yhteyden palatessa lomakkeen on päivityttävä itsestään.
  const srcIdx = cfg.fields.findIndex((f) => f.key === 'source');
  await post('/api/config', 'source=1');
  const c1 = await waitConfig(10000);
  check(c1 && c1.fields.find((f) => f.key === 'source').value === 1, 'lähde vaihdettu ADC:ksi (laite käynnistyi uudelleen)');
  const fresh = await page.waitForFunction((i) => { const e = document.getElementById('f_' + i); return e && e.value === '1'; },
    srcIdx, { timeout: 10000 }).then(() => true).catch(() => false);
  check(fresh, 'avoin lomake päivittyy yhteyden palattua (lähde = ADC)');
  await page.reload();
  await page.waitForSelector('#f_' + srcIdx, { timeout: 8000 });
  dialogs.length = 0;
  await page.click('#bDef');
  await page.waitForFunction(() => /käynnistyy uudelleen/.test(document.getElementById('cOk').textContent), null, { timeout: 4000 })
    .catch(() => {});
  check(dialogs.length === 1 && /käynnistyy uudelleen/.test(dialogs[0]), `palautus varoittaa uudelleenkäynnistyksestä (${dialogs[0]})`);
  check(/käynnistyy uudelleen/.test(await page.textContent('#cOk')), 'palautuksen jälkeen kerrotaan uudelleenkäynnistyksestä');
  // Lomake ladataan uudelleen viiveellä (laite käynnistyy): odotetaan, että
  // kaikki rivit ovat taas paikallaan ja lähde näyttää oletusta.
  const reloaded = await page.waitForFunction(([n, i]) => {
    const e = document.getElementById('f_' + i);
    return document.querySelectorAll('.frow').length === n && e && e.value === '0';
  }, [cfg.fields.length, srcIdx], { timeout: 20000 }).then(() => true).catch(() => false);
  const selVal = await page.evaluate((i) => { const e = document.getElementById('f_' + i); return e ? e.value : '?'; }, srcIdx);
  check(reloaded, `asetuslomake latautuu uudelleenkäynnistyksen jälkeen, lähde = Simulaattori (${selVal})`);
  const c2 = await waitConfig(5000);
  check(c2 && c2.fields.find((f) => f.key === 'source').value === 0, 'lähde palautui simulaattoriksi');
  await page.waitForSelector('#simbar:not([hidden])', { timeout: 10000 }).catch(() => {});
  check(await page.isVisible('#simbar'), 'SIMULAATTORI-palkki palaa');
  check(pageErrors.length === pe0, `uudelleenkäynnistys ei aiheuta JS-virheitä (${pageErrors.slice(pe0).join(' | ')})`);
  // Asetushaku vastaa hetken 503 (laite käynnistymässä): lomake yrittää uudelleen.
  let n503 = 0;
  await page.route('**/api/config', (r) => {
    if (r.request().method() === 'GET' && n503 < 2) {
      n503++;
      return r.fulfill({ status: 503, contentType: 'text/plain', body: 'laite käynnistyy uudelleen' });
    }
    return r.continue();
  });
  await page.reload();   // sama #huolto-osoite: goto ei lataisi sivua uudelleen
  const retryTxt = await page.waitForFunction(() => /yritetään uudelleen/.test(document.getElementById('cfg').textContent),
    null, { timeout: 3000 }).then(() => true).catch(() => false);
  check(retryTxt, 'asetushaun 503: "yritetään uudelleen" näkyy');
  const retried = await page.waitForFunction((n) => document.querySelectorAll('.frow').length === n, cfg.fields.length,
    { timeout: 8000 }).then(() => true).catch(() => false);
  check(retried && n503 === 2, `asetuslomake latautuu 503-vastausten jälkeen (${n503} × 503)`);
  await page.unroute('**/api/config');

  // Vaaka-asento.
  await page.goto(URL);
  await page.setViewportSize({ width: 844, height: 390 });
  await page.waitForTimeout(1500);
  await shot('main_landscape.png');
  await page.setViewportSize({ width: 390, height: 844 });

  // ---- kiinteät snapshotit: laitteen ja UI:n päätösten reunatapaukset --------
  const chan = (d, extra) => Object.assign({ d, mean: -55 - d, level: -55 - d, min: -72, p2p: 25, rpm: 851, mv: 1323,
    pulse: true, fault: false, sat: false }, extra || {});
  function snap(over) {
    const s = { v: 1, t: 1000, src: 'sim', state: 'synced', fault_ch: -1, bars: true, verdict: true,
      head: 'Synkronoitu', advice: 'Valmis: ero 0,4 kPa (raja 1,5 kPa)',
      flags: { sat: false, uncal_zero: false, uncal_gain: true, drift: false }, master: 0, tol: [1.5, 3.0],
      rpm: 851, rpm_sd: 9, rpm_cv: 0.011, spread: 0.4, worst: 1, ch: [chan(0), chan(0.4), chan(-0.0)],
      wave: { fs: 62.5, s: [] }, cal: { busy: false, zero: true, gain: false, msg: '' },
      diag: { fs: 1000, gaps: 0, heap: 180000, frozen: false } };
    Object.keys(over).forEach((k) => {
      s[k] = k === 'flags' ? Object.assign({}, s.flags, over.flags) : over[k];
    });
    return s;
  }
  async function fixture(label, s, cfgBody) {
    await page.route('**/events', (r) => r.fulfill({ status: 200, headers: { 'Content-Type': 'text/event-stream' },
      body: 'retry: 60000\n\nevent: snap\ndata: ' + JSON.stringify(s) + '\n\n' }));
    if (cfgBody) {
      await page.route('**/api/config', (r) => r.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(cfgBody) }));
    }
    await page.goto(URL);
    await page.waitForFunction((st) => document.getElementById('banner').getAttribute('data-state') === st, s.state, { timeout: 5000 });
    await page.waitForTimeout(400);   // asetushaku (hose_off_kpa) ehtii perille
    const r = { b: await banner(), cs: await cards() };
    await page.unroute('**/events');
    if (cfgBody) await page.unroute('**/api/config');
    return r;
  }
  let f = await fixture('F1', snap({ state: 'near', head: 'Lähellä – kalibroimaton',
    advice: 'Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse', flags: { uncal_zero: true },
    spread: 1.0, worst: 2, ch: [chan(0), chan(0.2), chan(-1.0)] }));
  check(f.cs.every((c) => !c.act && !c.worst), 'F1 D11 ero ≤ tol_green: ei säätövihjettä eikä korostusta');
  check(f.cs[1].bar === 'neu' && f.cs[2].bar === 'neu', `F1 D11: palkit neutraaleja (${f.cs[1].bar},${f.cs[2].bar})`);
  check(f.b.cls.indexOf('st-near') >= 0, `F1 banneri keltainen (${f.b.cls})`);
  f = await fixture('F2', snap({ state: 'near', head: 'Lähellä – kalibroimaton',
    advice: 'SYL 3: sulje läppää – imee 2,0 kPa vähemmän kuin SYL 1', flags: { uncal_zero: true },
    spread: 2.3, worst: 2, ch: [chan(0), chan(0.3), chan(-2.0)] }));
  check(f.cs[2].act === 'SULJE' && f.cs[2].worst, `F2 kalibroimaton, ero > tol_green: laite nimeää ruuvin -> vihje (${f.cs[2].act})`);
  check(f.cs[2].bar === 'near' && f.cs[1].bar === 'neu', `F2 palkit near/neu (${f.cs[2].bar},${f.cs[1].bar})`);
  f = await fixture('F3', snap({ flags: { uncal_zero: true } }));
  check(f.b.cls.indexOf('st-ok') < 0 && f.cs.every((c) => c.bar !== 'ok'), `F3 synced + uncal_zero: ei koskaan vihreää (D11) (${f.b.cls})`);
  f = await fixture('F4', snap({ master: 1, worst: 2, state: 'out_of_sync', head: 'Ei synkronissa', spread: 3.5,
    advice: 'SYL 3: avaa läppää – imee 3,5 kPa enemmän kuin SYL 2', ch: [chan(1.0), chan(0), chan(3.5)] }));
  check(f.cs[2].act === 'AVAA' && f.cs[2].bar === 'bad' && f.cs[0].bar === 'neu' && f.cs[1].master,
    `F4 master SYL 2: säätö SYL 3 (${f.cs[2].act}/${f.cs[2].bar}), SYL 1 neutraali (${f.cs[0].bar})`);
  f = await fixture('F5', snap({ state: 'near', head: 'Lähellä', advice: 'SYL 2: avaa läppää – imee 2,0 kPa enemmän kuin SYL 1',
    spread: 2.0, worst: 1, master: 1, ch: [chan(0), chan(2.0), chan(0.5)] }));
  check(f.cs.every((c) => !c.act), 'F5 worst = master (epäjohdonmukainen): ei vihjettä masterille');
  for (const [tol, top, bot] of [[[1.6, 3.25], '+6,5', '−6,5'], [[1.1, 2.2], '+4,4', '−4,4'], [[1.5, 3.0], '+6', '−6'],
    [[1, 2.125], '+4,25', '−4,25']]) {
    f = await fixture('F6', snap({ tol }));
    check(f.cs[1].top === top && f.cs[1].bot === bot, `F6 asteikko tol_yellow ${tol[1]}: ${top}/${bot} (${f.cs[1].top}/${f.cs[1].bot})`);
  }
  f = await fixture('F7', snap({ flags: { sat: true, uncal_zero: true }, state: 'near', head: 'Lähellä – kalibroimaton',
    advice: 'Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse' }));
  const satB = await page.evaluate(() => {
    const s = document.querySelector('.badge[data-f=sat]');
    const u = document.querySelector('.badge[data-f=uncal_zero]');
    const m = document.querySelector('.cyl.m .mtag');
    return s && u && { txt: s.textContent, col: getComputedStyle(s).color, ucol: getComputedStyle(u).color,
      mcol: getComputedStyle(m).backgroundColor };
  });
  check(satB && /SATUROITUU/.test(satB.txt) && /leikkautuu \(H1\)/.test(satB.txt), `F7 SATUROITUU kertoo merkityksen (${satB && satB.txt})`);
  check(satB && satB.col === satB.ucol && satB.col !== satB.mcol, `F7 SATUROITUU varoitusvärillä, ei koristevärillä (${satB && satB.col})`);
  // Letku irti vs. ei pulssia: raja tulee asetuksesta hose_off_kpa (S5a), ei vakiosta.
  const hose = (level) => snap({ state: 'fault_hose', verdict: false, bars: false, fault_ch: 2, worst: -1,
    head: 'Letkuvika: SYL 3', advice: 'Tarkista letku', ch: [chan(0), chan(0), chan(0, { pulse: false, level, mean: null })] });
  f = await fixture('F8a', hose(-1.0));
  check(f.cs[2].fov === 'LETKU IRTI?', `F8 taso 1,0 < hose_off 2 -> LETKU IRTI? (${f.cs[2].fov})`);
  f = await fixture('F8b', hose(-3.0));
  check(f.cs[2].fov === 'EI PULSSIA', `F8 taso 3,0 > hose_off 2 -> EI PULSSIA (${f.cs[2].fov})`);
  const cfg4 = JSON.parse(JSON.stringify(cfg));
  cfg4.fields.find((x) => x.key === 'hose_off_kpa').value = 4;
  f = await fixture('F8c', hose(-3.0), cfg4);
  check(f.cs[2].fov === 'LETKU IRTI?', `F8 hose_off_kpa = 4 asetuksista -> LETKU IRTI? (${f.cs[2].fov})`);

  // Kestävyys: puuttuvat/väärän tyyppiset kentät ja 4 kanavaa eivät saa kaataa sivua.
  const weird = [{}, { ch: null, tol: 'x', rpm: null, flags: 3 },
    { state: 'near', verdict: true, bars: true, master: 0, worst: 1, tol: [1.5, 3], ch: [{}, { d: 'a' }, { d: null }] },
    { state: 'out_of_sync', verdict: true, bars: true, master: 5, worst: 3, rpm: 1e9, rpm_sd: -1, tol: [0, 0],
      ch: [{ d: 0 }, { d: 99 }, { d: -99, fault: true }, { d: 1, pulse: false }],
      wave: { fs: 0, s: [[1], [null, 2, 3], 'x', [-200, 50, NaN]] }, cal: null, diag: 'x' },
    { state: 'near', verdict: true, bars: true, worst: 1, spread: 'x', tol: null, flags: { uncal_zero: true }, ch: [{ d: 0 }, { d: 2 }] },
    { state: 'fault_hose', fault_ch: 9, head: 7, advice: null, ch: [{ pulse: false, level: 0 }], src: null }];
  const body = 'retry: 60000\n\n' + weird.map((w) => 'event: snap\ndata: ' + JSON.stringify(w) + '\n\n').join('') +
    'event: snap\ndata: {broken json\n\n';
  const pe1 = pageErrors.length;
  await page.route('**/events', (r) => r.fulfill({ status: 200, headers: { 'Content-Type': 'text/event-stream' }, body }));
  await page.goto(URL);
  await page.waitForTimeout(1200);
  await page.evaluate(() => { document.getElementById('diagbox').open = true; });
  await page.waitForTimeout(300);
  check(pageErrors.length === pe1, `virheelliset snapshotit eivät kaada sivua (${pageErrors.slice(pe1).join(' | ')})`);
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
