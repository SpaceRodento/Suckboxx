/*=====================================================================
  wifi_portal_html_body.h - Portal body markup

  Rakenne seuraa kayttajan tehtavia, ei firmwaren moduuleja
  (docs/kehitys/web-ui-uudelleensuunnittelu.md, rautatesti 16.7.2026):

    KOTI      - arjen silmays: kasvatuksen tila + mittarit + historia.
                Ei yhtaan lomaketta; toiminnot tulevat toimintopalkista.
    KASVATUS  - paatokset tasan kerran: kasvi + aloitustapa (AINOA
                valikkopari koko UI:ssa), ohjelma, parametrit.
    ASETUKSET - muutos tallentuu heti (onchange); ainoa tallennusnappi
                on salaisuuskortilla (WiFi + PIN).
    HUOLTO    - wizardit, manuaaliohjaus, testit, simulaatio leimattuna.
                Himmennetty valilehti: asentajan tyokalu, ei tayki.

  Toimintopalkki (#abar) korvaa vanhat faultBanner/readyCard/
  ggPendingWrap-erilliselementit: yksi tilavetoinen ensisijainen
  toiminto, jonka valitsee laitepaan portal_primary_action.h
  (/api/status "primary_action"). Kayttoonotto ei ole enaa banneri —
  se on oma /setup-sivu (wifi_portal_html_setup.h).
=====================================================================*/

#ifndef WIFI_PORTAL_HTML_BODY_H
#define WIFI_PORTAL_HTML_BODY_H

static const char PORTAL_HTML_BODY[] PROGMEM = R"rawliteral(
<body>
<div class="hdr">
  <h1>PlantMeister</h1>
  <!-- 5x naputus "Online"-tekstiin vaihtaa devModen (Huolto-valilehti nakyviin/
       piiloon). Piilotettu ele: loppukayttaja ei loyda vahingossa. -->
  <div><span class="dot on" id="statusDot"></span><span id="statusTxt" onclick="devTap()" style="cursor:default">Online</span></div>
</div>
<div id="uiMsg" class="msg"></div>

<div id="authModal" class="auth-overlay">
  <div class="auth-card">
    <h3>PIN-kirjautuminen</h3>
    <p>Tama portaali on suojattu. Syota laitteen hallinta-PIN jatkaaksesi.</p>
    <div class="field">
      <label>PIN (4-8 numeroa)</label>
      <input type="password" id="authPin" inputmode="numeric" maxlength="8" pattern="[0-9]*" autocomplete="off">
    </div>
    <button class="btn btn-primary" onclick="authLogin()">Kirjaudu</button>
  </div>
</div>

<div class="tabs">
  <div class="tab active" onclick="showTab(0)">Koti</div>
  <div class="tab" onclick="showTab(1)">Kasvatus</div>
  <div class="tab" onclick="showTab(2)">Asetukset</div>
  <div class="tab dim" id="tabHuolto" onclick="showTab(3)" style="display:none">Huolto</div>
</div>

<!-- Toimintopalkki: nakyy jokaisessa nakymassa. Sisallon valitsee laitepaan
     portal_primary_action.h (FAULT > askelkuittaus > vaihesiirtyma >
     advisory > aloitus) — ensisijaisia toimintoja on maaritelmallisesti
     YKSI, joten kilpailevia kaynnistysnappeja ei voi syntya (rautatestin
     16.7.2026 H6). Kaynnistys on aina kayttajan ele (kartoitus §6). -->
<div id="abar" class="abar" style="display:none">
  <div class="abar-title" id="abarTitle"></div>
  <div class="abar-msg" id="abarMsg"></div>
  <button class="btn btn-primary" id="abarBtn" style="display:none"></button>
  <div class="grid2" id="abarGrid" style="display:none;margin-top:12px">
    <button class="btn btn-primary" style="margin-top:0" onclick="growAction('next')">Hyv&auml;ksy siirtym&auml;</button>
    <button class="btn btn-action" style="margin-top:0" onclick="growAction('delay')">Lykk&auml;&auml; 3 pv</button>
  </div>
</div>

<!-- ── KOTI ── -->
<div class="panel active" id="p0">
  <div class="card" style="border:1px solid #2a4a3a">
    <h3>Kasvatus</h3>
    <div style="font-size:15px;color:#fff;font-weight:500" id="hgState">--</div>
    <div style="font-size:13px;color:#8899aa;margin-top:4px" id="hgDay"></div>
    <div class="hint" id="hgGuid" style="margin-top:10px">--</div>
  </div>
  <div class="card">
    <h3>Ympäristö</h3>
    <div class="row"><span class="lbl">Lämpötila</span><span class="val" id="airTemp">--</span></div>
    <div class="row"><span class="lbl">Kosteus</span><span class="val" id="airHum">--</span></div>
    <div class="row"><span class="lbl">Ilmanpaine</span><span class="val" id="airPres">--</span></div>
  </div>
  <div class="card">
    <h3>Kasvi</h3>
    <div class="row"><span class="lbl">Korkeus</span><span class="val" id="plantH">--</span></div>
    <div class="row"><span class="lbl">Lampun korkeus</span><span class="val" id="motorH">--</span></div>
    <div class="row"><span class="lbl">Valo</span><span class="val" id="lightSt">--</span></div>
  </div>
  <div class="card">
    <h3>Vesi</h3>
    <div class="row"><span class="lbl">Veden lämpötila</span><span class="val" id="waterT">--</span></div>
    <div class="row"><span class="lbl">TDS</span><span class="val" id="tds">--</span></div>
    <div class="row"><span class="lbl">Vesitaso (säiliö)</span><span class="val" id="waterLvl">--</span></div>
    <div class="row"><span class="lbl">Ylivuoto (allas)</span><span class="val" id="waterOvf">--</span></div>
    <div class="row"><span class="lbl">Ilmapumppu</span><span class="val" id="airPump">--</span></div>
  </div>
  <div class="card" id="ebb-card" style="display:none">
    <h3>Ebb &amp; Flow</h3>
    <div class="row"><span class="lbl">Kasvatus</span><span class="val" id="ebb-grow">--</span></div>
    <div class="row"><span class="lbl">Tila</span><span class="val" id="ebb-state">--</span></div>
    <div class="row"><span class="lbl">Seuraava sykli</span><span class="val" id="ebb-next-cycle">--</span></div>
    <div class="row"><span class="lbl">Kierto</span><span class="val" id="ebb-circulate">--</span></div>
    <div class="row"><span class="lbl">Vika</span><span class="val" id="ebb-fault">--</span></div>
    <div class="row"><span class="lbl">Viim. sykli</span><span class="val" id="ebb-cycle">--</span></div>
    <div class="row"><span class="lbl">Efektiivinen väli</span><span class="val" id="ebb-effective-interval">--</span></div>
    <div class="row"><span class="lbl">Tulvituksen syy</span><span class="val" id="ebb-flood-reason">--</span></div>
    <button class="btn btn-danger" id="ebb-ack-btn" style="display:none" onclick="doCmd('EBB_ACK','1')">KUITTAA VIKA</button>
  </div>
  <div class="card">
    <h3>Järjestelmä</h3>
    <div class="row"><span class="lbl">Akku</span><span class="val" id="batt">--</span></div>
    <div class="row"><span class="lbl">LoRa</span><span class="val" id="lora">--</span></div>
    <div class="row"><span class="lbl">Käynnissä</span><span class="val" id="uptime">--</span></div>
    <div class="row"><span class="lbl">WiFi AP sammuu</span><span class="val" id="apTime">--</span></div>
  </div>
  <div class="card">
    <h3>Historia (2h)</h3>
    <div class="field">
      <label>Kenttä</label>
      <select id="histField" onchange="refreshHistoryChart(true)">
        <option value="air_temp">Lämpötila</option>
        <option value="air_humidity">Kosteus</option>
        <option value="plant_height_mm">Kasvin korkeus</option>
        <option value="battery_voltage">Akku</option>
      </select>
    </div>
    <div id="histChart" class="chart-wrap"><div class="chart-empty">Ladataan historiaa...</div></div>
    <div id="histMeta" class="chart-meta">-</div>
  </div>
</div>

<!-- ── KASVATUS ── -->
<div class="panel" id="p1">
  <!-- AINOA paikka jossa kasvi ja aloitustapa valitaan (rautatestin H1:
       kaksi valikkoa samalle arvolle poistettu). Valinnat tallentuvat heti
       kun ne tehdaan — "tallenna toisella valilehdella" hukkasi valinnan. -->
  <div class="card">
    <h3>Valitse kasvi</h3>
    <select id="plantSel" onchange="selectPlant()"></select>
    <div class="field" style="margin-top:12px">
      <label>Mist&auml; aloitat?</label>
      <select id="growStartMethod" onchange="saveStartMethod()">
        <option value="0">Pistokkaasta (leikattu varsi)</option>
        <option value="1">Siemenest&auml;</option>
        <option value="2">Valmiista taimesta</option>
      </select>
      <div style="font-size:12px;color:#8899aa;margin-top:6px">
        M&auml;&auml;r&auml;&auml; mist&auml; vaiheesta kasvatus alkaa ja mit&auml; ohjeita laite antaa.
        Valitse ennen kuin aloitat kasvatuksen.
      </div>
    </div>
  </div>
  <div class="card" style="border:1px solid #4cd97b">
    <h3>Kasvuohjelma</h3>
    <div class="row"><span class="lbl">Tila</span><span class="val" id="ggMode">--</span></div>
    <div class="row"><span class="lbl">Aktiivinen vaihe</span><span class="val" id="ggPhase">--</span></div>
    <div class="row"><span class="lbl">Vaihepäivä</span><span class="val" id="ggDay">--</span></div>
    <div class="row"><span class="lbl">Jäljellä</span><span class="val" id="ggRemain">--</span></div>
    <div class="field" style="margin-top:10px">
      <label>Vaiheen ohje</label>
      <div class="hint" id="ggInstr">--</div>
    </div>
    <!-- Kaynnistys tapahtuu toimintopalkista — taalla ei ole toista
         kaynnistysnappia (H6). Pysaytys on harvinainen, tuhoisa ele:
         vahvistuksen takana. -->
    <button class="btn btn-danger" id="ggStopBtn" style="display:none" onclick="if(confirm('Oletko varma? Nykyinen kasvatus loppuu.'))growAction('stop')">Pysäytä kasvatus</button>
  </div>
  <!-- Aktiivinen ALITEHTAVA (grow_steps.h / grow_step_fsm.h). Nakyy vain kun
       askelsarja on kaynnissa (siemenaloituksen liota->kylva->idata jne).
       Kortti nayttaa koko askeltekstin, oman kellonsa ja Valmis-napin kun
       kuittaus on sallittu. E-ink kayttaa samaa GrowStepView-dataa. -->
  <div class="card" id="ggStepCard" style="display:none;border:1px solid #4cd97b">
    <h3 id="ggStepTitle">Alitehtävä</h3>
    <div class="hint" id="ggStepText" style="white-space:pre-line"></div>
    <div class="row" style="margin-top:8px"><span class="lbl" id="ggStepTimerLbl">Ajastin</span><span class="val" id="ggStepTimer">--</span></div>
    <!-- Valmis kuittaa nykyisen BUTTON-askeleen. Reititys: /api/command
         {cmd:GROW_STEP_ACK} -> INTENT_ACK_STEP (command_handler.h). Ei omaa
         endpointia. Nappi nakyy vain kun step.ack_allowed. -->
    <button class="btn btn-primary" id="ggStepAckBtn" style="display:none;margin-top:10px" onclick="doCmd('GROW_STEP_ACK')">Valmis</button>
  </div>
  <!-- Plant empowerment -tavoitteet vs. nykyarvo (docs/kehitys/pe-ohjausmalli.md
       V1-naytto). Tavoite tulee aktiivivaiheesta (/api/grow), nykyarvo
       antureilta (/api/status). Puuttuva anturi -> nykyarvo "--", ei nolla
       joka nayttaisi mitatulta. Laite ei viela AJA naita — nayttaa vain. -->
  <div class="card" id="peCard" style="display:none">
    <h3>Plant empowerment — tavoite vs. nyt</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">Aktiivivaiheen tavoitteet ja mitatut arvot. Laite näyttää nämä; ohjaus tulee myöhemmin.</div>
    <div class="row"><span class="lbl">Valosumma DLI</span><span class="val" id="peDli">--</span></div>
    <div class="row"><span class="lbl">Valoteho PPFD</span><span class="val" id="pePpfd">--</span></div>
    <div class="row"><span class="lbl">VPD</span><span class="val" id="peVpd">--</span></div>
    <div class="row"><span class="lbl">CO₂</span><span class="val" id="peCo2">--</span></div>
    <div class="row"><span class="lbl">Lehtilämpö</span><span class="val" id="peLeaf">--</span></div>
  </div>
  <div class="card">
    <h3>Kasvuohjelman vaiheet</h3>
    <div id="ggTimeline" class="hint">Ladataan...</div>
  </div>
  <div class="card" id="phase-editor-card" style="display:none">
    <h3>Muokkaa kasvuvaihetta</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">Vaihe: <span id="phaseEditorLabel">--</span></div>
    <div class="field"><label>Valojakso (h/vrk)</label>
      <input type="number" id="phLightHours" min="4" max="24"></div>
    <!-- Tulvitus/ebb&flow piilotettu: laite on NFT-kastelu. Kentat jaavat
         DOMiin (JS lukee/kirjoittaa) mutta eivat nay normaalitabissa; ebb-
         saadot ovat vain Huolto-valilehden ebb-tune-kortissa. -->
    <div class="field" style="display:none"><label>Tulvitusväli (min, 0 = pois)</label>
      <input type="number" id="phFloodIntervalMin" min="0" max="1440"></div>
    <div class="field" style="display:none"><label>Tulvituskesto (s)</label>
      <input type="number" id="phFloodDurationSec" min="0" max="3600"></div>
    <!-- Plant empowerment -tavoitteet (docs/kehitys/pe-ohjausmalli.md). V1:
         nama NAYTETAAN ja saadetaan; laite ei viela aja niita silmukkana.
         Nykyarvo tavoitteen vieressa (phXxxNow) kertoo missa mennaan. -->
    <div style="font-size:12px;color:#4cd97b;margin:10px 0 4px;font-weight:600">Plant empowerment -tavoitteet</div>
    <div class="field"><label>Valosumma DLI (mol/m²/vrk)</label>
      <input type="number" id="phDli" min="0" max="60"></div>
    <div class="field"><label>Valoteho PPFD latvustossa (µmol/m²/s)</label>
      <input type="number" id="phPpfd" min="0" max="2000"></div>
    <div class="grid2">
      <div class="field"><label>VPD min (kPa)</label>
        <input type="number" id="phVpdMin" min="0" max="4" step="0.1"></div>
      <div class="field"><label>VPD max (kPa)</label>
        <input type="number" id="phVpdMax" min="0" max="4" step="0.1"></div>
    </div>
    <div class="field"><label>CO₂-tavoite (ppm)</label>
      <input type="number" id="phCo2" min="0" max="2000" step="10"></div>
    <div class="field"><label>Lehtilämpö-katto (°C)</label>
      <input type="number" id="phLeafMax" min="10" max="45" step="0.5"></div>
    <div style="font-size:11px;color:#8899aa;margin:2px 0 10px">DLI ja PPFD ovat tavoitteita joita laite näyttää; valo on toistaiseksi tuntipohjainen (valojakso yllä). VPD, CO₂ ja lehtilämpö ovat valmennusarvoja.</div>
    <div class="field"><label>Kesto (pv, 0 = jatkuva)</label>
      <input type="number" id="phDurationDays" min="0" max="365"></div>
    <button class="btn btn-primary" onclick="savePhase()">Tallenna vaihe</button>
  </div>
  <div class="card">
    <h3>Kasvuparametrit <span id="plantDirty" style="display:none;color:#f39c12;font-size:12px;font-weight:normal">&#9679; muokattu (ei tallennettu)</span></h3>
    <div class="field"><label>Maksimikorkeus (mm)</label>
      <input type="number" id="pHeight" min="50" max="800" oninput="markPlantDirty()"></div>
    <div class="field"><label>Valojakso (h/vrk)</label>
      <input type="number" id="pLight" min="4" max="24" oninput="markPlantDirty()"></div>
    <div class="field"><label>Kastelumäärä (ml)</label>
      <input type="number" id="pWater" min="1" max="200" oninput="markPlantDirty()"></div>
    <div class="field"><label>Kasteluväli (h)</label>
      <input type="number" id="pInterval" min="1" max="72" oninput="markPlantDirty()"></div>
    <div style="font-size:11px;color:#8899aa">Lämpö-, valo- ja ravinnetavoitteet asetetaan kasvuvaiheittain yllä (Plant empowerment -tavoitteet), eivät kasvitasolla.</div>
  </div>
  <div class="grid2">
    <button class="btn btn-primary" style="margin-top:0" onclick="savePlant()">Tallenna</button>
    <button class="btn btn-action" style="margin-top:0" onclick="resetPlantDefaults()">Palauta oletukset</button>
  </div>
</div>

<!-- ── ASETUKSET ── -->
<!-- Tallennusmalli (rautatestin H4): muutos tallentuu heti kun sen tekee
     (onchange -> /api/config yksi avain kerrallaan). AINOA tallennusnappi
     on salaisuuskortilla, koska salaisuuksien lahetys on tahallinen ele
     (tyhja = ala muuta, poisto = oma rasti — PR #212). -->
<div class="panel" id="p2">
  <div class="card">
    <h3>WiFi STA</h3>
    <div class="row"><span class="lbl">Tila</span><span class="val" id="sta-status">--</span></div>
    <div class="row"><span class="lbl">IP</span><span class="val" id="sta-ip">--</span></div>
    <div class="row"><span class="lbl">Signaali</span><span class="val" id="sta-rssi">--</span></div>
    <div class="field" style="margin-top:10px">
      <label>SSID</label>
      <input type="text" id="wifiSsid" maxlength="31" placeholder="Kotiverkon nimi">
    </div>
    <!-- Salasanakenttaa EI esitayteta laitteen arvolla (script.h loadConfig).
         Tyhja = "ala muuta". Poisto vaatii oman rastin, jotta salasana ei
         voi kadota vahingossa pelkasta tallennusnapin painalluksesta. -->
    <div class="field">
      <label>Salasana</label>
      <input type="password" id="wifiPw" maxlength="63" placeholder="Jata tyhjaksi jos et vaihda">
    </div>
    <div class="field" id="wifiPwClearWrap" style="display:none">
      <label class="checkline"><input type="checkbox" id="wifiPwClear">Poista tallennettu salasana (avoin verkko)</label>
    </div>
    <div class="field">
      <label class="checkline"><input type="checkbox" id="wifiAutoConnect">Yhdista automaattisesti bootissa</label>
    </div>
    <button class="btn btn-action" onclick="wifiScan()">Skannaa verkot</button>
    <div id="wifiList" class="wifi-list"></div>
    <div class="field" style="margin-top:12px">
      <label>Hallinta-PIN (4-8 numeroa)</label>
      <input type="password" id="adminPin" inputmode="numeric" maxlength="8" pattern="[0-9]*" placeholder="Jata tyhjaksi jos et vaihda">
    </div>
    <div class="field" id="adminPinClearWrap" style="display:none">
      <label class="checkline"><input type="checkbox" id="adminPinClear">Poista PIN (portaali ilman tunnistusta)</label>
    </div>
    <button class="btn btn-primary" onclick="wifiSave()">Tallenna WiFi + PIN</button>
  </div>
  <!-- Valojakson aloitus piilotettu: kayttajan ei kuulu saataa sita kasin,
       valojakso on automaattinen. Config-kentta + oletus sailyvat. -->
  <div class="card" style="display:none">
    <h3>Valojakson aloitus</h3>
    <div class="field"><label>Tunti (0-23, käynnistyksestä)</label>
      <input type="number" id="cLightH" min="0" max="23" onchange="saveNum('light_on_hour','cLightH')"></div>
  </div>
  <!-- LoRa-osoitteet piilotettu: LoRa on V2, pois paalta paayksikossa.
       Osoitteet ovat 100% automaattisia. Config-kentat sailyvat. -->
  <div class="card" style="display:none">
    <h3>LoRa</h3>
    <div class="field"><label>Oma osoite</label>
      <input type="number" id="cAddr" min="1" max="255" onchange="saveNum('lora_address','cAddr')"></div>
    <div class="field"><label>Verkko ID</label>
      <input type="number" id="cNet" min="0" max="16" onchange="saveNum('lora_network_id','cNet')"></div>
    <div class="field"><label>Kohde (RPi)</label>
      <input type="number" id="cTarget" min="1" max="255" onchange="saveNum('lora_target','cTarget')"></div>
  </div>
  <div style="font-size:12px;color:#8899aa;padding:0 4px">
    Muutokset tallentuvat heti kun teet ne. Vain WiFi + PIN vaatii
    tallennusnapin, koska salaisuuksien lahetys on tahallinen ele.
  </div>
  <!-- §8.1: autotallennuksen paluutie — palauttaa VAIN taman nakyman
       kentat (valojakso + LoRa). Ei kasvia, WiFia eika kalibrointeja;
       ei ole FACTORY_RESET. Oletusarvot tulevat laitteesta. -->
  <button class="btn btn-action" onclick="resetSettingsView()">Palauta t&auml;m&auml;n n&auml;kym&auml;n oletukset</button>
</div>

<!-- ── HUOLTO ── -->
<!-- Asentajan ja kehittajan tyokalut: kalibrointiwizardit, manuaaliohjaus,
     aktuaattoritestit ja simulaatio (aina SIMULAATIO-leimalla). Erotettu
     visuaalisesti — tama ei ole loppukayttajan arkinakymaa. -->
<div class="panel" id="p3">
  <div style="font-size:12px;color:#f39c12;border:1px dashed #5d3f13;border-radius:6px;padding:8px 10px;margin-bottom:12px">
    &#9881; Huolto ja testaus — tarkoitettu asennukseen, kalibrointiin ja vianetsintaan.
  </div>
  <div class="card" style="border:2px solid #4cd97b">
    <h3 style="color:#4cd97b">&#9881; Mode: <span id="modeName">Normal</span></h3>
    <div class="row"><span class="lbl">AP-portaali</span><span class="val" id="modeAp">Sammuu 15 min jälkeen</span></div>
    <div class="grid2" style="margin-top:8px">
      <button class="btn btn-primary" onclick="setMode(false)">Normal mode</button>
      <button class="btn btn-action" onclick="setMode(true)">Test mode</button>
    </div>
    <div style="font-size:12px;color:#8899aa;margin-top:8px">
      Test mode: AP + /api/* pysyvät pystyssä ikuisesti, ei viiveitä. Asetus tallentuu yli rebootin.
      Testikomennot sallitaan edelleen vain IDLE/FAULT-tiloissa.
    </div>
  </div>
  <div class="card" style="border:1px solid #f39c12">
    <h3 style="color:#f39c12">&#9889; Live-status</h3>
    <div class="row"><span class="lbl">Tila</span><span class="val" id="tstDev">--</span></div>
    <div class="row"><span class="lbl">Valo / ilmapumppu</span><span class="val" id="tstSt">--</span></div>
    <div class="row"><span class="lbl">Moottori</span><span class="val" id="tstMo">--</span></div>
    <div class="row"><span class="lbl">Vesipumppu</span><span class="val" id="tstPu">--</span></div>
    <div class="row"><span class="lbl">Viim. komento</span><span class="val" id="tstLast">--</span></div>
    <button class="btn btn-action" id="tstClearFaultBtn" style="margin-top:8px;display:none" onclick="doCmd('CLEAR_FAULT')">Tyhjenna fault &rarr; IDLE</button>
    <div style="font-size:12px;color:#8899aa;margin-top:6px">Live-paivitys 1 s valein kun tama tabi auki. Aktuaattorit toimivat IDLE-tilassa.</div>
  </div>
  <div class="card">
    <h3>Manuaaliohjaus</h3>
    <div class="grid2">
      <button class="btn btn-action" onclick="doCmd('light_toggle')">Valo päälle/pois</button>
      <button class="btn btn-action" onclick="doCmd('water')">Kastele nyt</button>
      <button class="btn btn-action" onclick="doCmd('motor_up')">Nosta 10mm</button>
      <button class="btn btn-action" onclick="doCmd('motor_down')">Laske 10mm</button>
    </div>
    <button class="btn btn-action" onclick="doCmd('calibrate')">Kalibroi korkeus (0mm)</button>
    <button class="btn btn-danger" onclick="if(confirm('Käynnistä uudelleen?'))doCmd('reboot')">Uudelleenkäynnistys</button>
  </div>
  <div class="card" style="border:1px solid #4cd97b">
    <h3 style="color:#4cd97b">Fyysinen nappi &mdash; lyhyt paine</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      Valitse mit&auml; laitteen napin <b>lyhyt</b> paine tekee (testik&auml;ytt&ouml;&ouml;n). Pitk&auml; paine pysyy aina sammutuksena.
    </div>
    <div class="field">
      <label>Lyhyen paineen toiminto</label>
      <select id="btnAction">
        <option value="0">Ei mit&auml;&auml;n</option>
        <option value="1">Ebb&amp;Flow: tulva nyt</option>
        <option value="2">Sammuta (shutdown)</option>
        <option value="3">Uudelleenk&auml;ynnistys</option>
      </select>
    </div>
    <button class="btn btn-primary" style="margin-top:0" onclick="saveButtonAction()">Tallenna napin toiminto</button>
  </div>
  <div class="card" id="ebb-tune-card" style="border:1px solid #f39c12;display:none">
    <h3 style="color:#f39c12">Ebb&amp;Flow &mdash; ajat (live)</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      0 = k&auml;yt&auml; oletusta (kasvatusvaihe / config.h). Muutos vaikuttaa heti, ei tarvitse flashata. &quot;T&auml;ysi allas&quot; = soak-aika; stand-pipe + ylivuotoanturi pit&auml;v&auml;t tason.
    </div>
    <div id="ebbOverrideBanner" style="font-size:12px;font-weight:bold;border-radius:6px;padding:6px 8px;margin-bottom:8px;display:none"></div>
    <div class="field"><label>Tulvav&auml;li (min)</label>
      <input type="number" id="ebbInterval" min="0" max="1440" step="1"></div>
    <div class="field"><label>T&auml;ytt&ouml; / pumppu p&auml;&auml;ll&auml; (s)</label>
      <input type="number" id="ebbFloodDur" min="0" max="600" step="1"></div>
    <div class="field"><label>Soak / t&auml;ynn&auml; (s)</label>
      <input type="number" id="ebbSoak" min="0" max="3600" step="1"></div>
    <div class="field"><label>Soak-pid&auml;tys duty (%, 0 = pumppu pois)</label>
      <input type="number" id="ebbSoakPwm" min="0" max="100" step="1"></div>
    <div class="field"><label>Tyhjennys-timeout (s, advisory)</label>
      <input type="number" id="ebbDrain" min="0" max="3600" step="1"></div>
    <div class="field" style="margin-top:4px">
      <label style="display:flex;align-items:center;gap:8px;cursor:pointer">
        <input type="checkbox" id="ebbAutoClear" style="width:auto" onchange="saveAutoClear()">
        Ylivuotosalvan automaattikuittaus
      </label>
      <div style="font-size:11px;color:#8899aa;margin-top:4px">
        P&auml;&auml;ll&auml;: ylivuotosalpa vapautuu itsest&auml;&auml;n kun ylivuotokytkin on palannut normaaliksi 30 s ajaksi (allas tyhjentynyt) &mdash; kastelu jatkuu valvomatta ilman k&auml;sin kuittausta. Pois: salpa pit&auml;&auml; kuitata k&auml;sin (KUITTAA VIKA -nappi). P&auml;&auml;ll&auml; = autonomisempi, pois = turvallisin.
      </div>
    </div>
    <div class="field" style="margin-top:8px;border-top:1px solid #2a3a4a;padding-top:8px">
      <label style="display:flex;align-items:center;gap:8px;cursor:pointer">
        <input type="checkbox" id="ebbCirculate" style="width:auto">
        Kierto (anti-stagnaatio, ei nosta vett&auml; kasveille)
      </label>
      <div style="font-size:11px;color:#8899aa;margin-top:4px">
        Matala pumppukierto t&auml;ysien tulvien v&auml;liss&auml; soak-duty:ll&auml; &mdash; pit&auml;&auml; ravinneliuoksen virtaamassa eik&auml; vesi seiso. Vaatii kalibroidun soak-dutyn (yll&auml; &gt; 0). Ajaa vain kasvatuksen aikana, ebb-IDLE:ss&auml;.
      </div>
    </div>
    <div class="field"><label>Kierto-v&auml;li (min)</label>
      <input type="number" id="ebbCircInterval" min="1" max="1440" step="1"></div>
    <div class="field"><label>Kierto-kesto (s, max 290)</label>
      <input type="number" id="ebbCircDur" min="1" max="290" step="1"></div>
    <div class="field"><label>Kierto-duty (%, matala &mdash; ei nosta vett&auml; kasveille)</label>
      <input type="number" id="ebbCircDuty" min="0" max="100" step="1"></div>
    <div class="grid2">
      <button class="btn btn-primary" style="margin-top:0" onclick="saveEbbTiming()">Tallenna ajat</button>
      <button class="btn btn-action" style="margin-top:0" onclick="doCmd('EBB_FLOOD','1')">Aja tulva nyt</button>
    </div>
  </div>
  <div class="card" id="ebb-calib-card" style="border:1px solid #2ecc71;display:none">
    <h3 style="color:#2ecc71">Ebb&amp;Flow &mdash; t&auml;yt&ouml;n kalibrointi</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      Sama idea kuin moottorin rajakaappaus, mutta t&auml;st&auml; kaapataan AIKA. Tallentuvat suoraan flood/drain-aikoihin yll&auml;.<br>
      1. <b>Aloita t&auml;ytt&ouml;</b> &mdash; pumppu k&auml;y.<br>
      2. Kun vesi on halutulla korkeudella, paina <b>Merkitse T&Auml;YSI</b> &rarr; t&auml;ytt&ouml;aika tallentuu, pumppu pys&auml;htyy, tyhjenemisen ajastus alkaa.<br>
      3. Anna tyhjenty&auml;; kun riitt&auml;v&auml;n tyhj&auml;, paina <b>Merkitse TYHJ&Auml;</b> &rarr; tyhjennysaika tallentuu.
    </div>
    <div class="row" style="margin-bottom:8px"><span class="lbl">Vaihe</span><span class="val" id="ebbCalibPhase">--</span></div>
    <button class="btn btn-primary" style="margin-top:0" onclick="ebbCalib('fill_start')">1. Aloita t&auml;ytt&ouml; (pumppu p&auml;&auml;lle)</button>
    <div class="grid2">
      <button class="btn btn-action" style="margin-top:8px" onclick="ebbCalib('capture_full')">2. Merkitse T&Auml;YSI</button>
      <button class="btn btn-action" style="margin-top:8px" onclick="ebbCalib('capture_empty')">3. Merkitse TYHJ&Auml;</button>
    </div>
    <button class="btn btn-danger" style="margin-top:8px" onclick="ebbCalib('cancel')">Keskeyt&auml; / pys&auml;yt&auml; pumppu</button>
  </div>
  <div class="card" id="soak-calib-card" style="border:1px solid #4cd9c5;display:none">
    <h3 style="color:#4cd9c5">Soak-pid&auml;tys &mdash; jatkuva PWM-taso</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      Jatkuvavirtaus-soak: pumppu pit&auml;&auml; veden pinnan halutulla korkeudella matalalla PWM:ll&auml;.<br>
      1. <b>Aloita t&auml;ytt&ouml;</b> &mdash; pumppu nostaa veden.<br>
      2. Kun vesi on oikealla korkeudella, paina <b>Pid&auml; t&auml;ss&auml;</b> &rarr; duty putoaa ~30 %.<br>
      3. Hienos&auml;&auml;d&auml; <b>+/-</b> tai sy&ouml;t&auml; luku kunnes pinta pysyy paikallaan.<br>
      4. <b>Tallenna</b> &rarr; soak-duty (+ kesto) menee kasvatusrutiinin SOAK-vaiheeseen.
    </div>
    <div class="row" style="margin-bottom:6px"><span class="lbl">Sessio</span><span class="val" id="soakPhase">--</span></div>
    <div class="row" style="margin-bottom:8px"><span class="lbl">Pumpun duty</span><span class="val" id="soakDuty">--</span></div>
    <button class="btn btn-primary" style="margin-top:0" onclick="soakCalib('start')">1. Aloita t&auml;ytt&ouml; (pumppu t&auml;ydell&auml;)</button>
    <button class="btn btn-action" style="margin-top:8px" onclick="soakCalib('hold')">2. Pid&auml; t&auml;ss&auml; (~30 %)</button>
    <div class="field" style="margin-top:8px"><label>Hienos&auml;&auml;t&ouml; &mdash; duty %</label>
      <div style="display:grid;grid-template-columns:1fr 2fr 1fr;gap:6px">
        <button class="btn btn-action" style="margin-top:0" onclick="soakAdjust(-1)">&minus;1</button>
        <input type="number" id="soakDutyIn" min="0" max="100" step="1" style="text-align:center">
        <button class="btn btn-action" style="margin-top:0" onclick="soakAdjust(1)">+1</button>
      </div>
    </div>
    <button class="btn btn-action" style="margin-top:6px" onclick="soakAdjust(0)">Aseta sy&ouml;tetty %</button>
    <div class="field" style="margin-top:8px"><label>Soak-kesto (s, valinnainen)</label>
      <input type="number" id="soakDur" min="1" max="3600" step="1" placeholder="esim. 120"></div>
    <div class="grid2">
      <button class="btn btn-primary" style="margin-top:0" onclick="soakCalib('save')">Tallenna soak</button>
      <button class="btn btn-danger" style="margin-top:0" onclick="soakCalib('cancel')">Keskeyt&auml; / pys&auml;yt&auml;</button>
    </div>
  </div>
  <div class="card">
    <h3>Moottori — rajat</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      1. Aja moottori haluttuun ala- tai ylärajaan (+10mm/-10mm tai HEIGHT-kentällä).<br>
      2. Paina <b>Kaappaa</b> — moottorin nykyinen step-asema tallentuu rajaksi.<br>
      3. Tallennetut rajat ovat liikkeen turvaraja: motor_moveTo ei mene niiden ulkopuolelle.
    </div>
    <div class="row"><span class="lbl">Nykyinen asema</span><span class="val" id="calMotorPos">--</span></div>
    <div class="field" style="margin-top:8px"><label>Alaraja (stepit)</label><input type="number" id="calMotorDown" step="1"></div>
    <button class="btn btn-primary" style="margin-top:0" onclick="calibCaptureLimit('down')">Kaappaa NYKYINEN = alaraja</button>
    <div class="field" style="margin-top:12px"><label>Ylaraja (stepit)</label><input type="number" id="calMotorUp" step="1"></div>
    <button class="btn btn-primary" style="margin-top:0" onclick="calibCaptureLimit('up')">Kaappaa NYKYINEN = yläraja</button>
    <div class="grid2" style="margin-top:12px">
      <button class="btn btn-action" style="margin-top:0" onclick="calibProbe('up')">Aja ylärajaan</button>
      <button class="btn btn-action" style="margin-top:0" onclick="calibProbe('down')">Aja alarajaan</button>
    </div>
    <button class="btn btn-primary" onclick="saveCalibration()">Tallenna kalibrointi</button>
  </div>
  <div class="card">
    <h3>Valo — PPFD-kalibrointi (AS7341)</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      Mittaa PPFD (&micro;mol/m&sup2;/s) ulkoisella referenssill&auml; (esim. puhelimen lux-mittari &times; lux&rarr;PPFD-kerroin) samasta paikasta ja suunnasta kuin anturi, ja sy&ouml;t&auml; luku. Laite laskee kertoimen omasta raa&#39;asta lukemastaan.
    </div>
    <div class="row" style="margin-bottom:4px"><span class="lbl">Nykyinen kerroin</span><span class="val" id="ppfdFactor">--</span></div>
    <div class="row" style="margin-bottom:8px"><span class="lbl">Live PPFD / DLI t&auml;n&auml;&auml;n</span><span class="val" id="ppfdLive">--</span></div>
    <div class="field"><label>Referenssi-PPFD (&micro;mol/m&sup2;/s)</label>
      <input type="number" id="ppfdRef" step="0.01" min="0" placeholder="esim. 0.95"></div>
    <div class="grid2" style="margin-top:8px">
      <button class="btn btn-primary" style="margin-top:0" onclick="ppfdCal()">Tallenna kalibrointi</button>
      <button class="btn btn-danger" style="margin-top:0" onclick="ppfdReset()">Nollaa (1.0)</button>
    </div>
  </div>
  <div class="card">
    <h3>Virtamittaus — INA228-kalibrointi</h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:8px">
      Sy&ouml;t&auml; luotettavalla mittarilla mitattu referenssivirta (mA) samalla hetkell&auml; kun laite kuluttaa tasaista virtaa &rarr; laite korjaa oman lukemansa kertoimella. Vaatii HW_INA228=true firmwaressa.
    </div>
    <div class="row" style="margin-bottom:4px"><span class="lbl">Shuntti / kerroin</span><span class="val" id="powerCalFactor">--</span></div>
    <div class="row" style="margin-bottom:8px"><span class="lbl">Live virta</span><span class="val" id="powerLive">--</span></div>
    <div class="field"><label>Referenssivirta (mA)</label>
      <input type="number" id="powerRefMa" step="0.1" min="0" placeholder="esim. 68.5"></div>
    <div class="grid2" style="margin-top:8px">
      <button class="btn btn-primary" style="margin-top:0" onclick="powerCal()">Kalibroi</button>
      <button class="btn btn-danger" style="margin-top:0" onclick="powerReset()">Nollaa (1.0)</button>
    </div>
  </div>
  <div class="card">
    <h3>Valo (relay)</h3>
    <div class="grid2">
      <button class="btn btn-primary" onclick="doCmd('LIGHT','1')">Päällä</button>
      <button class="btn btn-danger" onclick="doCmd('LIGHT','0')">Pois</button>
    </div>
  </div>
  <div class="card">
    <h3>Vesipumppu</h3>
    <div class="field"><label>Määrä (ml)</label>
      <input type="number" id="tMl" value="50" min="1" max="500"></div>
    <div class="grid2">
      <button class="btn btn-primary" style="margin-top:0" onclick="doCmd('water',$('tMl').value)">Aja annos</button>
      <button class="btn btn-danger" style="margin-top:0" onclick="doCmd('WATER_STOP')">PYSÄYTÄ</button>
    </div>
  </div>
  <div class="card">
    <h3>Ylivuotosuoja</h3>
    <div class="row"><span class="lbl">FLOAT_OVF salpa</span><span class="val" id="pumpOvfLatch">--</span></div>
    <button class="btn btn-action" style="margin-top:8px" onclick="pumpClearOverflow()">Kuittaa ylivuotosuoja</button>
    <div style="font-size:12px;color:#8899aa;margin-top:4px">
      FLOAT_OVF pakottaa pumpun pois ja lukitsee salvan. Kuittaa vasta kun
      reservoaari/allas on tyhjennetty.
    </div>
  </div>
  <div class="card">
    <h3>Stepperi</h3>
    <div class="row" style="margin-bottom:8px"><span class="lbl">Asema</span><span class="val"><span id="motor-pos">0</span> / <span id="motor-target">0</span> mm</span></div>
    <div class="grid2">
      <button class="btn btn-action" style="margin-top:0" onclick="doCmd('motor_up','1')">&#9650; +10mm</button>
      <button class="btn btn-action" style="margin-top:0" onclick="doCmd('motor_down','1')">&#9660; -10mm</button>
    </div>
    <button class="btn btn-danger" style="margin-top:8px" onclick="doCmd('MOTOR_STOP')">PYSÄYTÄ STEPPERI</button>
    <div class="field" style="margin-top:12px"><label>Nopeus</label>
      <input type="number" id="motor-speed" value="500" min="1" max="2000" step="50"></div>
    <button class="btn btn-action" onclick="doCmd('MOTOR_SPEED',$('motor-speed').value)">Aseta nopeus</button>
    <div class="grid2" style="margin-top:6px">
      <button class="btn btn-action" style="margin-top:0" onclick="$('motor-speed').value=100;doCmd('MOTOR_SPEED','100')">Hidas (100)</button>
      <button class="btn btn-action" style="margin-top:0" onclick="$('motor-speed').value=500;doCmd('MOTOR_SPEED','500')">Normaali (500)</button>
    </div>
    <button class="btn btn-action" style="margin-top:6px" onclick="$('motor-speed').value=1500;doCmd('MOTOR_SPEED','1500')">Nopea (1500)</button>
    <div style="font-size:12px;color:#8899aa;margin-top:4px">
      TMC2208 (V1): steps/s. Hidas ~100, normaali ~500, max ~1500 (yli sen stalli mahd.).<br>
      MKS UART (V2): 1..127 enum (16 = ~150 RPM, 64 = ~600 RPM).<br>
      Asetus vaikuttaa seuraavaan moveTo-kutsuun.
    </div>
    <div class="field" style="margin-top:8px"><label>Kohde (mm)</label>
      <input type="number" id="tMm" value="100" min="0" max="400"></div>
    <button class="btn btn-action" onclick="doCmd('HEIGHT',$('tMm').value)">Mene kohtaan</button>
    <button class="btn btn-action" onclick="doCmd('calibrate','0')">Kalibroi (0 mm)</button>
  </div>
  <div class="card">
    <h3>Sensorit (testausnäkymä)</h3>
    <button class="btn btn-action" onclick="doCmd('sensor_read','1');setTimeout(update,1200)">Pakota luenta</button>
    <div style="font-size:12px;color:#8899aa;margin-top:4px">
      Lukee kaikki sensorit kerralla (ei odota 30 s sykliä). Käytä kytkentätestaukseen.
    </div>
    <h4 style="margin-top:14px;margin-bottom:4px">Vesi</h4>
    <div class="row"><span class="lbl">FLOAT_MIN (D6)</span><span class="val" id="sFloatMin">--</span></div>
    <div class="row"><span class="lbl">FLOAT_OVF (D7)</span><span class="val" id="sFloatOvf">--</span></div>
    <div class="row"><span class="lbl">DS18B20 (D3, OneWire)</span><span class="val" id="sWaterT">--</span></div>
    <div class="row"><span class="lbl">VL53L0X (I2C 0x29)</span><span class="val" id="sHeight">--</span></div>
    <h4 style="margin-top:14px;margin-bottom:4px">Ilma</h4>
    <div class="row"><span class="lbl">BME280 (I2C 0x76)</span><span class="val" id="sBme">--</span></div>
    <div class="row"><span class="lbl">TDS / ADS1115</span><span class="val" id="sTds">--</span></div>
    <h4 style="margin-top:14px;margin-bottom:4px">Diagnostiikka</h4>
    <div class="row"><span class="lbl">Edellinen luenta</span><span class="val" id="sLastRead">--</span></div>
  </div>
  <!-- Simulaatio asuu VAIN taalla ja on aina leimattu (rautatestin H2:
       demo-vaihe luettiin neuvoksi). Oletusnakymat hakevat /api/grow
       simulate=0:lla — tama kortti on ainoa simulate=1-kuluttaja. -->
  <div class="card" style="border:1px dashed #f39c12">
    <h3>Guided Growing API -testi <span style="color:#f39c12">(SIMULAATIO kun kasvatus ei k&auml;ynniss&auml;)</span></h3>
    <div class="row"><span class="lbl">Tila</span><span class="val" id="tGrowMode">--</span></div>
    <div class="row"><span class="lbl">Vaihe</span><span class="val" id="tGrowPhase">--</span></div>
    <div class="row"><span class="lbl">Pending</span><span class="val" id="tGrowPending">--</span></div>
    <div class="row"><span class="lbl">Override</span><span class="val" id="tGrowOverride">--</span></div>
    <div class="field" style="margin-top:10px">
      <label>Aloitustapa</label>
      <select id="tGrowStartMethod">
        <option value="0">Pistokkaasta</option>
        <option value="1">Siemenest&auml;</option>
        <option value="2">Valmiista taimesta</option>
      </select>
    </div>
    <div class="grid2">
      <button class="btn btn-primary" style="margin-top:0" onclick="growStartFrom('tGrowStartMethod')">GROW_START</button>
      <button class="btn btn-action" style="margin-top:0" onclick="growAction('stop')">GROW_STOP</button>
      <button class="btn btn-action" style="margin-top:0" onclick="growAction('next')">GROW_NEXT</button>
      <button class="btn btn-action" style="margin-top:0" onclick="growAction('delay')">GROW_DELAY</button>
    </div>
  </div>
  <!-- Kasvatuksen tilan suora saato (HUOLTO/KATSELMOINTI). Hyppaa vaiheeseen,
       vaihepaivaan tai alitehtavaan ilman etta kasvatusta odotetaan kellon
       lapi — validoi milta tietty tila nayttaa flashausten valissa. Vaikuttaa
       OIKEAAN kasvatukseen (config persistoituu). Reititys /api/command ->
       INTENT_SET_PHASE / SET_DAY / SET_STEP. Vaatii kaynnissa olevan
       kasvatuksen (muuten router hylkaa "wrong state"). -->
  <div class="card" style="border:1px dashed #f39c12">
    <h3>Kasvatuksen tilan s&auml;&auml;t&ouml; <span style="color:#f39c12">(HUOLTO)</span></h3>
    <div style="font-size:12px;color:#8899aa;margin-bottom:10px">
      Hypp&auml;&auml; suoraan vaiheeseen, vaihep&auml;iv&auml;&auml;n tai aliteht&auml;v&auml;&auml;n. Vaatii k&auml;ynniss&auml; olevan
      kasvatuksen &mdash; muuten s&auml;&auml;t&ouml; ei vaikuta.
    </div>
    <div class="row"><span class="lbl">Kasvatus</span><span class="val" id="setStateNote">--</span></div>
    <div class="field" style="margin-top:10px">
      <label>Vaihe</label>
      <select id="setPhaseSel"></select>
    </div>
    <button class="btn btn-action" style="margin-top:8px" onclick="setGrowPhase()">Aseta valittu vaihe</button>
    <!-- Yhden klikkauksen selailu: hyppaa nykyisesta vaiheesta yksi taakse/eteen
         (laskee indeksin /api/grow-datasta, clampaa rajoihin). -->
    <div class="grid2" style="margin-top:8px">
      <button class="btn btn-action" style="margin-top:0" onclick="stepGrowPhase(-1)">&#9664; Edellinen vaihe</button>
      <button class="btn btn-action" style="margin-top:0" onclick="stepGrowPhase(1)">Seuraava vaihe &#9654;</button>
    </div>
    <div class="field" style="margin-top:12px">
      <label>Vaihep&auml;iv&auml; (0..255)</label>
      <input type="number" id="setDayIn" min="0" max="255" step="1" placeholder="esim. 7">
    </div>
    <button class="btn btn-action" style="margin-top:8px" onclick="setGrowDay()">Aseta vaihep&auml;iv&auml;</button>
    <div class="field" style="margin-top:12px">
      <label>Aliteht&auml;v&auml; / askel (0-pohjainen)</label>
      <input type="number" id="setStepIn" min="0" max="255" step="1" placeholder="esim. 2">
      <div style="font-size:12px;color:#8899aa;margin-top:4px" id="setStepHint">Aktiivinen askel: --</div>
    </div>
    <button class="btn btn-action" style="margin-top:8px" onclick="setGrowStep()">Aseta askel</button>
  </div>
  <!-- §8.1: Huollon paluutie — napin toiminto + ebb-live-ajat takaisin
       oletuksiin (0 = kasvuvaiheen / config.h:n arvot). Ei kalibrointeja
       (mittalaitteella tehtyja ei palauteta napista), ei FACTORY_RESET. -->
  <button class="btn btn-action" onclick="resetMaintView()">Palauta t&auml;m&auml;n n&auml;kym&auml;n oletukset</button>
</div>

<div class="info">PlantMeister v1.0 &mdash; WiFi AP sulkeutuu automaattisesti</div>
</body>
)rawliteral";

#endif // WIFI_PORTAL_HTML_BODY_H
