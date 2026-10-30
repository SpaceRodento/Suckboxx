#pragma once
// Suckboxx ESP32-liima - väliaikainen diagnostiikkasivu (WP-7a).
//
// GET / palauttaa tämän sellaisenaan flashista. Sivu kuuntelee SSE-tapahtumaa
// "diag" (/events) ja näyttää diag.h:ssa kuvatut kentät. Lisäksi se mittaa itse
// puhelimessa vastaanotetun viestitahdin, suurimman viestivälin ja yhteyden
// katkokset - laitetestin "SSE >= 10 Hz puhelimessa" -mittari. "Nollaa" ottaa
// laskureille vertailukohdan ja nollaa huiput (vain sivulla, laite ei muutu).
//
// WP-7b korvaa sivun src/web/web_page.h:lla (WP-5) ja poistaa tämän tiedoston.

namespace sbx {
namespace hal {

constexpr char kDiagPage[] = R"HTML(<!doctype html>
<html lang="fi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Suckboxx – alustatesti</title>
<style>
:root{color-scheme:light dark;--bg:#fff;--fg:#111;--mut:#666;--ok:#1a7f37;--bad:#c62828;--ln:#ddd}
@media(prefers-color-scheme:dark){:root{--bg:#121212;--fg:#eee;--mut:#999;--ok:#5cb85c;--bad:#ef5350;--ln:#333}}
body{margin:0;padding:12px 16px 24px;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
h1{font-size:18px;margin:0 0 4px}
h2{font-size:13px;margin:18px 0 4px;color:var(--mut);text-transform:uppercase;letter-spacing:.05em}
table{border-collapse:collapse;width:100%}
td,th{padding:3px 4px;border-bottom:1px solid var(--ln);text-align:left;font-weight:normal}
th{color:var(--mut);font-size:13px}
.n{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
.ok{color:var(--ok)}.bad{color:var(--bad)}.mut{color:var(--mut)}
button{margin-top:18px;padding:8px 16px;font:inherit}
</style></head><body>
<h1>Suckboxx · alustatesti</h1>
<div>Yhteys: <b id="st" class="bad">yhdistetään…</b> <span class="mut" id="up"></span></div>

<h2>Näytteenotto</h2>
<table>
<tr><th></th><th class="n">nyt / yht.</th><th class="n">nollauksesta</th></tr>
<tr><td>Lähde</td><td class="n" id="src">–</td><td class="n" id="seq">–</td></tr>
<tr><td>Näytetaajuus (Hz)</td><td class="n" id="fs">–</td><td class="n" id="fsr">–</td></tr>
<tr><td>Poikkeama 1 ms:stä (µs)</td><td class="n" id="dev">–</td><td class="n" id="devp">–</td></tr>
<tr><td>Myöhästyminen (µs)</td><td class="n" id="lag">–</td><td class="n" id="lagp">–</td></tr>
<tr><td>Myöhässä &gt; 250 µs</td><td class="n" id="late">–</td><td class="n" id="lated">–</td></tr>
<tr><td>Aukot (seq)</td><td class="n" id="gaps">–</td><td class="n" id="gapsd">–</td></tr>
<tr><td>Jonon pudotukset</td><td class="n" id="qd">–</td><td class="n" id="qdd">–</td></tr>
<tr><td>Ohitetut jaksot</td><td class="n" id="sk">–</td><td class="n" id="skd">–</td></tr>
<tr><td>Jonon huippu</td><td class="n" id="q">–</td><td class="n" id="qp">–</td></tr>
<tr><td>Lukuaika ka / max (µs)</td><td class="n" id="rd">–</td><td class="n" id="rdp">–</td></tr>
<tr><td>HX710B hylätyt</td><td class="n" id="hx">–</td><td class="n" id="hxd">–</td></tr>
</table>

<h2>Kanavat (mV, 100 ms ikkuna)</h2>
<table>
<tr><th></th><th class="n">min</th><th class="n">ka</th><th class="n">max</th></tr>
<tr><td>SYL 1</td><td class="n" id="c0a">–</td><td class="n" id="c0b">–</td><td class="n" id="c0c">–</td></tr>
<tr><td>SYL 2</td><td class="n" id="c1a">–</td><td class="n" id="c1b">–</td><td class="n" id="c1c">–</td></tr>
<tr><td>SYL 3</td><td class="n" id="c2a">–</td><td class="n" id="c2b">–</td><td class="n" id="c2c">–</td></tr>
<tr><td>Kisko</td><td class="n" id="c3a">–</td><td class="n" id="c3b">–</td><td class="n" id="c3c">–</td></tr>
</table>

<h2>Verkko</h2>
<table>
<tr><th></th><th class="n">nyt / yht.</th><th class="n">nollauksesta</th></tr>
<tr><td>AP-asemat / SSE-asiakkaat</td><td class="n" id="cl">–</td><td class="n"></td></tr>
<tr><td>SSE tässä puhelimessa (Hz)</td><td class="n" id="rate">–</td><td class="n" id="gap">–</td></tr>
<tr><td>Viestit / katkokset</td><td class="n"></td><td class="n" id="msgs">–</td></tr>
<tr><td>Lähetetty kaikille</td><td class="n" id="tx0">–</td><td class="n" id="tx0d">–</td></tr>
<tr><td>Osalle / ei kenellekään</td><td class="n" id="tx1">–</td><td class="n" id="tx1d">–</td></tr>
<tr><td>Lähetyskierros (µs)</td><td class="n" id="pu">–</td><td class="n" id="pup">–</td></tr>
</table>

<h2>Muisti (tavua)</h2>
<table>
<tr><th></th><th class="n">nyt</th><th class="n">pienin nollauksesta</th></tr>
<tr><td>Vapaa heap</td><td class="n" id="hf">–</td><td class="n" id="hfm">–</td></tr>
<tr><td>Pienin koskaan</td><td class="n" id="hm">–</td><td class="n"></td></tr>
<tr><td>Suurin vapaa lohko</td><td class="n" id="hb">–</td><td class="n" id="hbm">–</td></tr>
</table>

<button id="z">Nollaa</button>
<script>
const $=i=>document.getElementById(i);
const F0=new Intl.NumberFormat('fi-FI'),F1=new Intl.NumberFormat('fi-FI',{minimumFractionDigits:1,maximumFractionDigits:1}),
F2=new Intl.NumberFormat('fi-FI',{minimumFractionDigits:2,maximumFractionDigits:2});
const s=(i,v)=>{$(i).textContent=v};
let d=null,b=null,p={},ts=[],last=0,msgs=0,cuts=0,open=false;
function zero(){
  b=d?{late:d.late,gaps:d.gaps,qd:d.q_drop,sk:d.skipped,hx:d.hx_bad,t0:d.sse_tx[0],t1:d.sse_tx[1]+d.sse_tx[2]}:null;
  p={fmin:1e9,fmax:0,dev:0,lag:0,q:0,rd:0,pu:0,hf:1e9,hb:1e9,gap:0};msgs=0;cuts=0;
}
zero();
function upT(t){const h=Math.floor(t/3600),m=Math.floor(t/60)%60,x=t%60;return (h?h+' h ':'')+(h||m?m+' min ':'')+x+' s'}
function show(){
  const mx=(k,v)=>{if(v>p[k])p[k]=v;return p[k]},mn=(k,v)=>{if(v<p[k])p[k]=v;return p[k]};
  s('up','· käynnissä '+upT(d.up));
  s('src',d.src);s('seq','ikkuna '+F0.format(d.win)+', seq '+F0.format(d.seq));
  s('fs',F2.format(d.fs));
  if(d.n>0){mn('fmin',d.fs);mx('fmax',d.fs)}
  s('fsr',p.fmax?F2.format(p.fmin)+' – '+F2.format(p.fmax):'–');
  s('dev',F0.format(d.dev_us));s('devp',F0.format(mx('dev',d.dev_us)));
  s('lag',F0.format(d.lag_us));s('lagp',F0.format(mx('lag',d.lag_us)));
  s('late',F0.format(d.late));s('lated','+'+F0.format(d.late-b.late));
  s('gaps',F0.format(d.gaps));s('gapsd','+'+F0.format(d.gaps-b.gaps));
  s('qd',F0.format(d.q_drop));s('qdd','+'+F0.format(d.q_drop-b.qd));
  s('sk',F0.format(d.skipped));s('skd','+'+F0.format(d.skipped-b.sk));
  s('q',d.q_peak+' / '+d.q_len);s('qp',F0.format(mx('q',d.q_peak)));
  s('rd',d.read_us[0]+' / '+d.read_us[1]);s('rdp',F0.format(mx('rd',d.read_us[1])));
  s('hx',F0.format(d.hx_bad));s('hxd','+'+F0.format(d.hx_bad-b.hx));
  for(let i=0;i<4;i++){const c=i<3?d.mv[i]:d.rail;s('c'+i+'a',F0.format(c[0]));s('c'+i+'b',F0.format(c[1]));s('c'+i+'c',F0.format(c[2]))}
  s('cl',d.sta+' / '+d.sse);
  const r=ts.length>1?(ts.length-1)*1000/(ts[ts.length-1]-ts[0]):0;
  s('rate',F1.format(r));s('gap','suurin väli '+F0.format(p.gap)+' ms');
  s('msgs',F0.format(msgs)+' / '+cuts);
  s('tx0',F0.format(d.sse_tx[0]));s('tx0d','+'+F0.format(d.sse_tx[0]-b.t0));
  s('tx1',F0.format(d.sse_tx[1])+' / '+F0.format(d.sse_tx[2]));s('tx1d','+'+F0.format(d.sse_tx[1]+d.sse_tx[2]-b.t1));
  s('pu',F0.format(d.push_us));s('pup',F0.format(mx('pu',d.push_us)));
  s('hf',F0.format(d.heap[0]));s('hfm',F0.format(mn('hf',d.heap[0])));
  s('hm',F0.format(d.heap[1]));
  s('hb',F0.format(d.heap[2]));s('hbm',d.heap[2]?F0.format(mn('hb',d.heap[2])):'–');
}
const es=new EventSource('/events');
es.addEventListener('diag',e=>{
  const t=performance.now();
  if(last&&t-last>p.gap)p.gap=t-last;
  last=t;ts.push(t);while(ts.length>2&&t-ts[0]>3000)ts.shift();
  d=JSON.parse(e.data);if(!b)zero();msgs++;show();
});
es.onopen=()=>{open=true;s('st','yhdistetty');$('st').className='ok'};
es.onerror=()=>{if(open){cuts++;open=false}s('st','ei yhteyttä – yhdistetään uudelleen');$('st').className='bad';
  s('msgs',F0.format(msgs)+' / '+cuts)};
$('z').onclick=()=>{zero();if(d)show()};
</script>
</body></html>
)HTML";

}  // namespace hal
}  // namespace sbx
