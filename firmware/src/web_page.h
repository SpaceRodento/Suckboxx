#pragma once
#include <Arduino.h>

// Yksittäinen itsenäinen HTML-sivu laitteen omasta Wi-Fi AP:sta ladattavaksi.
// Ei ulkoisia riippuvuuksia (fontteja, CDN-skriptejä) - AP:lla ei ole
// internet-yhteyttä, joten kaiken pitää olla mukana tässä tiedostossa.
const char WEB_PAGE_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="fi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Suckboxx</title>
<style>
  :root {
    --bg: #12161c;
    --card: #1c2430;
    --text: #e8edf3;
    --muted: #8b97a8;
    --line: #3a4556;
    --fill-pos: #3ddc97;
    --fill-neg: #f59e0b;
    --bad: #ef4444;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 16px;
    background: var(--bg);
    color: var(--text);
    font-family: -apple-system, system-ui, "Segoe UI", Roboto, sans-serif;
    -webkit-tap-highlight-color: transparent;
  }
  header { text-align: center; margin-bottom: 18px; }
  h1 { margin: 0 0 4px; font-size: 1.4rem; }
  .sub { margin: 0; color: var(--muted); font-size: 0.85rem; }
  main {
    display: flex;
    justify-content: center;
    gap: 14px;
    flex-wrap: nowrap;
  }
  .ch {
    background: var(--card);
    border-radius: 12px;
    padding: 12px 8px;
    flex: 1 1 0;
    max-width: 140px;
    text-align: center;
    position: relative;
  }
  .ch-label { font-weight: 600; margin-bottom: 10px; font-size: 1.05rem; }
  .meter {
    position: relative;
    width: 56px;
    height: 220px;
    margin: 0 auto;
    background: #0f1319;
    border-radius: 8px;
    overflow: hidden;
  }
  .meter-center {
    position: absolute;
    left: 0; right: 0; top: 50%;
    height: 2px;
    background: var(--line);
    transform: translateY(-1px);
  }
  .meter-fill {
    position: absolute;
    left: 5px; right: 5px;
    height: 0;
    background: var(--fill-pos);
    border-radius: 4px;
    transition: height 0.15s ease-out, top 0.15s ease-out, bottom 0.15s ease-out;
  }
  .delta { margin-top: 10px; font-size: 1.3rem; font-weight: 700; font-variant-numeric: tabular-nums; }
  .raw { color: var(--muted); font-size: 0.75rem; margin-top: 2px; font-variant-numeric: tabular-nums; }
  .stale-overlay {
    display: none;
    position: absolute;
    inset: 0;
    background: rgba(18, 22, 28, 0.88);
    border-radius: 12px;
    align-items: center;
    justify-content: center;
    font-size: 0.8rem;
    font-weight: 600;
    color: var(--bad);
    text-align: center;
    padding: 8px;
  }
  .ch.stale .stale-overlay { display: flex; }
  footer { margin-top: 22px; text-align: center; }
  button {
    background: var(--card);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: 8px;
    padding: 12px 22px;
    font-size: 1rem;
    -webkit-appearance: none;
  }
  button:active { background: var(--line); }
  #statusLine { margin-top: 10px; color: var(--muted); font-size: 0.8rem; }
  #statusLine.offline { color: var(--bad); }
</style>
</head>
<body>
  <header>
    <h1>Suckboxx</h1>
    <p class="sub">HX710B raakadata &middot; ei kalibroitu &middot; v&auml;liaikainen bring-up</p>
  </header>

  <main>
    <div class="ch" id="ch0">
      <div class="ch-label">SYL 1</div>
      <div class="meter">
        <div class="meter-center"></div>
        <div class="meter-fill" id="fill0"></div>
      </div>
      <div class="delta" id="delta0">+0</div>
      <div class="raw" id="raw0">raw 0</div>
      <div class="stale-overlay">EI<br>DATAA</div>
    </div>
    <div class="ch" id="ch1">
      <div class="ch-label">SYL 2</div>
      <div class="meter">
        <div class="meter-center"></div>
        <div class="meter-fill" id="fill1"></div>
      </div>
      <div class="delta" id="delta1">+0</div>
      <div class="raw" id="raw1">raw 0</div>
      <div class="stale-overlay">EI<br>DATAA</div>
    </div>
    <div class="ch" id="ch2">
      <div class="ch-label">SYL 3</div>
      <div class="meter">
        <div class="meter-center"></div>
        <div class="meter-fill" id="fill2"></div>
      </div>
      <div class="delta" id="delta2">+0</div>
      <div class="raw" id="raw2">raw 0</div>
      <div class="stale-overlay">EI<br>DATAA</div>
    </div>
  </main>

  <footer>
    <button id="zeroBtn">Nollaa kaikki</button>
    <p id="statusLine">yhdistetään&hellip;</p>
  </footer>

<script>
(function () {
  var METER_HALF_PX = 105;   // meter-height 220px, keskiviivasta reunaan
  var MIN_SPAN = 500;        // lattia asteikolle ettei kohina heiluta palkkeja alussa
  var STALE_MS = 2000;
  var maxAbsDelta = MIN_SPAN;

  var statusLine = document.getElementById('statusLine');

  function updateChannel(i, ch) {
    var chEl = document.getElementById('ch' + i);
    var fillEl = document.getElementById('fill' + i);
    var deltaEl = document.getElementById('delta' + i);
    var rawEl = document.getElementById('raw' + i);

    var stale = ch.age_ms > STALE_MS;
    chEl.classList.toggle('stale', stale);

    var pct = Math.min(1, Math.abs(ch.delta) / maxAbsDelta);
    var px = Math.round(pct * METER_HALF_PX);
    if (ch.delta >= 0) {
      fillEl.style.background = 'var(--fill-pos)';
      fillEl.style.bottom = '50%';
      fillEl.style.top = 'auto';
      fillEl.style.height = px + 'px';
    } else {
      fillEl.style.background = 'var(--fill-neg)';
      fillEl.style.top = '50%';
      fillEl.style.bottom = 'auto';
      fillEl.style.height = px + 'px';
    }

    deltaEl.textContent = (ch.delta >= 0 ? '+' : '') + ch.delta;
    rawEl.textContent = 'raw ' + ch.raw;
  }

  function poll() {
    fetch('/api/readings')
      .then(function (r) { return r.json(); })
      .then(function (data) {
        var channels = data.ch;
        for (var i = 0; i < channels.length; i++) {
          maxAbsDelta = Math.max(maxAbsDelta, Math.abs(channels[i].delta));
        }
        for (var i = 0; i < channels.length; i++) {
          updateChannel(i, channels[i]);
        }
        statusLine.textContent = 'yhteys OK · uptime ' + Math.round(data.uptime_ms / 1000) + ' s';
        statusLine.classList.remove('offline');
      })
      .catch(function () {
        statusLine.textContent = 'ei yhteyttä laitteeseen';
        statusLine.classList.add('offline');
      });
  }

  document.getElementById('zeroBtn').addEventListener('click', function () {
    fetch('/api/zero', { method: 'POST' }).then(function () {
      maxAbsDelta = MIN_SPAN;
    });
  });

  setInterval(poll, 300);
  poll();
})();
</script>
</body>
</html>
)HTMLPAGE";
