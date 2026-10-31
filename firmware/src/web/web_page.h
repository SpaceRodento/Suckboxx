#pragma once
// GENEROITU TIEDOSTO - ÄLÄ MUOKKAA KÄSIN.
// Lähde: web/index.html, generaattori: tools/embed_web.py
//   python3 tools/embed_web.py          # päivitä
//   python3 tools/embed_web.py --check  # onko ajan tasalla
//
// Koko web-UI yhtenä merkkijonona flashissa (docs/firmware.md §5). Laitteen
// AP:lla ei ole internetiä, joten sivu ei lataa mitään ulkoista.
// Lähde 54162 tavua, sha256 5aec32259adc4eca.
//
// inline constexpr (C++17): yksi kopio koko ohjelmassa. Pelkkä `const char[]` olisi
// sisäisellä linkityksellä ja kopioituisi flashiin jokaiseen käännösyksikköön, joka
// tämän headerin sisällyttää.
// PROGMEM puuttuu tarkoituksella: Arduino-ESP32:n pgmspace.h määrittelee sen tyhjäksi,
// ja vakiodata (.rodata) on silti flashissa, joten sillä ei olisi mitään vaikutusta.
// Pituus ilman päättävää NUL-tavua: sizeof(WEB_PAGE_HTML) - 1.

inline constexpr char WEB_PAGE_HTML[] = R"SBXHTML(<!DOCTYPE html>
<html lang="fi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#160a2b">
<title>Suckboxx</title>
<!--
  Suckboxx UI (WP-5): koko käyttöliittymä yhdessä tiedostossa, ei ulkoisia resursseja (AP:lla ei
  ole internetiä). API: docs/firmware.md §5. JS on ES5-tyylistä (var, function), jotta vanhatkin
  WebViewit toimivat. Koodi pidetään luettavana: sivu on flashissa raakana merkkijonona, ja
  kokoraja on 64 KiB (tools/embed_web.py tarkistaa sen).
-->
<style>
/* ==== Värit ===============================================================
   Vihreä, keltainen ja punainen ovat tilavärejä eli laitteen tuomio. Magenta ja
   syaani ovat koristetta (kanavatunnus, master, valinta) eivätkä koskaan tarkoita
   tilaa. Vihreä (--ok, --b-ok) kuuluu vain tilalle synced (D11): banneri, palkit
   ja mittarin kaista saavat sen samasta ehdosta (isGreen() skriptissä). */
:root {
  --bg: #160a2b;      /* sivun tausta */
  --bg2: #221043;     /* kortit */
  --bg3: #2e1759;     /* kytkimen tausta */
  --line: #46297e;    /* reunaviivat */
  --tx: #f8f3ff;      /* teksti */
  --mu: #c9b8ea;      /* toissijainen teksti */
  --mag: #ff3fd2;     /* koriste: magenta */
  --cy: #31e7ff;      /* koriste: syaani */
  --ok: #2ee87a;      /* tila: vihreä (vain synced) */
  --okt: #03200e;     /* teksti vihreällä pohjalla */
  --nr: #ffb21e;      /* tila: keltainen */
  --nrt: #2b1700;     /* teksti keltaisella pohjalla */
  --nrx: #ffb21e;     /* keltainen tekstinä ja reunana (varoitus) */
  --bd: #d81e3a;      /* tila: punainen */
  --bdb: #ff4d5e;     /* punainen tekstinä ja reunana */
  --id: #554c7c;      /* tila: ei arviota (harmaa) */
  --b-ok: #2ee87a;    /* mittarin palkit: vihreä, keltainen, punainen ja neutraali */
  --b-near: #ffb21e;
  --b-bad: #ff4d5e;
  --b-neu: #c9b8ea;
  --band: rgba(248, 243, 255, .1);   /* tolerassa oleva alue, kun tuomio ei ole vihreä */
  --c0: #31e7ff;      /* kanavien tunnusvärit (käyrät ja pisteet) */
  --c1: #ff5ce1;
  --c2: #f2ecff;
  --grid: rgba(255, 255, 255, .13);
  --glow: #3e1675;
}

/* Päivänvalo: vaalea tausta ja tummennetut värit, jotta kontrasti riittää auringossa. */
html.sun {
  --bg: #fff;
  --bg2: #f4f1fb;
  --bg3: #e4ddf4;
  --line: #a99cc9;
  --tx: #0b0514;
  --mu: #3a3050;
  --mag: #a1007f;
  --cy: #0072a3;
  --nrx: #8a4f00;
  --bdb: #c4102c;
  --b-ok: #0e9b48;
  --b-near: #d17a00;
  --b-bad: #c4102c;
  --b-neu: #3a3050;
  --band: rgba(11, 5, 20, .08);
  --c0: #0072a3;
  --c1: #b0008a;
  --c2: #2c1d52;
  --grid: rgba(0, 0, 0, .16);
  --glow: #fff;
}

/* ==== Perusasetukset ==================================================== */
* { box-sizing: border-box; }
[hidden] { display: none !important; }
html {
  background: var(--bg) radial-gradient(130% 380px at 50% -60px, var(--glow) 0, transparent 75%) no-repeat;
  -webkit-text-size-adjust: 100%;
  color: var(--tx);
  font: 16px/1.35 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
body {
  margin: 0 auto;
  max-width: 980px;
  padding: 0 12px calc(20px + env(safe-area-inset-bottom));
  font-variant-numeric: tabular-nums;
  -webkit-tap-highlight-color: transparent;
}
button, input, select {
  font: inherit;
  color: inherit;
  touch-action: manipulation;   /* ei tuplanapautuksen zoomausta (masterin vaihto vaatii kaksi napautusta) */
}
:focus-visible { outline: 3px solid var(--cy); outline-offset: 2px; }

/* ==== Yläpalkki ========================================================= */
.top {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px 8px;
  padding: 8px 0;
}
.brand { flex: 1; min-width: 0; }
.wm {
  font-weight: 900;
  font-style: italic;
  font-size: 1.7rem;
  letter-spacing: .05em;
  line-height: 1;
  background: linear-gradient(90deg, #ff4d4d, #ff9f1c, #ffe23b, #3bff8a, #31e7ff, #8f6bff, #ff3fd2);
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
  filter: drop-shadow(0 0 6px rgba(255, 63, 210, .5));
}
.tag {
  display: block;
  font-size: .68rem;
  color: var(--mu);
  letter-spacing: .14em;
  text-transform: uppercase;
}
/* Yhteystila: syaani = yhteys ok. Vihreää ei käytetä, ettei se näytä tuomiolta. */
.conn {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: .82rem;
  font-weight: 700;
  color: var(--mu);
  text-align: right;
}
.conn i {
  flex: none;
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: var(--nr);
}
.conn.on i { background: var(--cy); }
.conn.off i { background: var(--bdb); }
.ib {
  width: 44px;
  height: 44px;
  border-radius: 10px;
  border: 1px solid var(--line);
  background: var(--bg2);
  font-size: 1.2rem;
  cursor: pointer;
}
.tabs {
  order: 5;
  flex: 1 0 100%;
  display: flex;
  gap: 6px;
}
.tabs a {
  flex: 1;
  min-height: 44px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  background: var(--bg2);
  border: 1px solid var(--line);
  color: var(--tx);
  text-decoration: none;
  font-weight: 800;
  letter-spacing: .03em;
}
.tabs a.on {
  border-color: var(--mag);
  box-shadow: inset 0 -4px 0 var(--mag);
}

/* ==== Ilmoituspalkit ==================================================== */
.simbar {
  margin: 0 0 8px;
  padding: 9px 10px;
  border-radius: 10px;
  font-weight: 900;
  letter-spacing: .05em;
  text-align: center;
  color: #fff;
  border: 2px solid var(--mag);
  background: repeating-linear-gradient(135deg, #a0007c 0 12px, #55003f 12px 24px);
  text-shadow: 0 1px 2px #000;
}
.offline {
  position: sticky;
  top: 0;
  z-index: 5;
  margin: 0 0 8px;
  padding: 10px;
  border-radius: 10px;
  background: #000;
  color: #fff;
  border: 3px solid var(--bdb);
  font-weight: 900;
  text-align: center;
  font-size: 1.1rem;
}
/* Vanha data on vaarallisempi kuin ei dataa: yhteyden katketessa näkymä harmaantuu. */
html.stale .data {
  filter: grayscale(1);
  opacity: .35;
}

/* Varoitusmerkit (flags): kaikki keltaisella varoitusvärillä, ei koristeväreillä. */
.badges {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 8px;
}
.badges:empty { display: none; }
.badge {
  padding: 5px 10px;
  border-radius: 999px;
  font-size: .8rem;
  font-weight: 900;
  letter-spacing: .05em;
  border: 2px solid var(--nrx);
  color: var(--nrx);
}

/* ==== Tilabanneri ======================================================= */
.banner {
  border-radius: 14px;
  padding: 12px 14px;
  margin-bottom: 10px;
  min-height: 100px;
}
.head {
  font-size: clamp(1.35rem, 6.4vw, 2rem);
  font-weight: 900;
  line-height: 1.15;
}
.advice {
  font-size: clamp(1.02rem, 4.5vw, 1.3rem);
  font-weight: 700;
  margin-top: 5px;
}
.st-ok { background: var(--ok); color: var(--okt); }
.st-near { background: var(--nr); color: var(--nrt); }
.st-bad { background: var(--bd); color: #fff; }
.st-idle { background: var(--id); color: #fff; }
.st-fault {
  background: repeating-linear-gradient(135deg, var(--bd) 0 16px, #7d0c20 16px 32px);
  color: #fff;
  text-shadow: 0 1px 3px #000;
}

/* ==== Kierrosluku ======================================================= */
.rpmbox {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 8px;
  padding: 6px 12px 8px;
  margin-bottom: 10px;
  border-radius: 14px;
  background: var(--bg2);
  border: 2px solid var(--line);
}
.rpm {
  font-size: clamp(3rem, 16vw, 4.4rem);
  font-weight: 900;
  line-height: 1;
}
.rpmbox .u {
  font-weight: 800;
  color: var(--mu);
  margin-left: 6px;
}
.rpmsd {
  font-size: 1.35rem;
  font-weight: 800;
  text-align: right;
  white-space: nowrap;
}
.rpmsd small {
  display: block;
  color: var(--mu);
  font-size: .82rem;
  font-weight: 700;
}
.rpmbox.dim .rpm,
.rpmbox.dim .rpmsd { color: var(--mu); }
.rpmbox.drift {
  border-color: var(--nr);
  animation: pulse 1.2s ease-in-out infinite alternate;
}
@keyframes pulse {
  to { box-shadow: 0 0 0 4px rgba(255, 178, 30, .35); }
}

/* ==== Sylinterikortit ===================================================
   Kortti = yksi kanava: nimi, pystymittari (palkki = poikkeama masterista),
   luku, sanallinen suunta ja (vain kun laite antaa ruuvineuvon) AVAA/SULJE. */
.cyls {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
}
.cyl {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-start;
  background: var(--bg2);
  border: 2px solid var(--line);
  border-radius: 14px;
  padding: 8px 4px 10px;
  text-align: center;
  min-width: 0;
  cursor: pointer;
}
.cyl.m { border-color: var(--cy); }    /* master */
.cyl.w { border-color: var(--tx); }    /* kanava, jota laite neuvoo säätämään */
.cyl.arm {
  outline: 3px dashed var(--mag);      /* masterin vaihto odottaa toista napautusta */
  outline-offset: 2px;
}
.cyl-h {
  font-weight: 900;
  font-size: 1.05rem;
  letter-spacing: .04em;
  white-space: nowrap;
}
.dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  margin-right: 5px;
  vertical-align: 1px;
}

/* Mittari: keskiviiva = master, ylös = imee enemmän kuin master (D3). */
.meter {
  position: relative;
  height: clamp(170px, 34vh, 300px);
  margin: 6px auto;
  width: 74%;
  flex: none;
  max-width: 86px;
  border-radius: 8px;
  background: var(--bg);
  border: 1px solid var(--line);
  overflow: hidden;
}
.band, .bar, .zl {
  position: absolute;
  left: 0;
  right: 0;
}
/* Toleranssikaistat näkyvät vain kun palkit näytetään. Tolerassa oleva alue on
   neutraali ja muuttuu vihreäksi vasta tilassa synced (.meter.ok). */
.band { display: none; }
.meter.on .band { display: block; }
.band.y {
  background: rgba(255, 178, 30, .14);
  border: solid var(--b-near);
  border-width: 2px 0;
}
.band.g {
  background: var(--band);
  border: solid var(--b-neu);
  border-width: 2px 0;
}
.meter.ok .band.g {
  background: rgba(46, 232, 122, .3);
  border-color: var(--b-ok);
}
.zl {
  top: 50%;
  height: 2px;
  margin-top: -1px;
  background: var(--tx);
}
/* Palkin väri: ks. barClass() skriptissä. ok vain tilassa synced. */
.bar {
  left: 20%;
  right: 20%;
  border-radius: 3px;
  display: none;
}
.bar.ok { background: var(--b-ok); }
.bar.near { background: var(--b-near); }
.bar.bad { background: var(--b-bad); }
.bar.neu { background: var(--b-neu); }
.sc {
  position: absolute;
  left: 3px;
  font-size: .7rem;
  color: var(--mu);
  z-index: 1;
}
.sc.t { top: 1px; }
.sc.b { bottom: 1px; }
.clip {
  position: absolute;
  left: 0;
  right: 0;
  text-align: center;
  font-size: .95rem;
  line-height: 1.1;
  color: var(--tx);
  display: none;
  z-index: 2;
}
.clip.up { top: 0; }
.clip.dn { bottom: 0; }
.clip.on { display: block; }
.mtag {
  display: none;
  position: absolute;
  left: 50%;
  top: 50%;
  transform: translate(-50%, -50%);
  background: var(--cy);
  color: #04141a;
  font-size: .68rem;
  font-weight: 900;
  padding: 2px 4px;
  border-radius: 4px;
  letter-spacing: .04em;
  z-index: 2;
}
.cyl.m .mtag { display: block; }
/* Kanavakohtainen vikapeite mittarin päällä. */
.fov {
  display: none;
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  z-index: 3;
  align-items: center;
  justify-content: center;
  color: #fff;
  font-weight: 900;
  font-size: .95rem;
  letter-spacing: .1em;
  -webkit-writing-mode: vertical-rl;
  writing-mode: vertical-rl;
  text-shadow: 0 1px 2px #000;
  background: repeating-linear-gradient(135deg, var(--bd) 0 10px, #7d0c20 10px 20px);
}
.fov.on { display: flex; }
.fov.grey { background: var(--id); }

.dv {
  font-size: clamp(1.5rem, 7.6vw, 2.2rem);
  font-weight: 900;
  line-height: 1.05;
  white-space: nowrap;
}
.dv small {
  font-size: .42em;
  color: var(--mu);
  margin-left: 2px;
}
.dh {
  font-size: .8rem;
  color: var(--mu);
  font-weight: 700;
  min-height: 1.1em;
}
/* AVAA / SULJE: tyhjänä näkymätön, mutta varaa tilan, ettei asettelu hypi. */
.act {
  min-height: 30px;
  margin-top: 6px;
  padding: 4px 8px;
  border-radius: 8px;
  font-weight: 900;
  letter-spacing: .08em;
  background: var(--tx);
  color: var(--bg);
}
.act:empty { visibility: hidden; }
.mhint {
  margin: 8px 2px 0;
  font-size: .85rem;
  color: var(--mu);
  text-align: center;
}
.mhint.hot {
  color: var(--nrt);
  background: var(--nr);
  border-radius: 8px;
  padding: 8px;
  font-weight: 900;
}

/* ==== Kortit, aaltomuoto ja taulukot ==================================== */
.card {
  background: var(--bg2);
  border: 1px solid var(--line);
  border-radius: 14px;
  padding: 10px 12px;
  margin-top: 10px;
}
.card h2 {
  margin: 2px 0 8px;
  font-size: 1.15rem;
  color: var(--mag);
  letter-spacing: .03em;
}
.card h3 {
  margin: 14px 0 4px;
  font-size: 1rem;
}
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  flex-wrap: wrap;
}
#wave {
  display: block;
  width: 100%;
  height: 170px;
  margin-top: 6px;
}
.leg { font-size: .85rem; font-weight: 800; }
.leg span { margin-right: 12px; white-space: nowrap; }
summary {
  min-height: 44px;
  display: flex;
  align-items: center;
  font-weight: 800;
  cursor: pointer;
}
table {
  width: 100%;
  border-collapse: collapse;
  font-size: .88rem;
}
td, th {
  padding: 4px 3px;
  text-align: right;
  border-top: 1px solid var(--line);
}
th { color: var(--mu); font-weight: 700; }
td:first-child, th:first-child { text-align: left; color: var(--mu); }

/* ==== Painikkeet ja huoltosivun lomakkeet =============================== */
.btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-height: 48px;
  padding: 0 16px;
  border-radius: 12px;
  border: 2px solid var(--mag);
  background: linear-gradient(180deg, #ff3fd2, #a8108f);
  color: #fff;
  font-weight: 800;
  text-decoration: none;
  cursor: pointer;
  margin: 4px 6px 4px 0;
}
.btn.gh {
  background: transparent;
  color: var(--tx);
  border-color: var(--line);
}
.btn.sm {
  min-height: 44px;
  padding: 0 12px;
  font-size: .9rem;
}
.btn:disabled { opacity: .4; cursor: default; }
.note { color: var(--mu); font-size: .9rem; margin: 4px 0 8px; }
ol.note { padding-left: 20px; }
.msg { font-weight: 800; margin: 6px 0; min-height: 1.2em; }
.msg.ok { color: var(--ok); }
.msg.er { color: var(--bdb); }
html.sun .msg.ok { color: #08702f; }
.pill {
  display: inline-block;
  margin: 0 6px 6px 0;
  padding: 6px 10px;
  border-radius: 999px;
  font-weight: 800;
  font-size: .88rem;
  border: 2px solid var(--line);
}
.pill.y { border-color: var(--b-ok); }
.pill.n { border-color: var(--nr); }
.busy {
  font-weight: 800;
  color: var(--cy);
  margin: 6px 0;
}
.busy i {
  display: inline-block;
  width: 16px;
  height: 16px;
  margin-right: 8px;
  vertical-align: -3px;
  border: 3px solid var(--cy);
  border-top-color: transparent;
  border-radius: 50%;
  animation: spin .9s linear infinite;
}
@keyframes spin {
  to { transform: rotate(360deg); }
}
.frow, .srow {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 2px 8px;
  padding: 8px 0;
  border-top: 1px solid var(--line);
}
.frow label, .srow label { font-weight: 700; }
.frow label small {
  display: block;
  color: var(--mu);
  font-weight: 400;
  font-size: .74rem;
}
.fin {
  display: flex;
  align-items: center;
  gap: 5px;
}
.fin input[type=text], .fin select, .srow select {
  width: 7.2em;
  min-height: 44px;
  font-size: 1.05rem;
  padding: 0 8px;
  border-radius: 8px;
  border: 2px solid var(--line);
  background: var(--bg);
  color: var(--tx);
  text-align: right;
}
.fin select, .srow select {
  text-align: left;
  width: auto;
  max-width: 11em;
}
.unit {
  min-width: 3em;
  font-size: .82rem;
  color: var(--mu);
}
.fmeta, .ferr {
  grid-column: 1 / -1;
  font-size: .78rem;
  color: var(--mu);
}
.ferr {
  color: var(--bdb);
  font-weight: 800;
  font-size: .85rem;
}
.ferr:empty { display: none; }
.frow.chg input, .frow.chg select { border-color: var(--cy); }
.frow.bad input { border-color: var(--bdb); }
/* Kytkin: 72 x 44 px, jotta kosketusalue on vähintään 44 px. */
.sw {
  -webkit-appearance: none;
  appearance: none;
  width: 72px;
  height: 44px;
  border-radius: 22px;
  background: var(--bg3);
  border: 2px solid var(--line);
  position: relative;
  margin: 0;
  cursor: pointer;
  flex: none;
}
.sw::after {
  content: "";
  position: absolute;
  top: 4px;
  left: 4px;
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: var(--mu);
  transition: left .15s;
}
.sw:checked {
  background: var(--cy);
  border-color: var(--cy);
}
.sw:checked::after {
  left: 32px;
  background: #fff;
}
.srow input[type=range] {
  grid-column: 1 / -1;
  width: 100%;
  height: 44px;
  margin: 0;
  accent-color: var(--mag);
}
.srow output {
  font-weight: 800;
  white-space: nowrap;
}
.errbox, .warn {
  border: 2px solid var(--bdb);
  border-radius: 10px;
  padding: 8px 10px;
  color: var(--bdb);
  font-weight: 800;
  margin: 8px 0;
}
.warn {
  border-color: var(--nr);
  color: inherit;
}
.toast {
  position: fixed;
  left: 50%;
  bottom: calc(16px + env(safe-area-inset-bottom));
  transform: translateX(-50%);
  width: max-content;
  max-width: 92vw;
  background: #000;
  color: #fff;
  border: 2px solid var(--mag);
  border-radius: 12px;
  padding: 10px 14px;
  font-weight: 800;
  z-index: 10;
  box-shadow: 0 6px 24px rgba(0, 0, 0, .5);
}

/* ==== Leveä näyttö ja vaaka-asento ====================================== */
@media (min-width: 640px) {
  #pMain {
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(0, 1.1fr);
    gap: 0 12px;
    align-items: start;
  }
  .tabs { order: 0; flex: 1 1 auto; max-width: 360px; }
  .simbar { padding: 5px; }
  #pMain > .lcol { grid-column: 1; }
  #pMain > .rcol { grid-column: 2; grid-row: 1 / span 2; }
  #pMain > .full { grid-column: 1 / -1; }
  .meter { height: clamp(160px, 46vh, 330px); }
}
@media (orientation: landscape) and (max-height: 520px) {
  .meter { height: max(110px, calc(100vh - 300px)); }
  .top { padding: 4px 0; }
}
@media (prefers-reduced-motion: reduce) {
  * { animation: none !important; transition: none !important; }
}
</style>
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

<!-- ==== Mittausnäkymä ==== -->
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
  <details class="card full" id="diagbox">
    <summary>Diagnostiikka</summary>
    <div id="diag"></div>
  </details>
</main>

<!-- ==== Huoltonäkymä (#huolto) ==== -->
<main id="pSvc" hidden>
  <section class="card">
    <h2>Kalibrointi</h2>
    <div><span id="pZ" class="pill">Nollaus: –</span><span id="pG" class="pill">Vahvistus: –</span></div>
    <p id="cMsg" class="msg"></p>

    <h3>1. Nollaus</h3>
    <p class="note">Moottori seis, letkut saavat olla kiinni – sammuneen moottorin imusarja on ilmanpaineessa. Laite nollaa myös itse, kun moottori on ollut sammuksissa muutaman sekunnin. Nollaus katoaa, kun laite käynnistyy uudelleen (esim. virta katkeaa tai signaalilähde vaihtuu) tai anturiasetuksia muutetaan, ja ilman sitä vihreää ei näytetä. Moottorin sammutus ei poista nollausta.</p>
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
(function () {
'use strict';

// ==== Apufunktiot =========================================================
var $ = function (id) { return document.getElementById(id); };
var doc = document.documentElement;

function isNum(v) { return typeof v === 'number' && isFinite(v); }
function obj(v) { return v && typeof v === 'object' ? v : {}; }
function arr(v) { return Array.isArray(v) ? v : []; }

// Desimaalipilkku ja oikea miinusmerkki; "-0,0" -> "0,0".
function fx(v, d) {
  if (!isNum(v)) return '—';
  var s = v.toFixed(d);
  if (/^-0(\.0+)?$/.test(s)) s = s.slice(1);
  return s.replace('.', ',').replace('-', '−');
}
// Etumerkillinen luku: "+2,5", "−2,5", "0,0".
function sg(v, d) {
  var s = fx(v, d);
  return isNum(v) && s.charAt(0) !== '−' && s !== fx(0, d) ? '+' + s : s;
}
// Asteikkoluku ilman turhia desimaaleja (6 -> "6", 5.5 -> "5,5"): ei pyöristetä kokonaisluvuksi.
function fxt(v, d) {
  var s = fx(v, d);
  return s.indexOf(',') < 0 ? s : s.replace(/0+$/, '').replace(/,$/, '');
}
function sgt(v, d) { return v > 0 ? '+' + fxt(v, d) : fxt(v, d); }

function txt(el, s) { if (el.textContent !== s) el.textContent = s; }
function tog(el, c, on) { el.classList.toggle(c, !!on); }
function esc(s) {
  return String(s).replace(/[&<>"]/g, function (c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
  });
}

var toastT = 0;
function toast(msg, ms) {
  var t = $('toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastT);
  toastT = setTimeout(function () { t.hidden = true; }, ms || 3000);
}
function form(d) {
  return Object.keys(d).map(function (k) {
    return encodeURIComponent(k) + '=' + encodeURIComponent(d[k]);
  }).join('&');
}
function api(path, data) {
  var o = { method: data ? 'POST' : 'GET', cache: 'no-store' };
  if (data) {
    o.body = form(data);
    o.headers = { 'Content-Type': 'application/x-www-form-urlencoded' };
  }
  return fetch(path, o).then(function (r) {
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  });
}
function fail(e) { toast('Virhe: ' + (e && e.message ? e.message : 'ei yhteyttä'), 4000); }

// ==== Tila ================================================================
var S = null;                  // viimeisin snapshot
var lastMsg = 0;               // viimeisimmän snapshotin saapumisaika (ms)
var es = null;                 // EventSource
var cards = [];                // sylinterikorttien DOM-viitteet
var armed = -1, armT = 0;      // masterin vaihdon vahvistus (kaksi napautusta)
var wave = [], waveFs = 62.5, waveFixed = false, raf = 0, colors = [];

// ==== SSE (D9): EventSource yhdistää itse; suljettu yhteys avataan uudelleen ====
function connect() {
  if (es) es.close();
  es = new EventSource('/events');
  es.addEventListener('snap', function (e) {
    var s;
    try { s = JSON.parse(e.data); } catch (x) { return; }
    if (!s || typeof s !== 'object') return;
    lastMsg = Date.now();
    onSnap(s);
  });
  es.onerror = function () {
    setConn('off');
    if (es.readyState === 2) setTimeout(connect, 2000);
  };
}
function setConn(m) {
  var c = $('conn');
  c.className = 'conn ' + m;
  txt(c.lastChild, m === 'on' ? 'Yhteys' : m === 'off' ? 'Ei yhteyttä' : 'Yhdistetään…');
}
// Vanha data on vaarallisempi kuin ei dataa: yli 2 s hiljaisuus harmaannuttaa näkymän.
var t0 = Date.now();
setInterval(function () {
  var age = Date.now() - lastMsg;
  var stale = !lastMsg || age > 2000;
  var lost = stale && (lastMsg || Date.now() - t0 > 5000);
  tog(doc, 'stale', lost);
  $('offline').hidden = !lost;
  setConn(stale ? (lost ? 'off' : 'wait') : 'on');
  if (lastMsg && age > 8000 && es && es.readyState === 1) {
    lastMsg = Date.now() - 2500;
    connect();
  }
}, 500);

// ==== Mittausnäkymä =======================================================
// Tila -> bannerin väri. Värit ovat semanttisia (vihreä/keltainen/punainen/harmaa);
// koristevärit (magenta/syaani) eivät koskaan tarkoita tilaa.
var CLS = {
  synced: 'ok', near: 'near', out_of_sync: 'bad',
  no_signal: 'idle', unstable: 'idle', rpm_too_high: 'idle',
  fault_sensor: 'fault', fault_hose: 'fault', fault_master: 'fault'
};
// Oletusasteikko pelkkään piirtoon, jos snapshotissa ei ole tol-kenttää. Sitä ei koskaan käytetä
// värin tai tuomion perusteena (ks. readTol ja barClass: ilman tol-kenttää palkit ovat neutraaleja).
var DRAW_TOL = { g: 1.5, y: 3 };

// Vihreä on varattu yhdelle tilalle: laite sanoo synced JA antaa tuomion (D11, §8:n
// suunnittelusääntö). Banneri, palkit ja mittarin kaista käyttävät tätä samaa ehtoa, jottei
// mikään osa voi näyttää parempaa kuin tila sallii.
function isGreen(s) { return s.state === 'synced' && s.verdict === true; }

// Toleranssit snapshotista [tol_green, tol_yellow]; null, jos kenttä puuttuu tai on epäkelpo.
function readTol(s) {
  var t = arr(s.tol);
  return isNum(t[0]) && t[0] > 0 && isNum(t[1]) && t[1] > t[0] ? { g: t[0], y: t[1] } : null;
}

// Ruuvineuvo laitteelta rakenteisena (snapshotin act, firmware.md §5): status.h verdict() asettaa
// sen samassa kohdassa, jossa se kirjoittaa "SYL n: avaa|sulje läppää – ...", eli vain
// tuomiotilassa (Lähellä, Ei synkronissa) ja vain kun ero ylittää vihreän rajan. Rajan sisällä
// tila on Synkronoitu tai "Lähellä – kalibroimaton", advice on pelkkä nollausohje ja act null.
// Kortin worst-kehys ja AVAA/SULJE näytetään vain, kun neuvo on, ja täsmälleen sille kanavalle ja
// suunnalle, jonka laite nimeää: UI ei johda neuvoa itse (D11: ei omia kynnyksiä eikä
// etumerkkilogiikkaa). Epäkelpo kenttä tai tila ilman tuomiota -> ei merkintää (turvallinen suunta;
// banneri näyttää neuvon yhä). Palauttaa {ch, open} tai null.
function readAct(s, n) {
  var a = obj(s.act), st = s.state;
  if (s.verdict !== true || (st !== 'near' && st !== 'out_of_sync')) return null;
  var ok = isNum(a.ch) && a.ch % 1 === 0 && a.ch >= 0 && a.ch < n && typeof a.open === 'boolean';
  return ok ? { ch: a.ch, open: a.open } : null;
}

// Kanavakohtainen vikapeite laitteen luokituksesta (ch[i].cond, firmware.md §5): UI ei päättele
// kanavan tilaa pulssista, tasosta eikä kynnyksistä, joten pitoajat (letkuvika 0,5 s) ja
// hose_off_kpa ovat vain laitteella. [teksti, harmaa]: punainen = vika (rivit 6, 7, 12),
// harmaa = ohimenevä (laite odottaa, onko vika). ok, sat ja idle eivät peitä mittaria.
var FOV = {
  sensor: ['ANTURIVIKA', false], hose: ['LETKU IRTI?', false], blocked: ['EI PULSSIA', false],
  no_pulse: ['EI PULSSIA', true]
};
// Diagnostiikkataulukon "Tila"-rivi samasta luokituksesta.
var COND_TXT = {
  ok: 'pulssi', sat: 'pulssi · leikkautuu', idle: 'ei pulssia', no_pulse: 'ei pulssia (odota)',
  hose: 'letku irti?', blocked: 'ei pulssia (tukos?)', sensor: 'anturivika'
};

// Palkin väri tulee laitteen tuomiosta, ei pelkästä |d|:stä:
//  - ilman tuomiota (verdict ei tosi) kaikki on neutraalia
//  - tolerassa oleva palkki on vihreä vain tilassa synced, muuten neutraali
//  - pielessä oleva palkki on keltainen tai punainen (kanavakohtainen korostus)
function barClass(s, a, tol) {
  if (!tol || s.verdict !== true) return 'neu';
  if (a > tol.y) return 'bad';
  if (a > tol.g) return 'near';
  return isGreen(s) ? 'ok' : 'neu';
}

function bannerClass(s) {
  var c = CLS[s.state] || 'idle';
  if (c === 'ok' && !isGreen(s)) return 'idle';
  if (s.verdict !== true && (c === 'near' || c === 'bad')) return 'idle';
  return c;
}

function onSnap(s) {
  S = s;
  var fl = obj(s.flags), st = String(s.state || '');
  $('simbar').hidden = s.src !== 'sim';
  renderBadges(fl);

  $('banner').className = 'banner st-' + bannerClass(s);
  $('banner').setAttribute('data-state', st);
  txt($('head'), String(s.head || st || '—'));
  txt($('advice'), String(s.advice || ''));

  var rpm = s.rpm;
  txt($('rpm'), isNum(rpm) ? String(Math.round(rpm)) : '—');
  txt($('rpmsd'), isNum(s.rpm_sd) ? '± ' + Math.round(s.rpm_sd) : '± —');
  txt($('rpmcv'), isNum(s.rpm_cv) ? 'hajonta ' + fx(s.rpm_cv * 100, 1) + ' %' : 'hajonta');
  tog($('rpmbox'), 'dim', !isNum(rpm) || st === 'unstable' || st === 'no_signal');
  tog($('rpmbox'), 'drift', fl.drift);

  renderCyls(s);
  queueWave(s);
  if ($('diagbox').open) renderDiag(s);
  renderCal(s);
  renderSimInfo(s, st);
}

function renderBadges(fl) {
  var items = [];
  if (fl.uncal_zero) items.push('KALIBROIMATON');
  if (fl.uncal_gain) items.push('VAHVISTUS KALIBROIMATTA');
  if (fl.sat) items.push('SATUROITUU');
  if (fl.drift) items.push('KIERROKSET MUUTTUVAT');
  var h = items.map(function (t) { return '<span class="badge">' + t + '</span>'; }).join('');
  if ($('badges').innerHTML !== h) $('badges').innerHTML = h;
}

function renderSimInfo(s, st) {
  txt($('simState'), String(s.head || st));
  var w = $('simWarn');
  w.hidden = s.src === 'sim';
  if (s.src !== 'sim') {
    txt(w, 'Lähde on nyt "' + String(s.src || '?') +
      '" – simulaattorin asetukset eivät vaikuta mittaukseen.');
  }
}

function buildCyls(n) {
  var h = '';
  for (var i = 0; i < n; i++) {
    h += '<button type="button" class="cyl" data-i="' + i + '">' +
      '<div class="cyl-h"><span class="dot" style="background:var(--c' + (i % 3) + ')"></span>SYL ' +
        (i + 1) + '</div>' +
      '<div class="meter">' +
        '<span class="sc t"></span><span class="sc b"></span>' +
        '<div class="band y"></div><div class="band g"></div><div class="zl"></div><div class="bar"></div>' +
        '<span class="clip up">&#9650;</span><span class="clip dn">&#9660;</span>' +
        '<span class="mtag">MASTER</span><div class="fov"></div>' +
      '</div>' +
      '<div class="dv">—</div><div class="dh"></div><div class="act"></div>' +
      '</button>';
  }
  var box = $('cyls');
  box.innerHTML = h;
  cards = [];
  var q = box.querySelectorAll('.cyl');
  for (var k = 0; k < q.length; k++) {
    var e = q[k];
    cards.push({
      el: e, meter: e.querySelector('.meter'), bar: e.querySelector('.bar'),
      y: e.querySelector('.band.y'), g: e.querySelector('.band.g'),
      up: e.querySelector('.clip.up'), dn: e.querySelector('.clip.dn'),
      t: e.querySelector('.sc.t'), b: e.querySelector('.sc.b'), fov: e.querySelector('.fov'),
      dv: e.querySelector('.dv'), dh: e.querySelector('.dh'), act: e.querySelector('.act')
    });
    e.addEventListener('click', tapCyl.bind(null, k));
  }
  $('wleg').innerHTML = Array.apply(null, Array(n)).map(function (x, i) {
    return '<span style="color:var(--c' + (i % 3) + ')">&#9473; SYL ' + (i + 1) + '</span>';
  }).join('');
}

// Palkki: d (kPa) keskiviivasta, ylös = imee enemmän kuin master (D3).
// Kiinteä asteikko ±2·tol_yellow, jotta palkin pituus tarkoittaa aina samaa.
function renderCyls(s) {
  var ch = arr(s.ch);
  if (ch.length !== cards.length) buildCyls(ch.length);
  var tol = readTol(s), dt = tol || DRAW_TOL;
  var tg = dt.g, ty = dt.y, sc = 2 * ty;
  var st = String(s.state || '');
  var bars = s.bars === true;
  var adv = readAct(s, cards.length);
  var faultSt = st === 'fault_hose' || st === 'fault_master' || st === 'fault_sensor';
  var y = function (v) { return 50 - v / sc * 50; };   // kPa -> % mittarin ylälaidasta

  for (var i = 0; i < cards.length; i++) {
    var k = cards[i], c = obj(ch[i]), d = c.d;
    var isMaster = i === s.master;
    var isWorst = !!adv && i === adv.ch;
    tog(k.el, 'm', isMaster);
    tog(k.el, 'w', isWorst);
    tog(k.el, 'arm', armed === i && Date.now() - armT < 4000);

    // Toleranssikaistat ja asteikko. Vihreä kaista vasta tilassa synced.
    tog(k.meter, 'on', bars && !!tol);
    tog(k.meter, 'ok', isGreen(s));
    k.y.style.top = y(ty) + '%';
    k.y.style.height = (y(-ty) - y(ty)) + '%';
    k.g.style.top = y(tg) + '%';
    k.g.style.height = (y(-tg) - y(tg)) + '%';
    txt(k.t, sgt(sc, 2));
    txt(k.b, fxt(-sc, 2));

    // Palkki.
    var show = bars && isNum(d) && !isMaster;
    k.bar.style.display = show ? 'block' : 'none';
    if (show) {
      var dc = Math.max(-sc, Math.min(sc, d));          // rajataan asteikolle
      k.bar.style.top = (dc >= 0 ? y(dc) : 50) + '%';
      k.bar.style.height = Math.max(Math.abs(dc) / sc * 50, 0.6) + '%';
      k.bar.className = 'bar ' + barClass(s, Math.abs(d), tol);
    }
    tog(k.up, 'on', show && d > sc);
    tog(k.dn, 'on', show && d < -sc);

    // Kanavakohtainen vikapeite laitteen luokituksesta (FOV). Vikatilan nimeämä kanava saa
    // peitteen aina, vaikka luokitus puuttuisi (turvallinen suunta).
    var cf = FOV.hasOwnProperty(c.cond) ? FOV[c.cond] : null;
    var fv = cf ? cf[0] : '', grey = cf ? cf[1] : false;
    if (!fv && faultSt && i === s.fault_ch) fv = 'VIKA';
    txt(k.fov, fv);
    tog(k.fov, 'on', fv);
    tog(k.fov, 'grey', grey);

    // Luku ja sanallinen suunta.
    var dv = '—', dh = '';
    if (bars && isMaster) {
      dv = '0,0';
      dh = 'viite';
    } else if (bars && isNum(d)) {
      dv = sg(d, 1);
      dh = Math.abs(d) < 0.05 ? 'tasan' : d > 0 ? 'imee enemmän' : 'imee vähemmän';
    } else if (c.cond === 'idle') {
      dh = 'ei pulssia';
    }
    if (dv !== '—') {
      var dvHtml = dv + '<small>kPa</small>';
      if (k.dv.innerHTML !== dvHtml) k.dv.innerHTML = dvHtml;
    } else {
      txt(k.dv, dv);
    }
    txt(k.dh, dh);

    // Mitä ruuvia: vain kanavalle ja suuntaan, jonka laitteen neuvo nimeää (readAct).
    txt(k.act, isWorst ? (adv.open ? 'AVAA' : 'SULJE') : '');
  }

  var mh = $('mhint');
  tog(mh, 'hot', st === 'fault_master');
  txt(mh, st === 'fault_master'
    ? 'Master ei kelpaa – napauta toista sylinteriä kahdesti valitaksesi uuden masterin'
    : 'Napauta sylinteriä kahdesti vaihtaaksesi masterin · palkki ylös = imee enemmän');
}

// D7: master vaihtuu vain vahvistetusti (kaksi napautusta), ettei nollataso
// hyppää vahingossa kesken säädön.
function tapCyl(i) {
  if (!S) return;
  if (i === S.master) {
    toast('SYL ' + (i + 1) + ' on jo master');
    return;
  }
  if (armed === i && Date.now() - armT < 4000) {
    armed = -1;
    api('/api/master', { ch: i }).then(function (r) {
      toast(r && r.ok ? 'Master: SYL ' + (i + 1) : 'Ei onnistunut: ' + ((r && (r.err || r.msg)) || '?'));
    }).catch(fail);
  } else {
    armed = i;
    armT = Date.now();
    toast('Napauta SYL ' + (i + 1) + ' uudelleen: vaihda masteriksi', 4000);
  }
  renderCyls(S);
}

// ==== Aaltomuoto ==========================================================
function queueWave(s) {
  var w = obj(s.wave);
  if (isNum(w.fs) && w.fs > 0) waveFs = w.fs;
  var rows = arr(w.s), max = Math.max(16, Math.round(waveFs * 3));
  for (var i = 0; i < rows.length; i++) if (Array.isArray(rows[i])) wave.push(rows[i]);
  if (wave.length > max) wave.splice(0, wave.length - max);
  if (!raf && !document.hidden && !$('pMain').hidden) raf = requestAnimationFrame(drawWave);
}
function readColors() {
  var cs = getComputedStyle(doc);
  colors = ['--c0', '--c1', '--c2', '--grid', '--mu', '--tx'].map(function (v) {
    return cs.getPropertyValue(v).trim();
  });
}
function drawWave() {
  raf = 0;
  var cv = $('wave'), dpr = window.devicePixelRatio || 1, w = cv.clientWidth, h = cv.clientHeight;
  if (!w || !h) return;
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  var g = cv.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  if (!colors.length) readColors();

  // Asteikko: kiinteä −100…+10 tai automaattinen (data + marginaali).
  var lo = Infinity, hi = -Infinity, i, j;
  for (i = 0; i < wave.length; i++) {
    for (j = 0; j < wave[i].length; j++) {
      var v = wave[i][j];
      if (isNum(v)) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
    }
  }
  if (waveFixed || lo > hi) {
    lo = -100;
    hi = 10;
  } else {
    // Auto: vähintään 10 kPa, rajattu anturin alueelle −100…+10.
    var mid = (lo + hi) / 2, span = Math.max(10, (hi - lo) * 1.15);
    lo = Math.max(-105, mid - span / 2);
    hi = Math.min(15, mid + span / 2);
  }

  // Ruudukko ja akselin luvut (askel on 5, 10 tai 20 kPa, joten luvut ovat kokonaislukuja).
  var L = 34, R = 4, T = 6, B = 6;
  var step = hi - lo > 60 ? 20 : hi - lo > 25 ? 10 : 5;
  var Y = function (val) { return T + (hi - val) / (hi - lo) * (h - T - B); };
  g.font = '11px system-ui,sans-serif';
  g.textAlign = 'right';
  g.textBaseline = 'middle';
  g.lineWidth = 1;
  for (var gv = Math.ceil(lo / step) * step; gv <= hi; gv += step) {
    g.strokeStyle = colors[3];
    g.beginPath();
    g.moveTo(L, Y(gv));
    g.lineTo(w - R, Y(gv));
    g.stroke();
    g.fillStyle = colors[4];
    g.fillText(fx(gv, 0), L - 4, Y(gv));
  }

  // Käyrät: uusin näyte oikealla, vanhat vasemmalla.
  var n = Math.max(16, Math.round(waveFs * 3)), dx = (w - L - R) / (n - 1), off = n - wave.length;
  g.lineWidth = 2;
  g.lineJoin = 'round';
  var nc = wave.length ? wave[wave.length - 1].length : 0;
  for (j = 0; j < nc; j++) {
    g.strokeStyle = colors[j % 3];
    g.beginPath();
    var pen = false;
    for (i = 0; i < wave.length; i++) {
      var val2 = wave[i][j];
      if (!isNum(val2)) {
        pen = false;
        continue;
      }
      var px = L + (off + i) * dx, py = Y(Math.max(lo, Math.min(hi, val2)));
      if (pen) g.lineTo(px, py); else g.moveTo(px, py);
      pen = true;
    }
    g.stroke();
  }
}
$('wscale').addEventListener('click', function () {
  waveFixed = !waveFixed;
  txt(this, waveFixed ? 'Asteikko: −100…+10' : 'Asteikko: auto');
  drawWave();
});
window.addEventListener('resize', function () { if (!raf) raf = requestAnimationFrame(drawWave); });

// ==== Diagnostiikka =======================================================
function renderDiag(s) {
  var ch = arr(s.ch), dg = obj(s.diag);
  var rows = [
    ['Keskiarvo kPa', 'mean', 2], ['Taso kPa', 'level', 2], ['Minimi kPa', 'min', 1],
    ['Huippu–huippu', 'p2p', 1], ['rpm', 'rpm', 0], ['mV', 'mv', 0]
  ];
  var h = '<table><tr><th></th>' +
    ch.map(function (x, i) { return '<th>SYL ' + (i + 1) + '</th>'; }).join('') + '</tr>';
  rows.forEach(function (r) {
    h += '<tr><td>' + r[0] + '</td>' +
      ch.map(function (c) { return '<td>' + fx(obj(c)[r[1]], r[2]) + '</td>'; }).join('') + '</tr>';
  });
  h += '<tr><td>Tila</td>' + ch.map(function (c) {
    c = obj(c);
    return '<td>' + (COND_TXT.hasOwnProperty(c.cond) ? COND_TXT[c.cond] : '—') + '</td>';
  }).join('') + '</tr></table><table>';
  var kv = [
    ['Näytetaajuus', fx(dg.fs, 1) + ' Hz'],
    ['Pudonneet (aukot)', fx(dg.gaps, 0)],
    ['Vapaa muisti', isNum(dg.heap) ? Math.round(dg.heap / 1024) + ' kt' : '—'],
    ['Rengas jäädytetty', dg.frozen ? 'kyllä' : 'ei'],
    ['Ero (spread)', fx(s.spread, 2) + ' kPa'],
    ['Tila', esc(s.state || '—')],
    ['Laitteen aika', isNum(s.t) ? fx(s.t / 1000, 1) + ' s' : '—']
  ];
  kv.forEach(function (r) { h += '<tr><td>' + r[0] + '</td><td>' + r[1] + '</td></tr>'; });
  $('diag').innerHTML = h + '</table>';
}
$('diagbox').addEventListener('toggle', function () { if (this.open && S) renderDiag(S); });

// ==== Huoltosivu: kalibrointi =============================================
function renderCal(s) {
  var c = obj(s.cal);
  var z = $('pZ'), g = $('pG');
  txt(z, 'Nollaus: ' + (c.zero ? 'tehty ✓' : 'puuttuu'));
  z.className = 'pill ' + (c.zero ? 'y' : 'n');
  txt(g, 'Vahvistus: ' + (c.gain ? 'tehty ✓' : 'puuttuu'));
  g.className = 'pill ' + (c.gain ? 'y' : 'n');
  $('gBusy').hidden = c.busy !== true;
  $('bGain').disabled = c.busy === true;
  if (typeof c.msg === 'string' && !c.busy) {
    txt($('cMsg'), c.msg);
    $('cMsg').className = 'msg ' + (/onnistui|tehty/i.test(c.msg) ? 'ok' : /poistettu/.test(c.msg) ? '' : 'er');
  }
}
function calCall(path, el) {
  return api(path, {}).then(function (r) {
    if (el) {
      txt(el, (r && r.msg) || '');
      el.className = 'msg ' + (r && r.ok ? 'ok' : 'er');
    }
  }).catch(fail);
}
$('bZero').addEventListener('click', function () { calCall('/api/cal/zero', $('zMsg')); });
$('bGain').addEventListener('click', function () { calCall('/api/cal/gain', null); });
$('bGRst').addEventListener('click', function () {
  if (confirm('Poistetaanko vahvistuskalibrointi? Kanavien herkkyyserot palaavat näkyviin.')) {
    calCall('/api/cal/reset', null);
  }
});

// ==== Huoltosivu: asetukset ===============================================
// Lomake rakennetaan kokonaan GET /api/config -vastauksesta (S5a): uusi kenttä
// firmwaressa näkyy tässä ilman UI-muutosta. Nimeltä tunnetaan vain valintakentät
// source ja master (vaihtoehtojen nimet ovat tässä).
var fields = [];
var SRC = ['Simulaattori', 'ADC (XGZP)', 'HX710B'];
var MASTERS = ['SYL 1', 'SYL 2', 'SYL 3'];

function shown(v) { return typeof v === 'boolean' ? v : String(v).replace('.', ','); }
function parseNum(t) {
  t = String(t).trim().replace(',', '.');
  return /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(t) ? parseFloat(t) : NaN;
}
function optionsOf(f) { return f.key === 'source' ? SRC : f.key === 'master' ? MASTERS : null; }

function cfgRow(f, i) {
  var id = 'f_' + i, inp, opts = optionsOf(f);
  var def = shown(f.def);
  if (f.type === 'bool') def = f.def ? 'päällä' : 'pois';
  else if (opts && opts[f.def]) def = opts[f.def];
  var meta = 'oletus ' + esc(def);
  if (opts) {
    inp = '<select id="' + id + '">' + opts.map(function (o, k) {
      return k >= f.min && k <= f.max ? '<option value="' + k + '">' + o + '</option>' : '';
    }).join('') + '</select>';
    if (f.key === 'source') meta += ' · <b>vaihto käynnistää laitteen uudelleen</b>';
  } else if (f.type === 'bool') {
    inp = '<input type="checkbox" class="sw" id="' + id + '">';
  } else {
    inp = '<input type="text" id="' + id + '" inputmode="' + (f.type === 'float' ? 'decimal' : 'numeric') +
      '" spellcheck="false">';
    meta += ' · ' + shown(f.min) + '…' + shown(f.max);
  }
  return '<div class="frow" id="r_' + i + '">' +
    '<label for="' + id + '">' + esc(f.label || f.key) + '<small>' + esc(f.key) + '</small></label>' +
    '<div class="fin">' + inp + '<span class="unit">' + esc(f.unit || '') + '</span></div>' +
    '<div class="fmeta">' + meta + '</div><div class="ferr"></div></div>';
}
function loadCfg() {
  return api('/api/config').then(function (j) {
    fields = arr(obj(j).fields).filter(function (f) { return f && typeof f.key === 'string'; });
    var h = '', grp = null;
    fields.forEach(function (f, i) {
      if (f.group !== grp) {
        grp = f.group;
        h += '<h3>' + esc(grp || 'Muut') + '</h3>';
      }
      h += cfgRow(f, i);
    });
    $('cfg').innerHTML = h || '<p class="note">Ei asetuksia.</p>';
    fields.forEach(function (f, i) { setInput(f, i); });
    checkCfg();
  }).catch(function (e) {
    $('cfg').innerHTML = '<p class="msg er">Asetusten lataus epäonnistui (' + esc(e.message) + ')</p>';
  });
}
function setInput(f, i) {
  var el = $('f_' + i);
  if (el.type === 'checkbox') el.checked = !!f.value; else el.value = shown(f.value);
}
// Muuttuneet kentät -> {key: teksti}; virheelliset merkitään riville.
function checkCfg() {
  var out = {}, n = 0, bad = 0;
  fields.forEach(function (f, i) {
    var el = $('f_' + i), row = $('r_' + i), err = '', v, chg;
    if (!el) return;
    if (el.type === 'checkbox') {
      v = el.checked;
      chg = v !== !!f.value;
      v = v ? '1' : '0';
    } else if (el.tagName === 'SELECT') {
      v = el.value;
      chg = Number(v) !== Number(f.value);
    } else {
      var x = parseNum(el.value);
      chg = !(x === Number(f.value));
      v = String(x);
      if (!isFinite(x)) err = 'Anna luku';
      else if (f.type !== 'float' && Math.floor(x) !== x) err = 'Anna kokonaisluku';
      else if (x < f.min || x > f.max) err = 'Sallittu ' + shown(f.min) + '…' + shown(f.max);
    }
    tog(row, 'chg', chg);
    tog(row, 'bad', err);
    txt(row.querySelector('.ferr'), err);
    if (err) bad++;
    if (chg) {
      out[f.key] = v;
      n++;
    }
  });
  $('bSave').disabled = !n || bad > 0;
  $('bUndo').disabled = !n;
  txt($('bSave'), n ? 'Tallenna (' + n + ')' : 'Tallenna');
  return out;
}
$('cfg').addEventListener('input', function () { txt($('cOk'), ''); checkCfg(); });
$('cfg').addEventListener('change', checkCfg);
$('cfg').addEventListener('submit', function (e) { e.preventDefault(); });
$('bUndo').addEventListener('click', function () {
  fields.forEach(setInput);
  checkCfg();
  $('cErr').hidden = true;
});
$('bSave').addEventListener('click', function () {
  var ch = checkCfg();
  if (!Object.keys(ch).length) return;
  var reboot = Object.prototype.hasOwnProperty.call(ch, 'source');
  if (reboot && !confirm('Signaalilähteen vaihto käynnistää laitteen uudelleen. Jatketaanko?')) return;
  $('bSave').disabled = true;
  api('/api/config', ch).then(function (r) {
    if (r && r.ok) {
      $('cErr').hidden = true;
      txt($('cOk'), reboot ? 'Tallennettu – laite käynnistyy uudelleen…' : 'Tallennettu ✓');
      setTimeout(loadCfg, reboot ? 3000 : 0);
    } else {
      txt($('cErr'), (r && (r.err || r.msg)) || 'Tallennus hylättiin');
      $('cErr').hidden = false;
      checkCfg();
    }
  }).catch(function (e) {
    fail(e);
    checkCfg();
  });
});

// Oletusten palautus voi vaihtaa signaalilähdettä, ja lähteen vaihto käynnistää laitteen
// uudelleen (nollaus unohtuu, D11). Varoitetaan siitä ennen vahvistusta, samoin kuin tallennuksessa.
$('bDef').addEventListener('click', function () {
  var src = null;
  fields.forEach(function (f) { if (f.key === 'source') src = f; });
  var reboot = !!src && Number(src.value) !== Number(src.def);
  var msg = 'Palautetaanko kaikki asetukset oletusarvoihin?';
  if (reboot) {
    msg += '\n\nSignaalilähde vaihtuu (' + (SRC[src.value] || src.value) + ' → ' +
      (SRC[src.def] || src.def) + '), joten laite käynnistyy uudelleen ja nollaus on tehtävä uudelleen.';
  } else if (!src) {
    msg += '\n\nJos signaalilähde vaihtuu, laite käynnistyy uudelleen.';
  }
  if (!confirm(msg)) return;
  api('/api/config/reset', {}).then(function (r) {
    var ok = !!(r && r.ok);
    $('cErr').hidden = true;
    var done = reboot ? 'Oletukset palautettu – laite käynnistyy uudelleen…' : 'Oletukset palautettu ✓';
    txt($('cOk'), ok ? done : 'Palautus epäonnistui');
    setTimeout(loadCfg, ok && reboot ? 3000 : 0);
  }).catch(fail);
});

// ==== Huoltosivu: simulaattori ============================================
// §5:n sim-avaimet. Liukusäätimet lähettävät muutokset 150 ms viiveellä koottuna,
// jotta liu'utus ei tulvi laitetta pyynnöillä.
var FLT = ['Ei vikaa', 'Letku irti', 'Anturi alas (0 V)', 'Anturi ylös (3,3 V)'];
// [avain, nimi, min, max, askel, yksikkö]
var SIM = [
  ['engine_on', 'Moottori käy'],
  ['rpm', 'Kierrosluku', 300, 5000, 10, 'rpm'],
  ['jitter', 'Käynnin epätasaisuus', 0, 0.3, 0.005, '%'],
  ['drift', 'Kierrosten ajautuminen', -300, 300, 5, 'rpm/s'],
  ['base', 'Alipaine (keskiarvo)', -90, -5, 0.5, 'kPa'],
  ['err1', 'SYL 1 virhe', -8, 8, 0.1, 'kPa'],
  ['err2', 'SYL 2 virhe', -8, 8, 0.1, 'kPa'],
  ['err3', 'SYL 3 virhe', -8, 8, 0.1, 'kPa'],
  ['depth', 'Imupulssin syvyys', 0, 40, 0.5, 'kPa'],
  ['res', 'Letkuresonanssi', 0, 15, 0.5, 'kPa'],
  ['xtalk', 'Ristivuoto', 0, 0.5, 0.01, ''],
  ['peak', 'Paluuaalto (+)', 0, 60, 1, 'kPa'],
  ['noise', 'ADC-kohina', 0, 50, 0.5, 'mV'],
  ['fault1', 'SYL 1 vika'],
  ['fault2', 'SYL 2 vika'],
  ['fault3', 'SYL 3 vika']
];
var SIMDEF = {
  engine_on: 1, rpm: 850, jitter: 0.01, drift: 0, base: -55, err1: 0, err2: 0, err3: 0,
  depth: 20, res: 3, xtalk: 0, peak: 0, noise: 2, fault1: 0, fault2: 0, fault3: 0
};
var PRE = [
  ['Oletus', {}], ['SYL 2 +2,5', { err2: 2.5 }], ['SYL 3 −4', { err3: -4 }],
  ['Letku irti SYL 2', { fault2: 1 }], ['Anturivika SYL 1', { fault1: 2 }],
  ['Moottori seis', { engine_on: 0 }], ['4000 rpm', { rpm: 4000 }], ['Ajautuu', { drift: 60 }],
  ['Saturoituu', { base: -30, peak: 40 }], ['T-haara (kalibrointi)', { depth: 0, res: 0 }]
];
var simPend = {}, simT = 0;

function simFmt(k, v) {
  if (k === 'jitter') return fx(v * 100, 1);
  if (k === 'xtalk') return fx(v, 2);
  return fx(v, k === 'rpm' || k === 'drift' ? 0 : 1);
}
function buildSim() {
  var h = '';
  SIM.forEach(function (s) {
    var k = s[0];
    if (k === 'engine_on') {
      h += '<div class="srow"><label for="s_' + k + '">' + s[1] + '</label>' +
        '<input type="checkbox" class="sw" id="s_' + k + '"></div>';
    } else if (k.indexOf('fault') === 0) {
      h += '<div class="srow"><label for="s_' + k + '">' + s[1] + '</label><select id="s_' + k + '">' +
        FLT.map(function (o, i) { return '<option value="' + i + '">' + o + '</option>'; }).join('') +
        '</select></div>';
    } else {
      h += '<div class="srow"><label for="s_' + k + '">' + s[1] + '</label><output id="o_' + k + '"></output>' +
        '<input type="range" id="s_' + k + '" min="' + s[2] + '" max="' + s[3] + '" step="' + s[4] +
        '" data-u="' + s[5] + '"></div>';
    }
  });
  $('sim').innerHTML = h;
  $('pre').innerHTML = PRE.map(function (p, i) {
    return '<button type="button" class="btn gh sm" data-p="' + i + '">' + p[0] + '</button>';
  }).join('');
}
function setSim(j) {
  SIM.forEach(function (s) {
    var k = s[0], el = $('s_' + k), v = j[k];
    if (!isNum(v)) return;
    if (el.type === 'checkbox') el.checked = !!v; else el.value = String(v);
    var o = $('o_' + k);
    if (o) txt(o, simFmt(k, v) + ' ' + (el.getAttribute('data-u') || ''));
  });
}
function loadSim() { return api('/api/sim').then(function (j) { setSim(obj(j)); }).catch(fail); }
function simSend() {
  var p = simPend;
  simPend = {};
  api('/api/sim', p).then(function (r) {
    if (!r || !r.ok) toast('Simulaattori: ' + ((r && r.err) || 'hylätty'));
  }).catch(fail);
}
function simInput(e) {
  var el = e.target, k = el.id.slice(2);
  if (!el.id || el.id.indexOf('s_') !== 0) return;
  var v = el.type === 'checkbox' ? (el.checked ? 1 : 0) : Number(el.value);
  simPend[k] = v;
  var o = $('o_' + k);
  if (o) txt(o, simFmt(k, v) + ' ' + (el.getAttribute('data-u') || ''));
  clearTimeout(simT);
  simT = setTimeout(simSend, 150);
}
$('sim').addEventListener('input', simInput);
$('sim').addEventListener('change', simInput);
$('pre').addEventListener('click', function (e) {
  var b = e.target.closest('[data-p]');
  if (!b) return;
  var p = {}, k, o = PRE[Number(b.getAttribute('data-p'))][1];
  for (k in SIMDEF) p[k] = SIMDEF[k];
  for (k in o) p[k] = o[k];
  setSim(p);
  simPend = p;
  clearTimeout(simT);
  simSend();
});

// ==== Reititys: #huolto = huoltosivu ======================================
var svcLoaded = false;
function route() {
  var svc = location.hash === '#huolto';
  $('pMain').hidden = svc;
  $('pSvc').hidden = !svc;
  tog($('tMain'), 'on', !svc);
  tog($('tSvc'), 'on', svc);
  if (svc) {
    if (!svcLoaded) {
      buildSim();
      svcLoaded = true;
    }
    loadCfg();
    loadSim();
  } else if (!raf) {
    raf = requestAnimationFrame(drawWave);
  }
}
window.addEventListener('hashchange', route);

// ==== Päivänvalonäkymä: tumma teema on hämärässä paras, aurinko vaatii vaalean ====
function setSun(on) {
  tog(doc, 'sun', on);
  try { localStorage.setItem('sbx_sun', on ? '1' : '0'); } catch (x) { /* yksityinen tila */ }
  readColors();
  drawWave();
}
$('sun').addEventListener('click', function () { setSun(!doc.classList.contains('sun')); });
var sun = false;
try { sun = localStorage.getItem('sbx_sun') === '1'; } catch (x) { /* ei tallennusta */ }
tog(doc, 'sun', sun);

buildCyls(3);
route();
connect();
})();
</script>
</body>
</html>
)SBXHTML";
