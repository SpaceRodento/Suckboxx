/*=====================================================================
  wifi_portal_html_setup.h - Oma onboarding-sivu (/setup)

  Lukittu vaatimus (rautatesti 16.7.2026): kayttoonotolla on OMA sivu,
  ei banneria asiantuntijasivun paalla. Vanha banneri ohjasi pienilla
  alleviivatuilla tekstilinkeilla ("teksti on vahan turhan pienta ja ei
  ole kovin selkeaa pitaako minun painaa tuosta alleviivatusta
  tekstista") — talla sivulla koko ruutu on kayttoonotto: yksi askel
  kerrallaan, isot napit, iso teksti.

  Suunnitelma: docs/kehitys/web-ui-uudelleensuunnittelu.md §3.3.
  Reititys: wifi_portal.h — juuri (/) ohjaa tanne kun onboarding on
  kesken, ja /setup ohjaa juureen kun kayttoonotto on kuitattu.

  Periaatteet joita tama sivu EI saa rikkoa:
  - Portti on laitteessa (onboarding.h): Valmis kutsuu
    /api/onboarding/complete, joka hylkaa jos pakolliset askeleet
    puuttuvat. Sivu naytaa MIKSI nappi on estetty.
  - Turva-IDLE (kartoitus §6): taalla EI ole "Aloita kasvatus" -nappia.
    Kayttoonotto ja kasvatuksen aloitus ovat eri paatokset.
  - Salaisuudet (PR #212): salasana-/PIN-kenttia ei esitayteta koskaan;
    tyhja kentta = ala laheta avainta lainkaan.
  - Ei simulaatiota, ei vaihetietoa — mitaan mika voisi lukea "laite
    kaskee tehda X" (rautatestin H2-regressio).

  Kaytettavat API:t ovat olemassa olevia: /api/status, /api/config,
  /api/plants, /api/wifi/scan, /api/onboarding/complete, /api/auth/login.
=====================================================================*/

#ifndef WIFI_PORTAL_HTML_SETUP_H
#define WIFI_PORTAL_HTML_SETUP_H

static const char PORTAL_SETUP_HTML[] PROGMEM = R"rawliteral(
<body>
<style>
.stitle{font-size:20px;color:#fff;font-weight:600;margin-bottom:6px}
.sdesc{font-size:14px;color:#a9c2d9;line-height:1.5;margin-bottom:12px}
.sprog{font-size:13px;color:#7f8c9b;padding:14px 16px 0}
.sbtn{font-size:17px;padding:16px}
.choice{display:block;padding:14px;border:1px solid #2a3a4a;border-radius:8px;
  margin-top:8px;cursor:pointer;background:#0f1923;color:#c8d6e5;font-size:15px}
.choice.sel{border-color:#4cd97b;background:rgba(76,217,123,0.08);color:#fff}
/* Takaisin on NAPPI, ei pikkulinkki. Ensimmainen versio oli 14px harmaa
   <span> (#7f8c9b) — kayttaja ei loytanyt sita lainkaan rautatestissa
   17.7.2026 ("ei ole mitaan palaa takaisin nappia"), vaikka se oli koko
   ajan siina. Se toisti tasmalleen sen kuvion jonka H5 kielsi: pieni
   alleviivattu teksti jota ei uskalla painaa. */
.sback{display:inline-block;margin:4px 16px 12px;padding:10px 16px;
  border:1px solid #2a3a4a;border-radius:8px;background:#0f1923;
  color:#c8d6e5;font-size:16px;cursor:pointer}
.sback:hover{border-color:#4cd97b;color:#fff}
.snote{font-size:13px;color:#9fd8b0;background:#12321c;border:1px solid #4cd97b;
  border-radius:6px;padding:10px;margin-top:12px;line-height:1.5}
.swarn{font-size:13px;color:#f39c12;margin-top:8px;min-height:1em}
</style>
<div class="hdr">
  <h1>PlantMeister</h1>
  <div><span class="dot on" id="statusDot"></span><span id="statusTxt">Käyttöönotto</span></div>
</div>
<div id="uiMsg" class="msg"></div>
<div class="sprog" id="sProg">Otetaan laite käyttöön — askel 1/4</div>
<button class="sback" id="sBack" style="display:none" onclick="back()">&larr; Takaisin</button>
<div style="padding:0 16px 16px">

  <!-- Askel 1: kotiverkko (valinnainen — AP-only offline on ensiluokkainen tila) -->
  <div class="card" id="sec1" style="display:none">
    <div class="stitle">1. Yhdistä kotiverkkoon</div>
    <div class="sdesc">Kotiverkossa laitetta voi käyttää omasta WiFistäsi.
      Voit myös ohittaa tämän: laite toimii täysin ilman kotiverkkoa, ja
      tavoitat sen aina PlantMeister-verkon kautta.</div>
    <button class="btn btn-action" onclick="scan()">Etsi verkot</button>
    <div id="wifiList" class="wifi-list"></div>
    <div class="field" style="margin-top:12px">
      <label>Verkon nimi</label>
      <input type="text" id="ssid" maxlength="31" placeholder="Valitse listasta tai kirjoita">
    </div>
    <div class="field">
      <label>Salasana</label>
      <input type="password" id="pw" maxlength="63" placeholder="Verkon salasana">
    </div>
    <button class="btn btn-primary sbtn" id="joinBtn" onclick="joinWifi()">Yhdistä ja jatka</button>
    <button class="btn btn-action sbtn" onclick="show(2)">Käytä ilman kotiverkkoa &rarr;</button>
    <div class="swarn" id="w1"></div>
  </div>

  <!-- Askel 2: kasvi + aloitustapa (pakolliset — onboarding.h-portti) -->
  <div class="card" id="sec2" style="display:none">
    <div class="stitle">2. Mitä kasvatat?</div>
    <div class="sdesc">Aloitustapa määrää mistä vaiheesta kasvatus alkaa ja
      mitä ohjeita laite antaa — valitse se minkä oikeasti laitat
      laitteeseen.</div>
    <div class="field">
      <label>Kasvi</label>
      <select id="plantSel" onchange="savePlant()"></select>
    </div>
    <label style="font-size:13px;color:#7f8c9b">Mistä aloitat?</label>
    <div id="mWrap">
      <span class="choice" data-m="0" onclick="saveMethod(0)">Pistokkaasta (leikattu varsi)</span>
      <span class="choice" data-m="1" onclick="saveMethod(1)">Siemenestä</span>
      <span class="choice" data-m="2" onclick="saveMethod(2)">Valmiista taimesta</span>
    </div>
    <button class="btn btn-primary sbtn" id="next2" disabled onclick="show(3)">Jatka &rarr;</button>
    <div class="swarn" id="w2">Valitse kasvi ja aloitustapa.</div>
  </div>

  <!-- Askel 3: PIN (valinnainen) -->
  <div class="card" id="sec3" style="display:none">
    <div class="stitle">3. Suojaa laite PIN-koodilla</div>
    <div class="sdesc">Valinnainen. PIN kysytään kun laitteen asetuksia
      muutetaan selaimesta. Jos asetat PINin, kirjaudut sillä heti
      seuraavaksi.</div>
    <div class="field">
      <label>PIN (4-8 numeroa)</label>
      <input type="password" id="pin" inputmode="numeric" maxlength="8" pattern="[0-9]*" autocomplete="off">
    </div>
    <button class="btn btn-primary sbtn" onclick="setPin()">Aseta PIN ja jatka</button>
    <button class="btn btn-action sbtn" onclick="show(4)">Ohita &rarr;</button>
    <div class="swarn" id="w3"></div>
  </div>

  <!-- Askel 4: yhteenveto + Valmis (portti: onboarding.h) -->
  <div class="card" id="sec4" style="display:none">
    <div class="stitle">4. Valmista?</div>
    <div class="row"><span class="lbl">Kotiverkko</span><span class="val" id="rWifi">--</span></div>
    <div class="row"><span class="lbl">Kasvi</span><span class="val" id="rPlant">--</span></div>
    <div class="row"><span class="lbl">Aloitustapa</span><span class="val" id="rMethod">--</span></div>
    <div class="row"><span class="lbl">PIN</span><span class="val" id="rPin">--</span></div>
    <div class="snote">Laite ei aloita kasvatusta itsestään. Käyttöönoton
      jälkeen se jää lepäämään valmiustilaan — kasvatus alkaa vasta kun
      painat Aloita, laitteesta tai hallintasivulta.</div>
    <button class="btn btn-primary sbtn" id="doneBtn" onclick="complete()">Valmis — ota laite käyttöön</button>
    <div class="swarn" id="w4"></div>
  </div>

  <!-- Valmis-ruutu -->
  <div class="card" id="secDone" style="display:none">
    <div class="stitle">&#10003; Laite on käytössä</div>
    <div class="sdesc">Laite lepää nyt valmiustilassa. Kun olet valmis,
      aloita kasvatus hallintasivulta tai laitteen napista.</div>
    <button class="btn btn-primary sbtn" onclick="location.href='/'">Siirry hallintasivulle</button>
  </div>
</div>

<!-- PIN-kirjautuminen: naytetaan jos jokin kutsu palauttaa 401 (laitteessa
     on jo PIN, esim. kayttoonotto uusitaan tehdasresetin sijaan osittain). -->
<div id="authModal" class="auth-overlay">
  <div class="auth-card">
    <h3>PIN-kirjautuminen</h3>
    <p>Tämä laite on suojattu PIN-koodilla. Syötä PIN jatkaaksesi.</p>
    <div class="field">
      <label>PIN (4-8 numeroa)</label>
      <input type="password" id="authPin" inputmode="numeric" maxlength="8" pattern="[0-9]*" autocomplete="off">
    </div>
    <button class="btn btn-primary" onclick="login()">Kirjaudu</button>
  </div>
</div>

<script>
let step=1, ST={}, scanTimer=null, msgTimer=null, scanEmptyRetries=0, joinTimer=null;
// scanBusy pysayttaa 5 s status-pollauksen skannauksen ajaksi. TAMA on ainoa
// oleellinen ero paasivuun, jonka skannaus toimii "taydellisesti ja nopeasti":
// paasivu ei pollaa mitaan skannatessaan. ESP32:n WiFi.scanNetworks() hyppii
// kanavia, ja samaan aikaan tuleva HTTP-liikenne hairitsee sita — skannaus
// palauttaa nolla verkkoa vaikka niita on 18 kuuluvilla (rautatesti 17.7.2026,
// kaksi kertaa perakkain). Radio ei voi olla kahdessa paikassa yhtaaikaa.
let scanBusy=false;
const OB_WIFI=0x01, OB_PLANT=0x02, OB_METHOD=0x04, OB_PIN=0x08;
const METHODS=['Pistokkaasta','Siemenestä','Valmiista taimesta'];
function $(id){return document.getElementById(id)}
function msg(t,c){const b=$('uiMsg');if(!b)return;b.className='msg show '+(c||'info');
  b.textContent=t;if(msgTimer)clearTimeout(msgTimer);
  msgTimer=setTimeout(()=>{b.className='msg';b.textContent='';},3600);}
function handle(r){if(r.status===401){$('authModal').classList.add('show');throw new Error('auth');}return r.json();}
function apiGet(u){return fetch(u,{credentials:'same-origin'}).then(handle);}
function apiPost(u,b){return fetch(u,{method:'POST',credentials:'same-origin',
  headers:{'Content-Type':'application/json'},body:JSON.stringify(b||{})}).then(handle);}
function login(){
  const p=$('authPin').value.trim();
  if(!/^\d{4,8}$/.test(p)){msg('PIN oltava 4-8 numeroa','err');return;}
  apiPost('/api/auth/login',{pin:p}).then(d=>{
    if(!d.ok){msg('Virhe: '+(d.error||'kirjautuminen epäonnistui'),'err');return;}
    $('authPin').value='';$('authModal').classList.remove('show');
    msg('Kirjautuminen onnistui','ok');refresh();
  }).catch(()=>{});
}
function show(n){
  step=n;
  ['sec1','sec2','sec3','sec4'].forEach((id,i)=>{$(id).style.display=(i+1===n)?'':'none';});
  $('secDone').style.display='none';
  $('sProg').textContent='Otetaan laite käyttöön — askel '+n+'/4';
  $('sBack').style.display=(n>1)?'':'none';
  if(n===4)fillSummary();
}
function back(){if(step>1)show(step-1);}
function refresh(){
  // Ei status-kyselyita skannauksen aikana — ne hairitsevat radiota (ks. scanBusy).
  if(scanBusy) return Promise.resolve();
  return apiGet('/api/status').then(d=>{
    ST=d;
    if(d.onboarding===false){location.href='/';return;}
    const s=d.onboarding_steps|0;
    const both=(s&OB_PLANT)&&(s&OB_METHOD);
    $('next2').disabled=!both;
    $('w2').textContent=both?'':'Valitse kasvi ja aloitustapa.';
    if(typeof d.grow_start_method!=='undefined'&&(s&OB_METHOD)){
      document.querySelectorAll('.choice').forEach(c=>
        c.classList.toggle('sel',+c.dataset.m===(d.grow_start_method|0)));
    }
  }).catch(()=>{});
}
// ── Askel 1: WiFi ──
// HUOM tyhjasta tuloksesta: ESP32:n skannaus palauttaa herkasti NOLLA verkkoa
// kun radio on kiireinen (esim. STA yhdistaa samaan aikaan) — se ei ole
// totuus vaan ohimeneva tila. Ensimmainen versio naytti heti "Verkkoja ei
// loytynyt" eika yrittanyt uudelleen; kayttajalle se nayttaa rikkinaiselta
// (rautatesti 17.7.2026: 18 verkkoa kuuluvilla, sivu sanoi ettei yhtaan).
// API poistaa tulokset luvun jalkeen (WiFi.scanDelete), joten seuraava
// kysely kaynnistaa AIDOSTI uuden skannauksen — retry on siis halpa.
function sigText(r){if(typeof r!=='number')return '--';
  if(r>=-55)return 'erinomainen';if(r>=-67)return 'hyvä';
  if(r>=-75)return 'kohtalainen';return 'heikko';}
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML;}
function scanPoll(){
  apiGet('/api/wifi/scan').then(d=>{
    if(d.status==='scanning'){$('wifiList').innerHTML='<div class="chart-empty">Etsitään...</div>';
      scanTimer=setTimeout(scanPoll,2000);return;}
    const arr=Array.isArray(d.networks)?d.networks:[];
    if(!arr.length){
      // Nolla verkkoa on lahes aina ohimeneva (radio kiireinen), ei totuus.
      if(scanEmptyRetries<2){
        scanEmptyRetries++;
        $('wifiList').innerHTML='<div class="chart-empty">Etsitään uudelleen...</div>';
        scanTimer=setTimeout(scanPoll,1500);return;
      }
      scanBusy=false;
      $('wifiList').innerHTML='<div class="chart-empty">Verkkoja ei löytynyt. '+
        'Voit kirjoittaa verkon nimen käsin tai painaa Etsi verkot uudelleen.</div>';
      return;
    }
    scanEmptyRetries=0;scanBusy=false;
    $('wifiList').innerHTML='';
    arr.forEach(n=>{
      const ssid=String(n.ssid||'');
      if(!ssid)return;
      const row=document.createElement('div');
      row.className='wifi-item';
      row.onclick=()=>{$('ssid').value=ssid;
        document.querySelectorAll('.wifi-item').forEach(w=>w.classList.remove('active'));
        row.classList.add('active');};
      row.innerHTML='<div><div class="wifi-name">'+esc(ssid)+'</div>'+
        '<div class="wifi-meta">'+(n.rssi||'-')+' dBm ('+sigText(n.rssi)+')</div></div>';
      $('wifiList').appendChild(row);
    });
  }).catch(()=>{scanBusy=false;msg('Verkkohaku epäonnistui','err');});
}
function scan(){if(scanTimer)clearTimeout(scanTimer);scanEmptyRetries=0;scanBusy=true;
  $('wifiList').innerHTML='<div class="chart-empty">Etsitään...</div>';scanPoll();}
// VAHVISTUS, ei lupausta: ensimmainen versio tallensi asetuksen, sanoi
// "laite yhdistaa taustalla" ja hyppasi HETI askeleeseen 2. Kayttaja ei
// siis koskaan saanut tietaa onnistuiko yhteys — rautatesti 17.7.2026:
// "Sain yhdistettya manuaalisesti kai, mutta siita ei tullut vahvistusta
// mika on vaivaava." Nyt jaadaan askeleeseen 1 ja pollataan /api/status
// kunnes wifi_sta_connected kertoo totuuden suuntaan tai toiseen.
function joinWifi(){
  const ssid=($('ssid').value||'').trim(), pw=$('pw').value||'';
  if(!ssid){$('w1').textContent='Anna verkon nimi tai ohita askel.';return;}
  if(pw.length>0&&pw.length<8){$('w1').textContent='WiFi-salasana on vähintään 8 merkkiä.';return;}
  $('w1').textContent='';
  const body={wifi_ssid:ssid,wifi_auto_connect:1};
  if(pw.length>0)body.wifi_password=pw;   // tyhja = ala laheta avainta (PR #212)
  $('joinBtn').disabled=true;$('joinBtn').textContent='Yhdistetään...';
  apiPost('/api/config',body).then(d=>{
    if(!d.ok){joinFail('Virhe: '+(d.error||'tuntematon'));return;}
    joinWait(0);
  }).catch(()=>joinFail('Tallennus epäonnistui'));
}
function joinFail(t){
  if(joinTimer)clearTimeout(joinTimer);
  $('joinBtn').disabled=false;$('joinBtn').textContent='Yhdistä ja jatka';
  $('w1').textContent=t;
}
// ~20 s riittaa liittymiseen + DHCP:hen. Jos ei onnistu, syy on lahes aina
// vaara salasana — sanotaan se suoraan eika jateta arvailtavaksi. Askel on
// silti valinnainen, joten tarjotaan ulospaasy ilman kotiverkkoa.
function joinWait(n){
  if(joinTimer)clearTimeout(joinTimer);
  apiGet('/api/status').then(d=>{
    ST=d;
    if(d.wifi_sta_connected){
      // Vahvistus jaa NAKYVIIN eika valahda ohi: kayttaja siirtyy eteenpain
      // itse kun on lukenut sen. Ensimmainen versio hyppasi 1,4 s jalkeen
      // automaattisesti — "voisi olla pidempaan esilla" (rautatesti 17.7.2026).
      $('joinBtn').textContent='Yhdistetty &#10003;';
      $('w1').textContent='';
      $('wifiList').innerHTML='<div class="snote">&#10003; Yhdistetty verkkoon '+
        esc(ST.wifi_ssid||$('ssid').value||'')+
        (d.wifi_sta_ip?('<br>Laitteen osoite kotiverkossa: '+esc(d.wifi_sta_ip)):'')+
        '</div>';
      msg('Yhdistetty verkkoon'+(d.wifi_sta_ip?(' — '+d.wifi_sta_ip):''),'ok');
      $('joinBtn').style.display='none';
      let nb=$('nextAfterJoin');
      if(!nb){
        nb=document.createElement('button');
        nb.id='nextAfterJoin';nb.className='btn btn-primary sbtn';
        nb.textContent='Jatka →';
        nb.onclick=()=>{refresh().then(()=>show(2));};
        $('joinBtn').parentNode.insertBefore(nb,$('joinBtn'));
      }
      nb.style.display='';
      return;
    }
    if(n>=10){joinFail('Ei saatu yhteyttä verkkoon. Tarkista salasana, '+
      'tai jatka ilman kotiverkkoa.');return;}
    joinTimer=setTimeout(()=>joinWait(n+1),2000);
  }).catch(()=>joinFail('Yhteyden tarkistus epäonnistui'));
}
// ── Askel 2: kasvi + aloitustapa ──
function loadPlants(){
  apiGet('/api/plants').then(d=>{
    const sel=$('plantSel');sel.innerHTML='';
    const ph=document.createElement('option');
    ph.value='';ph.textContent='Valitse kasvi...';ph.disabled=true;ph.selected=true;
    sel.appendChild(ph);
    (Array.isArray(d)?d:[]).forEach(p=>{
      const o=document.createElement('option');o.value=p.id;o.textContent=p.name;
      sel.appendChild(o);
    });
  }).catch(()=>msg('Kasvilistaa ei saatu','err'));
}
function savePlant(){
  const id=$('plantSel').value;
  if(!id)return;
  apiPost('/api/config',{plant_id:id}).then(d=>{
    if(d.ok){msg('Kasvi valittu','ok');refresh();}
    else msg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>{});
}
function saveMethod(m){
  apiPost('/api/config',{grow_start_method:m}).then(d=>{
    if(d.ok){msg('Aloitustapa valittu','ok');
      document.querySelectorAll('.choice').forEach(c=>c.classList.toggle('sel',+c.dataset.m===m));
      refresh();}
    else msg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>{});
}
// ── Askel 3: PIN ──
function setPin(){
  const p=$('pin').value.trim();
  if(!/^\d{4,8}$/.test(p)){$('w3').textContent='PIN oltava 4-8 numeroa.';return;}
  $('w3').textContent='';
  apiPost('/api/config',{admin_pin:p}).then(d=>{
    if(!d.ok){$('w3').textContent='Virhe: '+(d.error||'tuntematon');return;}
    $('pin').value='';
    msg('PIN asetettu','ok');
    // refresh() on ASYNKRONINEN. Ennen 17.7.2026 tassa luki "refresh();show(4)",
    // jolloin show(4) -> fillSummary() renderoi VANHALLA ST:lla ja yhteenveto
    // sanoi "PIN: Ei kaytossa" vaikka PIN oli juuri tallennettu oikein.
    // Nayton pitaa odottaa tuoretta tilaa, ei kilpailla sen kanssa.
    refresh().then(()=>show(4));
  }).catch(()=>{});
}
// ── Askel 4: yhteenveto + valmis ──
function fillSummary(){
  const s=(ST.onboarding_steps|0);
  $('rWifi').textContent=ST.wifi_sta_connected?('Yhdistetty · '+(ST.wifi_sta_ip||'')):
    ((s&OB_WIFI)?'Tallennettu':'Ei käytössä (PlantMeister-verkko)');
  $('rPlant').textContent=ST.plant_name||ST.current_plant||'--';
  $('rMethod').textContent=(s&OB_METHOD)?(METHODS[ST.grow_start_method|0]||'--'):'--';
  $('rPin').textContent=(s&OB_PIN)?'Asetettu':'Ei käytössä';
  const both=(s&OB_PLANT)&&(s&OB_METHOD);
  $('doneBtn').disabled=!both;
  $('w4').textContent=both?'':'Kasvi ja aloitustapa puuttuvat — palaa askeleeseen 2.';
}
function complete(){
  apiPost('/api/onboarding/complete',{}).then(d=>{
    if(d&&d.ok===false){$('w4').textContent='Virhe: '+(d.error||'kuittaus epäonnistui');return;}
    ['sec1','sec2','sec3','sec4'].forEach(id=>$(id).style.display='none');
    $('sProg').textContent='Käyttöönotto valmis';
    $('sBack').style.display='none';
    $('secDone').style.display='';
  }).catch(()=>{});
}
// init
refresh().then(()=>{loadPlants();});
show(1);
setInterval(refresh,5000);
</script>
</body>
)rawliteral";

#endif // WIFI_PORTAL_HTML_SETUP_H
