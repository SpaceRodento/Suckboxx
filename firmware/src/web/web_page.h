#pragma once
// GENEROITU TIEDOSTO - ÄLÄ MUOKKAA KÄSIN.
// Lähde: web/index.html, generaattori: tools/embed_web.py
//   python3 tools/embed_web.py          # päivitä
//   python3 tools/embed_web.py --check  # onko ajan tasalla
//
// Koko web-UI yhtenä minimoituna merkkijonona flashissa (docs/firmware.md §5).
// Laitteen AP:lla ei ole internetiä, joten sivu ei lataa mitään ulkoista.
// Lähde 54007 tavua, upotettu 37896 tavua, sha256 7599b986fffda7ac.
//
// inline constexpr (C++17): yksi kopio flashissa, vaikka header olisi
// mukana useammassa käännösyksikössä (pelkkä const olisi sisäisesti
// linkitetty -> 40 kt kopio jokaiseen .cpp:hen).

#ifdef ARDUINO
#include <Arduino.h>
#else
// PC-käännös (native-testit): PROGMEM ei merkitse mitään.
#ifndef PROGMEM
#define PROGMEM
#endif
#endif

inline constexpr char WEB_PAGE_HTML[] PROGMEM = R"SBXHTML(<!DOCTYPE html>
<html lang="fi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#160a2b">
<title>Suckboxx</title>
<style>:root{--bg:#160a2b;--bg2:#221043;--bg3:#2e1759;--line:#46297e;--tx:#f8f3ff;--mu:#c9b8ea;--mag:#ff3fd2;--cy:#31e7ff;--ok:#2ee87a;--okt:#03200e;--nr:#ffb21e;--nrt:#2b1700;--nrx:#ffb21e;--bd:#d81e3a;--bdb:#ff4d5e;--id:#554c7c;--c0:#31e7ff;--c1:#ff5ce1;--c2:#f2ecff;--grid:rgba(255,255,255,.13);--glow:#3e1675}html.sun{--bg:#fff;--bg2:#f4f1fb;--bg3:#e4ddf4;--line:#a99cc9;--tx:#0b0514;--mu:#3a3050;--mag:#a1007f;--cy:#0072a3;--nrx:#8a4f00;--bdb:#c4102c;--c0:#0072a3;--c1:#b0008a;--c2:#2c1d52;--grid:rgba(0,0,0,.16);--glow:#fff}*{box-sizing:border-box}[hidden]{display:none !important}html{background:var(--bg) radial-gradient(130% 380px at 50% -60px,var(--glow) 0,transparent 75%) no-repeat;-webkit-text-size-adjust:100%;color:var(--tx);font:16px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}body{margin:0 auto;max-width:980px;padding:0 12px calc(20px + env(safe-area-inset-bottom));font-variant-numeric:tabular-nums;-webkit-tap-highlight-color:transparent}button,input,select{font:inherit;color:inherit}:focus-visible{outline:3px solid var(--cy);outline-offset:2px}.top{display:flex;flex-wrap:wrap;align-items:center;gap:6px 8px;padding:8px 0}.brand{flex:1;min-width:0}.wm{font-weight:900;font-style:italic;font-size:1.7rem;letter-spacing:.05em;line-height:1;background:linear-gradient(90deg,#ff4d4d,#ff9f1c,#ffe23b,#3bff8a,#31e7ff,#8f6bff,#ff3fd2);-webkit-background-clip:text;background-clip:text;color:transparent;filter:drop-shadow(0 0 6px rgba(255,63,210,.5))}.tag{display:block;font-size:.68rem;color:var(--mu);letter-spacing:.14em;text-transform:uppercase}.conn{display:flex;align-items:center;gap:6px;font-size:.82rem;font-weight:700;color:var(--mu);text-align:right}.conn i{flex:none;width:12px;height:12px;border-radius:50%;background:var(--nr)}.conn.on i{background:var(--ok)}.conn.off i{background:var(--bdb)}.ib{width:44px;height:44px;border-radius:10px;border:1px solid var(--line);background:var(--bg2);font-size:1.2rem;cursor:pointer}.tabs{order:5;flex:1 0 100%;display:flex;gap:6px}.tabs a{flex:1;min-height:44px;display:flex;align-items:center;justify-content:center;border-radius:10px;background:var(--bg2);border:1px solid var(--line);color:var(--tx);text-decoration:none;font-weight:800;letter-spacing:.03em}.tabs a.on{border-color:var(--mag);box-shadow:inset 0 -4px 0 var(--mag)}.simbar{margin:0 0 8px;padding:9px 10px;border-radius:10px;border:2px solid var(--mag);font-weight:900;letter-spacing:.05em;text-align:center;color:#fff;text-shadow:0 1px 2px #000;background:repeating-linear-gradient(135deg,#a0007c 0 12px,#55003f 12px 24px)}.offline{position:sticky;top:0;z-index:5;margin:0 0 8px;padding:10px;border-radius:10px;background:#000;color:#fff;border:3px solid var(--bdb);font-weight:900;text-align:center;font-size:1.1rem}html.stale .data{filter:grayscale(1);opacity:.35}.badges{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px}.badges:empty{display:none}.badge{padding:5px 10px;border-radius:999px;font-size:.8rem;font-weight:900;letter-spacing:.05em;border:2px solid var(--nrx);color:var(--nrx)}.badge small{font-weight:700;letter-spacing:0}.banner{border-radius:14px;padding:12px 14px;margin-bottom:10px;min-height:100px}.head{font-size:clamp(1.35rem,6.4vw,2rem);font-weight:900;line-height:1.15}.advice{font-size:clamp(1.02rem,4.5vw,1.3rem);font-weight:700;margin-top:5px}.st-ok{background:var(--ok);color:var(--okt)}.st-near{background:var(--nr);color:var(--nrt)}.st-bad{background:var(--bd);color:#fff}.st-idle{background:var(--id);color:#fff}.st-fault{background:repeating-linear-gradient(135deg,var(--bd) 0 16px,#7d0c20 16px 32px);color:#fff;text-shadow:0 1px 3px #000}.rpmbox{display:flex;align-items:flex-end;justify-content:space-between;gap:8px;padding:6px 12px 8px;margin-bottom:10px;border-radius:14px;background:var(--bg2);border:2px solid var(--line)}.rpm{font-size:clamp(3rem,16vw,4.4rem);font-weight:900;line-height:1}.rpmbox .u{font-weight:800;color:var(--mu);margin-left:6px}.rpmsd{font-size:1.35rem;font-weight:800;text-align:right;white-space:nowrap}.rpmsd small{display:block;color:var(--mu);font-size:.82rem;font-weight:700}.rpmbox.dim .rpm,.rpmbox.dim .rpmsd{color:var(--mu)}.rpmbox.drift{border-color:var(--nr);animation:pulse 1.2s ease-in-out infinite alternate}@keyframes pulse{to{box-shadow:0 0 0 4px rgba(255,178,30,.35)}}.cyls{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.cyl{position:relative;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;background:var(--bg2);border:2px solid var(--line);border-radius:14px;padding:8px 4px 10px;text-align:center;min-width:0;cursor:pointer}.cyl.m{border-color:var(--cy)}.cyl.w{border-color:var(--tx)}.cyl.arm{outline:3px dashed var(--mag);outline-offset:2px}.cyl-h{font-weight:900;font-size:1.05rem;letter-spacing:.04em;white-space:nowrap}.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px;vertical-align:1px}.meter{position:relative;height:clamp(170px,34vh,300px);margin:6px auto;width:74%;flex:none;max-width:86px;border-radius:8px;background:var(--bg);border:1px solid var(--line);overflow:hidden}.band,.bar,.zl{position:absolute;left:0;right:0}.band.y{background:rgba(255,178,30,.14);border:solid var(--nr);border-width:2px 0}.band.g{background:rgba(46,232,122,.3);border:solid var(--ok);border-width:2px 0}.zl{top:50%;height:2px;margin-top:-1px;background:var(--tx)}.bar{left:20%;right:20%;border-radius:3px;display:none}.bar.ok{background:var(--ok)}.bar.near{background:var(--nr)}.bar.bad{background:var(--bdb)}.bar.neu{background:var(--mu)}.sc{position:absolute;left:3px;font-size:.66rem;color:var(--mu);z-index:1}.sc.t{top:1px}.sc.b{bottom:1px}.clip{position:absolute;left:0;right:0;text-align:center;font-size:.95rem;line-height:1.1;color:var(--tx);display:none;z-index:2}.clip.up{top:0}.clip.dn{bottom:0}.clip.on{display:block}.mtag{display:none;position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);background:var(--cy);color:#04141a;font-size:.68rem;font-weight:900;padding:2px 4px;border-radius:4px;letter-spacing:.04em;z-index:2}.cyl.m .mtag{display:block}.fov{display:none;position:absolute;top:0;left:0;right:0;bottom:0;z-index:3;align-items:center;justify-content:center;color:#fff;text-shadow:0 1px 2px #000;font-weight:900;font-size:.95rem;letter-spacing:.1em;-webkit-writing-mode:vertical-rl;writing-mode:vertical-rl;background:repeating-linear-gradient(135deg,var(--bd) 0 10px,#7d0c20 10px 20px)}.fov.on{display:flex}.fov.grey{background:var(--id)}.dv{font-size:clamp(1.5rem,7.6vw,2.2rem);font-weight:900;line-height:1.05;white-space:nowrap}.dv small{font-size:.42em;color:var(--mu);margin-left:2px}.dh{font-size:.8rem;color:var(--mu);font-weight:700;min-height:1.1em}.act{visibility:hidden;margin-top:6px;padding:4px 8px;border-radius:8px;font-weight:900;letter-spacing:.08em;background:var(--tx);color:var(--bg)}.act.on{visibility:visible}.mhint{margin:8px 2px 0;font-size:.85rem;color:var(--mu);text-align:center}.mhint.hot{color:var(--nrt);background:var(--nr);border-radius:8px;padding:8px;font-weight:900}.card{background:var(--bg2);border:1px solid var(--line);border-radius:14px;padding:10px 12px;margin-top:10px}.card h2{margin:2px 0 8px;font-size:1.15rem;color:var(--mag);letter-spacing:.03em}.card h3{margin:14px 0 4px;font-size:1rem}.row{display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap}#wave{display:block;width:100%;height:170px;margin-top:6px}.leg{font-size:.85rem;font-weight:800}.leg span{margin-right:12px;white-space:nowrap}summary{min-height:44px;display:flex;align-items:center;font-weight:800;cursor:pointer}table{width:100%;border-collapse:collapse;font-size:.88rem}td,th{padding:4px 3px;text-align:right;border-top:1px solid var(--line)}th{color:var(--mu);font-weight:700}td:first-child,th:first-child{text-align:left;color:var(--mu)}.btn{display:inline-flex;align-items:center;justify-content:center;min-height:48px;padding:0 16px;border-radius:12px;border:2px solid var(--mag);background:linear-gradient(180deg,#ff3fd2,#a8108f);color:#fff;font-weight:800;text-decoration:none;cursor:pointer;margin:4px 6px 4px 0}.btn.gh{background:transparent;color:var(--tx);border-color:var(--line)}.btn.sm{min-height:44px;padding:0 12px;font-size:.9rem}.btn:disabled{opacity:.4;cursor:default}.note{color:var(--mu);font-size:.9rem;margin:4px 0 8px}ol.note{padding-left:20px}.msg{font-weight:800;margin:6px 0;min-height:1.2em}.msg.ok{color:var(--ok)}.msg.er{color:var(--bdb)}html.sun .msg.ok{color:#08702f}.pill{display:inline-block;margin:0 6px 6px 0;padding:6px 10px;border-radius:999px;font-weight:800;font-size:.88rem;border:2px solid var(--line)}.pill.y{border-color:var(--ok)}.pill.n{border-color:var(--nr)}.busy{font-weight:800;color:var(--cy);margin:6px 0}.busy i{display:inline-block;width:16px;height:16px;margin-right:8px;vertical-align:-3px;border:3px solid var(--cy);border-top-color:transparent;border-radius:50%;animation:spin .9s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}.frow,.srow{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:2px 8px;padding:8px 0;border-top:1px solid var(--line)}.frow label,.srow label{font-weight:700}.frow label small{display:block;color:var(--mu);font-weight:400;font-size:.74rem}.fin{display:flex;align-items:center;gap:5px}.fin input[type=text],.fin select,.srow select{width:7.2em;min-height:44px;font-size:1.05rem;padding:0 8px;border-radius:8px;border:2px solid var(--line);background:var(--bg);color:var(--tx);text-align:right}.fin select,.srow select{text-align:left;width:auto;max-width:11em}.unit{min-width:3em;font-size:.82rem;color:var(--mu)}.fmeta,.ferr{grid-column:1 / -1;font-size:.78rem;color:var(--mu)}.ferr{color:var(--bdb);font-weight:800;font-size:.85rem}.ferr:empty{display:none}.frow.chg input,.frow.chg select{border-color:var(--cy)}.frow.bad input{border-color:var(--bdb)}.sw{-webkit-appearance:none;appearance:none;width:76px;height:44px;border-radius:22px;background:var(--bg3);border:2px solid var(--line);position:relative;margin:0;cursor:pointer;flex:none}.sw::after{content:"";position:absolute;top:4px;left:4px;width:32px;height:32px;border-radius:50%;background:var(--mu);transition:left .15s}.sw:checked{background:var(--cy);border-color:var(--cy)}.sw:checked::after{left:36px;background:#fff}.srow input[type=range]{grid-column:1 / -1;width:100%;height:44px;margin:0;accent-color:var(--mag)}.srow output{font-weight:800;white-space:nowrap}.errbox,.warn{border:2px solid var(--bdb);border-radius:10px;padding:8px 10px;color:var(--bdb);font-weight:800;margin:8px 0}.warn{border-color:var(--nr);color:inherit}.toast{position:fixed;left:50%;bottom:calc(16px + env(safe-area-inset-bottom));transform:translateX(-50%);width:max-content;max-width:92vw;z-index:10;padding:10px 14px;border-radius:12px;background:#000;color:#fff;border:2px solid var(--mag);font-weight:800;box-shadow:0 6px 24px rgba(0,0,0,.5)}@media (min-width:640px){#pMain{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.1fr);gap:0 12px;align-items:start}.tabs{order:0;flex:1 1 auto;max-width:360px}.simbar{padding:5px}#pMain > .lcol{grid-column:1}#pMain > .rcol{grid-column:2;grid-row:1 / span 2}#pMain > .full{grid-column:1 / -1}.meter{height:clamp(160px,46vh,330px)}}@media (orientation:landscape) and (max-height:520px){.meter{height:max(110px,calc(100vh - 300px))}.top{padding:4px 0}}@media (prefers-reduced-motion:reduce){*{animation:none !important;transition:none !important}}</style>
</head>
<body>
<header class="top">
<div class="brand"><span class="wm">SUCKBOXX</span><span class="tag">Gay Carb Suck Box</span></div>
<div id="conn" class="conn" role="status"><i></i><span>Yhdistetään…</span></div>
<button id="sun" class="ib" type="button" aria-label="Päivänvalonäkymä">&#9728;</button>
<nav class="tabs"><a href="#" id="tMain" class="on">Mittaus</a><a href="#huolto" id="tSvc">Huolto</a></nav>
</header>
<div id="offline" class="offline" role="alert" hidden>Ei yhteyttä laitteeseen</div>
<div id="simbar" class="simbar" hidden>&#9888; SIMULAATTORI – ei oikeaa dataa</div>
<main id="pMain">
<div class="lcol data">
<div id="badges" class="badges" aria-live="polite"></div>
<section id="banner" class="banner st-idle" aria-live="polite">
<div id="head" class="head">Odotetaan dataa…</div>
<div id="advice" class="advice"></div>
</section>
<section id="rpmbox" class="rpmbox dim">
<div><span id="rpm" class="rpm">—</span><span class="u">rpm</span></div>
<div class="rpmsd"><span id="rpmsd">± —</span><small id="rpmcv">hajonta</small></div>
</section>
</div>
<div class="rcol data">
<section id="cyls" class="cyls" aria-label="Sylinterit: alipaine-ero masteriin"></section>
<p id="mhint" class="mhint"></p>
</div>
<section class="card full data">
<div class="row">
<b>Aaltomuoto (3 s)</b>
<button id="wscale" type="button" class="btn gh sm">Asteikko: auto</button>
</div>
<canvas id="wave" aria-label="Kanavien painekäyrät"></canvas>
<div id="wleg" class="leg"></div>
</section>
<details class="card full" id="diagbox"><summary>Diagnostiikka</summary><div id="diag"></div></details>
</main>
<main id="pSvc" hidden>
<section class="card">
<h2>Kalibrointi</h2>
<div><span id="pZ" class="pill">Nollaus: –</span><span id="pG" class="pill">Vahvistus: –</span></div>
<p id="cMsg" class="msg"></p>
<h3>1. Nollaus</h3>
<p class="note">Moottori seis, letkut saavat olla kiinni – sammuneen moottorin imusarja on ilmanpaineessa.
Laite nollaa myös itse, kun moottori on ollut sammuksissa muutaman sekunnin.
Nollaus on tehtävä joka käynnistyksen jälkeen, muuten vihreää ei näytetä.</p>
<button id="bZero" type="button" class="btn">Nollaa</button>
<p id="zMsg" class="msg"></p>
<h3>2. Vahvistus (T-haara)</h3>
<ol class="note">
<li>Kytke kaikki kolme letkua samaan T-haaraan / jakotukkiin ja käsipumppu siihen.</li>
<li>Pumppaa 40–60 kPa alipaine ja odota, että lukema asettuu.</li>
<li>Paina <b>Kalibroi vahvistus</b> ja pidä paine tasaisena 2 s.</li>
</ol>
<button id="bGain" type="button" class="btn">Kalibroi vahvistus</button>
<div id="gBusy" class="busy" hidden><i></i>Mitataan… pidä alipaine tasaisena</div>
<button id="bGRst" type="button" class="btn gh">Poista vahvistuskalibrointi</button>
</section>
<section class="card">
<h2>Raakadata</h2>
<p class="note">Viimeiset ~8 s kaikista kanavista (1 kHz, mV ja kPa).</p>
<a class="btn" href="/api/raw.csv" download="suckboxx_raw.csv">Tallenna raakadata (CSV)</a>
</section>
<section class="card">
<h2>Asetukset</h2>
<p class="note">Muutos tulee voimaan heti tallennettaessa. Desimaalierotin pilkku tai piste.</p>
<form id="cfg" autocomplete="off"><p class="note">Ladataan…</p></form>
<div id="cErr" class="errbox" hidden></div>
<p id="cOk" class="msg ok"></p>
<button id="bSave" type="button" class="btn" disabled>Tallenna</button>
<button id="bUndo" type="button" class="btn gh" disabled>Peru muutokset</button>
<button id="bDef" type="button" class="btn gh">Palauta oletukset</button>
</section>
<section class="card" id="simCard">
<h2>Simulaattori</h2>
<div id="simWarn" class="warn" hidden></div>
<p class="note">Tila nyt: <b id="simState">–</b> · virhe + = vähemmän alipainetta</p>
<div id="pre" class="row" style="justify-content:flex-start"></div>
<div id="sim"></div>
</section>
</main>
<div id="toast" class="toast" role="status" hidden></div>
<script>
(function(){'use strict';var $=function(id){return document.getElementById(id);};var doc=document.documentElement;var S=null;var lastMsg=0;var es=null;var cards=[];var armed=-1;var armT=0;var wave=[];var waveFs=62.5;var waveFixed=false;var raf=0;var colors=[];var CLS={synced:'ok',near:'near',out_of_sync:'bad',no_signal:'idle',unstable:'idle',rpm_too_high:'idle',fault_sensor:'fault',fault_hose:'fault',fault_master:'fault'};function isNum(v){return typeof v==='number'&&isFinite(v);}
function obj(v){return v&&typeof v==='object'?v:{};}
function arr(v){return Array.isArray(v)?v:[];}
function fx(v,d){if(!isNum(v))return'—';var s=v.toFixed(d);if (/^-0(\.0+)?$/.test(s)) s = s.slice(1);return s.replace('.',',').replace('-','−');}
function sg(v,d){var s=fx(v,d);return isNum(v)&&s.charAt(0)!=='−'&&s!==fx(0,d)?'+'+s:s;}
function decs(v){for(var d=0;d<2;d++){var k=Math.pow(10,d);if(Math.abs(v*k-Math.round(v*k))<1e-6*k)return d;}
return 2;}
function txt(el,s){if(el.textContent!==s)el.textContent=s;}
function tog(el,c,on){el.classList.toggle(c,!!on);}
function esc(s){return String(s).replace(/[&<>"]/g, function (c) {return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
var toastT=0;function toast(msg,ms){var t=$('toast');t.textContent=msg;t.hidden=false;clearTimeout(toastT);toastT=setTimeout(function(){t.hidden=true;},ms||3000);}
function form(d){return Object.keys(d).map(function(k){return encodeURIComponent(k)+'='+encodeURIComponent(d[k]);}).join('&');}
function api(path,data){var o={method:data?'POST':'GET',cache:'no-store'};if(data){o.body=form(data);o.headers = { 'Content-Type': 'application/x-www-form-urlencoded' };}
return fetch(path,o).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();});}
function fail(e){toast('Virhe: '+(e&&e.message?e.message:'ei yhteyttä'),4000);}
function connect(){if(es)es.close();es = new EventSource('/events');es.addEventListener('snap',function(e){var s;try{s=JSON.parse(e.data);}catch(x){return;}
if(!s||typeof s!=='object')return;if(lastMsg&&Date.now()-lastMsg>2000)onReconnect();lastMsg=Date.now();onSnap(s);});es.onerror=function(){setConn('off');if(es.readyState===2)setTimeout(connect,2000);};}
function setConn(m){var c=$('conn');c.className='conn '+m;txt(c.lastChild,m==='on'?'Yhteys':m==='off'?'Ei yhteyttä':'Yhdistetään…');}
var t0=Date.now();setInterval(function(){var age=Date.now()-lastMsg;var stale=!lastMsg||age>2000;var lost=stale&&(lastMsg||Date.now()-t0>5000);tog(doc,'stale',lost);$('offline').hidden=!lost;setConn(stale?(lost?'off':'wait'):'on');if(lastMsg&&age>8000&&es&&es.readyState===1){lastMsg=Date.now()-2500;connect();}},500);var hoseOff=2;var cfgVals=null;var cfgAsk=0;function cfgFields(j){return arr(obj(j).fields).filter(function(f){return f&&typeof f.key==='string';});}
function useCfg(list){cfgVals={};list.forEach(function(f){cfgVals[f.key]=f.value;});if(isNum(cfgVals.hose_off_kpa))hoseOff=cfgVals.hose_off_kpa;if(S)renderCyls(S);}
function onReconnect(){cfgVals=null;cfgAsk=0;if(!$('pSvc').hidden&&$('bUndo').disabled)loadCfg(0);}
function wantCfg(){if(cfgVals||Date.now()-cfgAsk<5000)return;cfgAsk=Date.now();api('/api/config').then(function (j) { useCfg(cfgFields(j)); }).catch(function () {});}
function onSnap(s){S=s;wantCfg();var fl=obj(s.flags);var st=String(s.state||'');$('simbar').hidden=s.src!=='sim';renderBadges(fl);renderBanner(s,fl,st);renderRpm(s,fl,st);renderCyls(s);addWave(obj(s.wave));if($('diagbox').open)renderDiag(s);renderCal(s);txt($('simState'),String(s.head||st));var w=$('simWarn');w.hidden=s.src==='sim';if(s.src!=='sim'){txt(w,'Lähde on nyt "'+String(s.src||'?')+'" – simulaattorin asetukset eivät vaikuta mittaukseen.');}}
var BADGES=[
['uncal_zero','KALIBROIMATON',''],['uncal_gain','VAHVISTUS KALIBROIMATTA',''],['sat','SATUROITUU','positiivinen paine leikkautuu (H1)'],['drift','KIERROKSET MUUTTUVAT','']];function renderBadges(fl){var h='';BADGES.forEach(function(b){if(!fl[b[0]])return;h+='<span class="badge" data-f="'+b[0]+'">'+b[1]+
(b[2] ? ' <small>' + b[2] + '</small>' : '') + '</span>';});if($('badges').innerHTML!==h)$('badges').innerHTML=h;}
function renderBanner(s,fl,st){var c=CLS[st]||'idle';if(s.verdict!==true&&(c==='ok'||c==='near'||c==='bad'))c='idle';if(c==='ok'&&fl.uncal_zero)c='near';$('banner').className='banner st-'+c;$('banner').setAttribute('data-state',st);txt($('head'),String(s.head||st||'—'));txt($('advice'),String(s.advice||''));}
function renderRpm(s,fl,st){txt($('rpm'),isNum(s.rpm)?String(Math.round(s.rpm)):'—');txt($('rpmsd'),isNum(s.rpm_sd)?'± '+Math.round(s.rpm_sd):'± —');txt($('rpmcv'),isNum(s.rpm_cv)?'hajonta '+fx(s.rpm_cv*100,1)+' %':'hajonta');tog($('rpmbox'),'dim',!isNum(s.rpm)||st==='unstable'||st==='no_signal');tog($('rpmbox'),'drift',fl.drift);}
function buildCyls(n){var h='';for(var i=0;i<n;i++){h+='<button type="button" class="cyl" data-i="'+i+'">'+
'<div class="cyl-h"><span class="dot" style="background:var(--c' + (i % 3) + ')"></span>' +
'SYL ' + (i + 1) + '</div>' +
'<div class="meter"><span class="sc t"></span><span class="sc b"></span>' +
'<div class="band y"></div><div class="band g"></div><div class="zl"></div><div class="bar"></div>' +
'<span class="clip up">&#9650;</span><span class="clip dn">&#9660;</span>' +
'<span class="mtag">MASTER</span><div class="fov"></div></div>' +
'<div class="dv">—</div><div class="dh"></div><div class="act">AVAA</div></button>';}
var box=$('cyls');box.innerHTML=h;cards=[];var q=box.querySelectorAll('.cyl');for(var k=0;k<q.length;k++){var e=q[k];cards.push({el:e,bar:e.querySelector('.bar'),y:e.querySelector('.band.y'),g:e.querySelector('.band.g'),up:e.querySelector('.clip.up'),dn:e.querySelector('.clip.dn'),t:e.querySelector('.sc.t'),b:e.querySelector('.sc.b'),fov:e.querySelector('.fov'),dv:e.querySelector('.dv'),dh:e.querySelector('.dh'),act:e.querySelector('.act')});e.addEventListener('click',tapCyl.bind(null,k));}
$('wleg').innerHTML=Array.apply(null,Array(n)).map(function(x,i){return '<span style="color:var(--c' + (i % 3) + ')">&#9473; SYL ' + (i + 1) + '</span>';}).join('');}
function adjCh(s,n){var st=s.state;var w=s.worst;if(s.verdict!==true||(st!=='near'&&st!=='out_of_sync'))return-1;if(!isNum(w)||w<0||w>=n||w===s.master)return-1;var tg=arr(s.tol)[0];if(!isNum(tg))tg=1.5;if(obj(s.flags).uncal_zero&&!(isNum(s.spread)&&s.spread>tg))return-1;return w;}
function barCls(s,i,adj){if(s.state==='synced'&&s.verdict===true&&!obj(s.flags).uncal_zero)return'ok';if(i===adj)return s.state==='near'?'near':'bad';return'neu';}
function overlay(s,c,i,anyPulse){var st=s.state;var faultSt=st==='fault_hose'||st==='fault_master'||st==='fault_sensor';if(c.fault===true)return'ANTURIVIKA';if(faultSt&&c.pulse!==true&&anyPulse){return isNum(c.level)&&Math.abs(c.level)<hoseOff?'LETKU IRTI?':'EI PULSSIA';}
if(faultSt&&i===s.fault_ch)return'VIKA';return'';}
function renderCyls(s){var ch=arr(s.ch);var tol=arr(s.tol);if(ch.length!==cards.length)buildCyls(ch.length);var tg=isNum(tol[0])&&tol[0]>0?tol[0]:1.5;var ty=isNum(tol[1])&&tol[1]>tg?tol[1]:2*tg;var sc=2*ty;var nd=decs(sc);var st=String(s.state||'');var bars=s.bars===true;var adj=adjCh(s,cards.length);var anyPulse=ch.some(function(x){return x&&x.pulse===true;});var y = function (v) { return 50 - v / sc * 50; };for(var i=0;i<cards.length;i++){var k=cards[i];var c=obj(ch[i]);var d=c.d;var m=i===s.master;tog(k.el,'m',m);tog(k.el,'w',i===adj);tog(k.el,'arm',armed===i&&Date.now()-armT<4000);k.y.style.top=y(ty)+'%';k.y.style.height=(y(-ty)-y(ty))+'%';k.g.style.top=y(tg)+'%';k.g.style.height=(y(-tg)-y(tg))+'%';txt(k.t,sg(sc,nd));txt(k.b,fx(-sc,nd));var show=bars&&isNum(d)&&!m;k.bar.style.display=show?'block':'none';if(show){var dc=Math.max(-sc,Math.min(sc,d));k.bar.style.top=(dc>=0?y(dc):50)+'%';k.bar.style.height = Math.max(Math.abs(dc) / sc * 50, 0.6) + '%';k.bar.className='bar '+barCls(s,i,adj);}
tog(k.up,'on',show&&d>sc);tog(k.dn,'on',show&&d<-sc);var fv=overlay(s,c,i,anyPulse);var grey=false;if(!fv&&st==='unstable'&&c.pulse===false&&anyPulse){fv='EI PULSSIA';grey=true;}
txt(k.fov,fv);tog(k.fov,'on',fv);tog(k.fov,'grey',grey);var dv='—';var dh='';if(bars&&m){dv='0,0';dh='viite';}else if(bars&&isNum(d)){dv=sg(d,1);dh=Math.abs(d)<0.05?'tasan':d>0?'imee enemmän':'imee vähemmän';}else if(c.pulse===false&&!fv){dh='ei pulssia';}
if(dv!=='—'){var html = dv + '<small>kPa</small>';if(k.dv.innerHTML!==html)k.dv.innerHTML=html;}else{txt(k.dv,dv);}
txt(k.dh,dh);var act=i===adj&&isNum(d)?(d>0?'AVAA':'SULJE'):'';txt(k.act,act||'AVAA');tog(k.act,'on',act);}
var mh=$('mhint');tog(mh,'hot',st==='fault_master');txt(mh,st==='fault_master'
?'Master ei kelpaa – napauta toista sylinteriä kahdesti valitaksesi uuden masterin'
:'Napauta sylinteriä vaihtaaksesi masterin · palkki ylös = imee enemmän');}
function tapCyl(i){if(!S)return;if(i===S.master){toast('SYL '+(i+1)+' on jo master');return;}
if(armed===i&&Date.now()-armT<4000){armed=-1;api('/api/master', { ch: i }).then(function (r) {toast(r&&r.ok?'Master: SYL '+(i+1):'Ei onnistunut: '+((r&&(r.err||r.msg))||'?'));}).catch(fail);}else{armed=i;armT=Date.now();toast('Napauta SYL '+(i+1)+' uudelleen: vaihda masteriksi',4000);}
renderCyls(S);}
function addWave(w){if(isNum(w.fs)&&w.fs>0)waveFs=w.fs;var rows=arr(w.s);var max=Math.max(16,Math.round(waveFs*3));for(var i=0;i<rows.length;i++){if(Array.isArray(rows[i]))wave.push(rows[i]);}
if(wave.length>max)wave.splice(0,wave.length-max);if(!raf&&!document.hidden&&!$('pMain').hidden)raf=requestAnimationFrame(drawWave);}
function readColors(){var cs=getComputedStyle(doc);colors=['--c0','--c1','--c2','--grid','--mu','--tx'].map(function(v){return cs.getPropertyValue(v).trim();});}
function drawWave(){raf=0;var cv=$('wave');var dpr=window.devicePixelRatio||1;var w=cv.clientWidth;var h=cv.clientHeight;if(!w||!h)return;if(cv.width!==Math.round(w*dpr)||cv.height!==Math.round(h*dpr)){cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);}
var g=cv.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,w,h);if(!colors.length)readColors();var lo=Infinity;var hi=-Infinity;var i,j;for(i=0;i<wave.length;i++){for(j=0;j<wave[i].length;j++){var v=wave[i][j];if(isNum(v)){if(v<lo)lo=v;if(v>hi)hi=v;}}}
if(waveFixed||lo>hi){lo=-100;hi=10;}else{var mid = (lo + hi) / 2;var span=Math.max(10,(hi-lo)*1.15);lo = Math.max(-105, mid - span / 2);hi = Math.min(15, mid + span / 2);}
var L=34;var R=4;var T=6;var B=6;var step=hi-lo>60?20:hi-lo>25?10:5;var Y = function (val) { return T + (hi - val) / (hi - lo) * (h - T - B); };g.font='11px system-ui,sans-serif';g.textAlign='right';g.textBaseline='middle';g.lineWidth=1;for (var gv = Math.ceil(lo / step) * step; gv <= hi; gv += step) {g.strokeStyle=colors[3];g.beginPath();g.moveTo(L,Y(gv));g.lineTo(w-R,Y(gv));g.stroke();g.fillStyle=colors[4];g.fillText(fx(gv,0),L-4,Y(gv));}
var n=Math.max(16,Math.round(waveFs*3));var dx = (w - L - R) / (n - 1);var off=n-wave.length;var nc=wave.length?wave[wave.length-1].length:0;g.lineWidth=2;g.lineJoin='round';for(j=0;j<nc;j++){g.strokeStyle=colors[j%3];g.beginPath();var pen=false;for(i=0;i<wave.length;i++){var val=wave[i][j];if(!isNum(val)){pen=false;continue;}
var px=L+(off+i)*dx;var py=Y(Math.max(lo,Math.min(hi,val)));if(pen)g.lineTo(px,py);else g.moveTo(px,py);pen=true;}
g.stroke();}}
$('wscale').addEventListener('click',function(){waveFixed=!waveFixed;txt(this,waveFixed?'Asteikko: −100…+10':'Asteikko: auto');drawWave();});window.addEventListener('resize',function(){if(!raf)raf=requestAnimationFrame(drawWave);});function renderDiag(s){var ch=arr(s.ch);var dg=obj(s.diag);var rows=[['Keskiarvo kPa','mean',2],['Taso kPa','level',2],['Minimi kPa','min',1],['Huippu–huippu','p2p',1],['rpm','rpm',0],['mV','mv',0]];var h = '<table><tr><th></th>' + ch.map(function (x, i) {return '<th>SYL ' + (i + 1) + '</th>';}).join('') + '</tr>';rows.forEach(function(r){h += '<tr><td>' + r[0] + '</td>' + ch.map(function (c) {return '<td>' + fx(obj(c)[r[1]], r[2]) + '</td>';}).join('') + '</tr>';});h += '<tr><td>Tila</td>' + ch.map(function (c) {c=obj(c);return '<td>' + (c.fault ? 'VIKA' : c.pulse ? 'pulssi' : 'ei pulssia') + (c.sat ? ' · sat' : '') + '</td>';}).join('') + '</tr></table><table>';var kv=[
['Näytetaajuus',fx(dg.fs,1)+' Hz'],['Pudonneet (aukot)',fx(dg.gaps,0)],['Vapaa muisti', isNum(dg.heap) ? Math.round(dg.heap / 1024) + ' kt' : '—'],['Rengas jäädytetty',dg.frozen?'kyllä':'ei'],['Ero (spread)',fx(s.spread,2)+' kPa'],['Tila',esc(s.state||'—')],['Laitteen aika', isNum(s.t) ? fx(s.t / 1000, 1) + ' s' : '—']];kv.forEach(function (r) { h += '<tr><td>' + r[0] + '</td><td>' + r[1] + '</td></tr>'; });$('diag').innerHTML = h + '</table>';}
$('diagbox').addEventListener('toggle',function(){if(this.open&&S)renderDiag(S);});function renderCal(s){var c=obj(s.cal);var z=$('pZ');var g=$('pG');txt(z,'Nollaus: '+(c.zero?'tehty ✓':'puuttuu'));z.className='pill '+(c.zero?'y':'n');txt(g,'Vahvistus: '+(c.gain?'tehty ✓':'puuttuu'));g.className='pill '+(c.gain?'y':'n');$('gBusy').hidden=c.busy!==true;$('bGain').disabled=c.busy===true;if(typeof c.msg==='string'&&!c.busy){txt($('cMsg'),c.msg);$('cMsg').className = 'msg ' + (/onnistui|tehty/i.test(c.msg) ? 'ok' : /poistettu/.test(c.msg) ? '' : 'er');}}
function calCall(path,el){return api(path,{}).then(function(r){if(el){txt(el,(r&&r.msg)||'');el.className='msg '+(r&&r.ok?'ok':'er');}}).catch(fail);}
$('bZero').addEventListener('click', function () { calCall('/api/cal/zero', $('zMsg')); });$('bGain').addEventListener('click', function () { calCall('/api/cal/gain', null); });$('bGRst').addEventListener('click',function(){if(confirm('Poistetaanko vahvistuskalibrointi? Kanavien herkkyyserot palaavat näkyviin.')){calCall('/api/cal/reset', null);}});var fields=[];var cfgGen=0;var SRC=['Simulaattori','ADC (XGZP)','HX710B'];function shown(v){return typeof v==='boolean'?v:String(v).replace('.',',');}
function parseNum(t){t=String(t).trim().replace(',','.');return /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(t) ? parseFloat(t) : NaN;}
function fieldOf(key){for(var i=0;i<fields.length;i++){if(fields[i].key===key)return fields[i];}
return null;}
function loadCfg(delay){var gen=++cfgGen;var tries=0;function attempt(){if(gen!==cfgGen)return;api('/api/config').then(function (j) {if(gen===cfgGen)buildCfg(j);}).catch(function(e){if(gen!==cfgGen)return;fields=[];checkCfg();var m=esc(e&&e.message?e.message:'ei yhteyttä');if(++tries<10){$('cfg').innerHTML='<p class="note">Ladataan… laite ei vastaa vielä ('+m+'), '+
'yritetään uudelleen</p>';setTimeout(attempt,1500);}else{$('cfg').innerHTML = '<p class="msg er">Asetusten lataus epäonnistui (' + m + ')</p>';}});}
setTimeout(attempt,delay||0);}
function buildCfg(j){fields=cfgFields(j);useCfg(fields);var h='';var grp=null;fields.forEach(function(f,i){if(f.group!==grp){grp=f.group;h += '<h3>' + esc(grp || 'Muut') + '</h3>';}
var id='f_'+i;var inp;var meta='oletus '+esc(f.type==='bool'?(f.def?'päällä':'pois'):shown(f.def));if(f.key==='source'||f.key==='master'){var opts=f.key==='source'?SRC:['SYL 1','SYL 2','SYL 3'];inp='<select id="'+id+'">'+opts.map(function(o,k){return k >= f.min && k <= f.max ? '<option value="' + k + '">' + o + '</option>' : '';}).join('') + '</select>';if (f.key === 'source') meta += ' · <b>vaihto käynnistää laitteen uudelleen</b>';}else if(f.type==='bool'){inp='<input type="checkbox" class="sw" id="'+id+'">';}else{inp='<input type="text" id="'+id+'" spellcheck="false" inputmode="'+
(f.type==='float'?'decimal':'numeric')+'">';meta+=' · '+shown(f.min)+'…'+shown(f.max);}
h+='<div class="frow" id="r_'+i+'">'+
'<label for="' + id + '">' + esc(f.label || f.key) + '<small>' + esc(f.key) + '</small></label>' +
'<div class="fin">' + inp + '<span class="unit">' + esc(f.unit || '') + '</span></div>' +
'<div class="fmeta">' + meta + '</div><div class="ferr"></div></div>';});$('cfg').innerHTML = h || '<p class="note">Ei asetuksia.</p>';fields.forEach(setInput);checkCfg();}
function setInput(f,i){var el=$('f_'+i);if(!el)return;if(el.type==='checkbox')el.checked=!!f.value;else el.value=shown(f.value);}
function checkCfg(){var out={};var n=0;var bad=0;fields.forEach(function(f,i){var el=$('f_'+i);var row=$('r_'+i);var err='';var v,chg;if(!el)return;if(el.type==='checkbox'){chg=el.checked!==!!f.value;v=el.checked?'1':'0';}else if(el.tagName==='SELECT'){v=el.value;chg=Number(v)!==Number(f.value);}else{var x=parseNum(el.value);chg=!(x===Number(f.value));v=String(x);if(!isFinite(x))err='Anna luku';else if(f.type!=='float'&&Math.floor(x)!==x)err='Anna kokonaisluku';else if(x<f.min||x>f.max)err='Sallittu '+shown(f.min)+'…'+shown(f.max);}
tog(row,'chg',chg);tog(row,'bad',err);txt(row.querySelector('.ferr'),err);if(err)bad++;if(chg){out[f.key]=v;n++;}});$('bSave').disabled=!n||bad>0;$('bUndo').disabled=!n;txt($('bSave'),n?'Tallenna ('+n+')':'Tallenna');return out;}
function okMsg(t,ok){txt($('cOk'),t);$('cOk').className='msg '+(ok?'ok':'er');}
$('cfg').addEventListener('input',function(){txt($('cOk'),'');checkCfg();});$('cfg').addEventListener('change',checkCfg);$('cfg').addEventListener('submit',function(e){e.preventDefault();});$('bUndo').addEventListener('click',function(){fields.forEach(setInput);checkCfg();$('cErr').hidden=true;});$('bSave').addEventListener('click',function(){var ch=checkCfg();if(!Object.keys(ch).length)return;var reboot=Object.prototype.hasOwnProperty.call(ch,'source');if(reboot&&!confirm('Signaalilähteen vaihto käynnistää laitteen uudelleen. Jatketaanko?'))return;$('bSave').disabled=true;api('/api/config', ch).then(function (r) {if(r&&r.ok){$('cErr').hidden=true;okMsg(reboot?'Tallennettu – laite käynnistyy uudelleen…':'Tallennettu ✓',true);loadCfg(reboot?3000:0);}else{txt($('cErr'),(r&&(r.err||r.msg))||'Tallennus hylättiin');$('cErr').hidden=false;checkCfg();}}).catch(function(e){fail(e);checkCfg();});});$('bDef').addEventListener('click',function(){var src=fieldOf('source');var reboot=!!src&&Number(src.value)!==Number(src.def);var q='Palautetaanko kaikki asetukset oletusarvoihin?';if(reboot)q+=' Signaalilähde palaa oletukseen, joten laite käynnistyy uudelleen.';if(!confirm(q))return;api('/api/config/reset', {}).then(function (r) {var ok=!!(r&&r.ok);$('cErr').hidden=true;if(!ok)okMsg('Palautus epäonnistui',false);else okMsg(reboot?'Oletukset palautettu – laite käynnistyy uudelleen…':'Oletukset palautettu ✓',true);loadCfg(reboot?3000:0);}).catch(fail);});var FLT=['Ei vikaa','Letku irti','Anturi alas (0 V)','Anturi ylös (3,3 V)'];var SIM=[
['engine_on','Moottori käy'],['rpm','Kierrosluku',300,5000,10,'rpm'],['jitter','Käynnin epätasaisuus',0,0.3,0.005,'%'],['drift', 'Kierrosten ajautuminen', -300, 300, 5, 'rpm/s'],['base','Alipaine (keskiarvo)',-90,-5,0.5,'kPa'],['err1','SYL 1 virhe',-8,8,0.1,'kPa'],['err2','SYL 2 virhe',-8,8,0.1,'kPa'],['err3','SYL 3 virhe',-8,8,0.1,'kPa'],['depth','Imupulssin syvyys',0,40,0.5,'kPa'],['res','Letkuresonanssi',0,15,0.5,'kPa'],['xtalk','Ristivuoto',0,0.5,0.01,''],['peak','Paluuaalto (+)',0,60,1,'kPa'],['noise','ADC-kohina',0,50,0.5,'mV'],['fault1','SYL 1 vika'],['fault2','SYL 2 vika'],['fault3','SYL 3 vika']];var SIMDEF={engine_on:1,rpm:850,jitter:0.01,drift:0,base:-55,err1:0,err2:0,err3:0,depth:20,res:3,xtalk:0,peak:0,noise:2,fault1:0,fault2:0,fault3:0};var PRE=[
['Oletus',{}],['SYL 2 +2,5',{err2:2.5}],['SYL 3 −4',{err3:-4}],['Letku irti SYL 2',{fault2:1}],['Anturivika SYL 1',{fault1:2}],['Moottori seis',{engine_on:0}],['4000 rpm',{rpm:4000}],['Ajautuu',{drift:60}],['Saturoituu',{base:-30,peak:40}],['T-haara (kalibrointi)',{depth:0,res:0}]];var simPend={};var simT=0;function simFmt(k,v){if(k==='jitter')return fx(v*100,1);if(k==='xtalk')return fx(v,2);return fx(v,k==='rpm'||k==='drift'?0:1);}
function buildSim(){var h='';SIM.forEach(function(s){var k=s[0];var lab = '<label for="s_' + k + '">' + s[1] + '</label>';if(k==='engine_on'){h += '<div class="srow">' + lab + '<input type="checkbox" class="sw" id="s_' + k + '"></div>';}else if(k.indexOf('fault')===0){h+='<div class="srow">'+lab+'<select id="s_'+k+'">'+FLT.map(function(o,i){return '<option value="' + i + '">' + o + '</option>';}).join('') + '</select></div>';}else{h += '<div class="srow">' + lab + '<output id="o_' + k + '"></output>' +
'<input type="range" id="s_'+k+'" min="'+s[2]+'" max="'+s[3]+'" step="'+s[4]+
'" data-u="' + s[5] + '"></div>';}});$('sim').innerHTML=h;$('pre').innerHTML=PRE.map(function(p,i){return '<button type="button" class="btn gh sm" data-p="' + i + '">' + p[0] + '</button>';}).join('');}
function simOut(k,el,v){var o=$('o_'+k);if(o)txt(o,simFmt(k,v)+' '+(el.getAttribute('data-u')||''));}
function setSim(j){SIM.forEach(function(s){var k=s[0];var el=$('s_'+k);var v=j[k];if(!isNum(v))return;if(el.type==='checkbox')el.checked=!!v;else el.value=String(v);simOut(k,el,v);});}
function loadSim(){return api('/api/sim').then(function (j) { setSim(obj(j)); }).catch(fail);}
function simSend(){var p=simPend;simPend={};api('/api/sim', p).then(function (r) {if(!r||!r.ok)toast('Simulaattori: '+((r&&r.err)||'hylätty'));}).catch(fail);}
function simInput(e){var el=e.target;if(!el.id||el.id.indexOf('s_')!==0)return;var k=el.id.slice(2);var v=el.type==='checkbox'?(el.checked?1:0):Number(el.value);simPend[k]=v;simOut(k,el,v);clearTimeout(simT);simT=setTimeout(simSend,150);}
$('sim').addEventListener('input',simInput);$('sim').addEventListener('change',simInput);$('pre').addEventListener('click',function(e){var b=e.target.closest('[data-p]');if(!b)return;var p={};var k;var o=PRE[Number(b.getAttribute('data-p'))][1];for(k in SIMDEF)p[k]=SIMDEF[k];for(k in o)p[k]=o[k];setSim(p);simPend=p;clearTimeout(simT);simSend();});var svcLoaded=false;function route(){var svc=location.hash==='#huolto';$('pMain').hidden=svc;$('pSvc').hidden=!svc;tog($('tMain'),'on',!svc);tog($('tSvc'),'on',svc);if(svc){if(!svcLoaded){buildSim();svcLoaded=true;}
loadCfg(0);loadSim();}else if(!raf){raf=requestAnimationFrame(drawWave);}}
window.addEventListener('hashchange',route);function setSun(on){tog(doc,'sun',on);try{localStorage.setItem('sbx_sun',on?'1':'0');}catch(x){}
readColors();drawWave();}
$('sun').addEventListener('click',function(){setSun(!doc.classList.contains('sun'));});var sun=false;try{sun=localStorage.getItem('sbx_sun')==='1';}catch(x){sun=false;}
tog(doc,'sun',sun);buildCyls(3);route();connect();})();
</script>
</body>
</html>
)SBXHTML";
