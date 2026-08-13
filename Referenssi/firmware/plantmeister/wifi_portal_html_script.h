/*=====================================================================
  wifi_portal_html_script.h - Portal client-side JavaScript

  Extracted from wifi_portal_html.h. JS only, no markup.
=====================================================================*/

#ifndef WIFI_PORTAL_HTML_SCRIPT_H
#define WIFI_PORTAL_HTML_SCRIPT_H

static const char PORTAL_HTML_SCRIPT[] PROGMEM = R"rawliteral(
<script>
let S={};
let G={};
let failCount=0;
let activeTab=0;
let lastHistoryFetchMs=0;
let msgTimer=null;
let authRequired=false;
let gDevMode=false;           // Huolto-valilehden nakyvyys (/api/config dev_mode)
let devTapCount=0;            // 5x naputus "Online"-kenttaan -> devMode toggle
let devTapTimer=null;
let authAuthorized=true;
let portalDataLoaded=false;
let wifiScanPollTimer=null;
let wifiSelectedSsid='';
let currentPhaseIndex=0;

const HISTORY_REFRESH_MS=30000;
const CHART_FIELDS={
  air_temp:{label:'Lampotila',unit:'C'},
  air_humidity:{label:'Kosteus',unit:'%'},
  plant_height_mm:{label:'Kasvin korkeus',unit:'mm'},
  battery_voltage:{label:'Akku',unit:'V'}
};

function parseNumberValue(value, fallback){
  const n=Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function showAuthModal(show){
  const m=$('authModal');
  if(!m) return;
  m.classList.toggle('show',!!show);
}

function apiHandleResponse(r){
  if(r.status===401){
    authAuthorized=false;
    showAuthModal(true);
    throw new Error('unauthorized');
  }
  return r.json();
}

function apiGet(url){
  return fetch(url,{credentials:'same-origin'}).then(apiHandleResponse);
}

function apiPost(url,body){
  return fetch(url,{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body||{})}).then(apiHandleResponse);
}

function authLogin(){
  const pin=$('authPin').value.trim();
  if(!/^\d{4,8}$/.test(pin)){
    showMsg('PIN oltava 4-8 numeroa','err');
    return;
  }
  apiPost('/api/auth/login',{pin:pin}).then(d=>{
    if(!d.ok){
      showMsg('Virhe: '+(d.error||'kirjautuminen epaonnistui'),'err');
      return;
    }
    $('authPin').value='';
    authAuthorized=true;
    portalDataLoaded=false;
    showAuthModal(false);
    showMsg('Kirjautuminen onnistui','ok');
    update();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function wifiSignalText(rssi){
  if(typeof rssi!=='number') return '--';
  if(rssi>=-55) return 'erinomainen';
  if(rssi>=-67) return 'hyva';
  if(rssi>=-75) return 'kohtalainen';
  return 'heikko';
}

function renderWifiList(networks){
  const root=$('wifiList');
  if(!root) return;
  const arr=Array.isArray(networks)?networks:[];
  if(arr.length===0){
    root.innerHTML='<div class="chart-empty">Verkkoja ei loytynyt</div>';
    return;
  }
  root.innerHTML='';
  arr.forEach(n=>{
    const ssid=String(n.ssid||'');
    if(!ssid) return;
    const row=document.createElement('div');
    row.className='wifi-item'+((wifiSelectedSsid===ssid)?' active':'');
    row.onclick=()=>{
      wifiSelectedSsid=ssid;
      $('wifiSsid').value=ssid;
      renderWifiList(arr);
    };
    row.innerHTML='<div><div class="wifi-name">'+esc(ssid)+'</div><div class="wifi-meta">Ch '+(n.channel||'-')+
      ' | '+(n.rssi||'-')+' dBm ('+wifiSignalText(n.rssi)+')</div></div>';
    root.appendChild(row);
  });
}

function wifiScanPoll(){
  apiGet('/api/wifi/scan').then(d=>{
    if(d.status==='scanning'){
      if($('wifiList')) $('wifiList').innerHTML='<div class="chart-empty">Skannataan...</div>';
      wifiScanPollTimer=setTimeout(wifiScanPoll,2000);
      return;
    }
    renderWifiList(d.networks||[]);
  }).catch(()=>showMsg('WiFi-skannaus epaonnistui','err'));
}

function wifiScan(){
  if(wifiScanPollTimer){
    clearTimeout(wifiScanPollTimer);
    wifiScanPollTimer=null;
  }
  if($('wifiList')) $('wifiList').innerHTML='<div class="chart-empty">Skannataan...</div>';
  wifiScanPoll();
}

// Tyhja salaisuuskentta tarkoittaa "ala muuta" — avainta ei laheteta
// lainkaan. Poisto on oma tahallinen ele (rasti), ei sivuvaikutus siita
// etta kentta sattui olemaan tyhja. Ennen 16.7.2026 kentta esitaytettiin
// laitteen maskilla ja tallennus lahetti sen takaisin salasanaksi, mika
// pudotti laitteen kotiverkosta ilman etta kayttaja koski kenttaan.
function wifiSave(){
  const ssid=($('wifiSsid').value||'').trim();
  const pw=$('wifiPw').value||'';
  const pin=($('adminPin').value||'').trim();
  const pwClear=!!($('wifiPwClear')&&$('wifiPwClear').checked);
  const pinClear=!!($('adminPinClear')&&$('adminPinClear').checked);
  if(ssid.length>31){
    showMsg('SSID max 31 merkkia','err');
    return;
  }
  if(pw.length>0 && pw.length<8){
    showMsg('WiFi-salasana oltava vahintaan 8 merkkia','err');
    return;
  }
  if(pin!=='' && !/^\d{4,8}$/.test(pin)){
    showMsg('PIN oltava 4-8 numeroa','err');
    return;
  }
  if(pwClear && pw.length>0){
    showMsg('Valitse joko uusi salasana tai poisto, ei molempia','err');
    return;
  }
  if(pinClear && pin!==''){
    showMsg('Valitse joko uusi PIN tai poisto, ei molempia','err');
    return;
  }

  const body={
    wifi_ssid:ssid,
    wifi_auto_connect:$('wifiAutoConnect').checked?1:0
  };
  if(pw.length>0)      body.wifi_password=pw;
  else if(pwClear)     body.wifi_password='';
  if(pin!=='')         body.admin_pin=pin;
  else if(pinClear)    body.admin_pin='';

  apiPost('/api/config',body).then(d=>{
    if(!d.ok){
      showMsg('Virhe: '+(d.error||'tuntematon'),'err');
      return;
    }
    showMsg('WiFi/PIN tallennettu','ok');
    loadConfig();
    update();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function showTab(n){
  activeTab=n;
  document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',i===n));
  document.querySelectorAll('.panel').forEach((p,i)=>p.classList.toggle('active',i===n));
  if(n===0) refreshHistoryChart(true);
}

// Huolto-valilehden nakyvyys. Oletus piilossa (tuotanto); paljastetaan
// 5x naputuksella "Online"-kenttaan (devTap). Jos Huolto piilotetaan
// kayttajan ollessa silla valilehdella, siirry Kotiin.
function applyDevMode(on){
  gDevMode=!!on;
  const t=$('tabHuolto');
  if(t) t.style.display=gDevMode?'':'none';
  if(!gDevMode && activeTab===3) showTab(0);
}

// 5x naputus 2 s ikkunassa vaihtaa devModen. POST /api/config {dev_mode:!nyt};
// laite persistoi ja palauttaa uuden tilan, jonka applyDevMode nayttaa.
function devTap(){
  devTapCount++;
  if(devTapTimer) clearTimeout(devTapTimer);
  devTapTimer=setTimeout(()=>{devTapCount=0;},2000);
  if(devTapCount>=5){
    devTapCount=0; clearTimeout(devTapTimer);
    const next=!gDevMode;
    apiPost('/api/config',{dev_mode:next}).then(d=>{
      if(d&&d.ok!==false){
        applyDevMode(next);
        showMsg(next?'Huolto-valilehti nakyviin':'Huolto-valilehti piiloon','ok');
      } else showMsg('Virhe: '+((d&&d.error)||'tuntematon'),'err');
    }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
  }
}

function $(id){return document.getElementById(id)}

function setConnStatus(ok,text){
  $('statusDot').className='dot '+(ok?'ok':'err');
  $('statusTxt').textContent=text;
}

function showMsg(text,type){
  const box=$('uiMsg');
  if(!box) return;
  box.className='msg show '+(type||'info');
  box.textContent=text;
  if(msgTimer) clearTimeout(msgTimer);
  msgTimer=setTimeout(()=>{box.className='msg';box.textContent='';},3600);
}

function formatSince(ms){
  if(typeof ms!=='number' || ms<0) return '--';
  const s=Math.floor(ms/1000);
  if(s<60) return s+' s sitten';
  const m=Math.floor(s/60);
  if(m<60) return m+' min sitten';
  const h=Math.floor(m/60);
  return h+' h sitten';
}

function ebbClass(stateName){
  if(stateName==='IDLE') return 'ok';
  if(stateName==='FAULT') return 'err';
  return 'warn';
}

function drawChart(containerId,data,label,unit){
  const root=$(containerId);
  if(!root) return;

  const pts=(Array.isArray(data)?data:[]).map(p=>({
    timestamp:Number(p.timestamp),
    value:parseFloat(p.value)
  })).filter(p=>!Number.isNaN(p.timestamp)&&!Number.isNaN(p.value));

  if(pts.length<2){
    root.innerHTML='<div class="chart-empty">Ei tarpeeksi historiaa (min 2 pistettä)</div>';
    return;
  }

  const W=320,H=140,PAD=24;
  const values=pts.map(p=>p.value);
  const minV=Math.min(...values);
  const maxV=Math.max(...values);
  const range=maxV-minV||1;
  const points=pts.map((p,i)=>{
    const x=PAD+(i/(pts.length-1))*(W-PAD*2);
    const y=H-PAD-((p.value-minV)/range)*(H-PAD*2);
    return x.toFixed(1)+','+y.toFixed(1);
  }).join(' ');

  const tsMin=pts[0].timestamp;
  const tsMax=pts[pts.length-1].timestamp;
  const spanMin=Math.max(0,Math.round((tsMax-tsMin)/60000));

  root.innerHTML=''
    +'<svg viewBox="0 0 '+W+' '+H+'" style="width:100%;max-width:'+W+'px;display:block">'
    +'<text x="'+PAD+'" y="12" fill="#8899aa" font-size="10">'+label+' ('+unit+')</text>'
    +'<line x1="'+PAD+'" y1="'+(H-PAD)+'" x2="'+(W-PAD)+'" y2="'+(H-PAD)+'" stroke="#334455"/>'
    +'<line x1="'+PAD+'" y1="'+PAD+'" x2="'+PAD+'" y2="'+(H-PAD)+'" stroke="#334455"/>'
    +'<text x="2" y="'+(PAD+4)+'" fill="#667788" font-size="9">'+maxV.toFixed(1)+'</text>'
    +'<text x="2" y="'+(H-PAD)+'" fill="#667788" font-size="9">'+minV.toFixed(1)+'</text>'
    +'<text x="'+PAD+'" y="'+(H-6)+'" fill="#667788" font-size="9">'+spanMin+' min sitten</text>'
    +'<text x="'+(W-PAD-14)+'" y="'+(H-6)+'" fill="#667788" font-size="9">nyt</text>'
    +'<polyline points="'+points+'" fill="none" stroke="#4cd97b" stroke-width="2"/>'
    +'</svg>';
}

function refreshHistoryChart(force){
  if(activeTab!==0 && !force) return;
  const now=Date.now();
  if(!force && now-lastHistoryFetchMs<HISTORY_REFRESH_MS) return;

  const sel=$('histField');
  const field=(sel&&sel.value)||'air_temp';
  const meta=CHART_FIELDS[field]||{label:field,unit:''};
  lastHistoryFetchMs=now;

  apiGet('/api/history?field='+encodeURIComponent(field)+'&max=60').then(data=>{
    drawChart('histChart',data,meta.label,meta.unit);
    const arr=Array.isArray(data)?data:[];
    if(arr.length===0){
      $('histMeta').textContent='Ei mittauspisteitä.';
      return;
    }
    const lastTs=Number(arr[arr.length-1].timestamp);
    const age=(typeof S.uptime_ms==='number' && !Number.isNaN(lastTs)) ? Math.max(0,S.uptime_ms-lastTs) : NaN;
    $('histMeta').textContent='Pisteita: '+arr.length+' | Viimeisin: '+(Number.isNaN(age)?'--':formatSince(age));
  }).catch(()=>{
    $('histChart').innerHTML='<div class="chart-empty">Historia ei saatavilla</div>';
    $('histMeta').textContent='Yhteysvirhe historian haussa';
  });
}

function update(){
  apiGet('/api/status').then(d=>{
    S=d;
    failCount=0;
    setConnStatus(true,'Online');
    authRequired=!!d.auth_required;
    authAuthorized=!!d.auth_authorized;

    $('airTemp').textContent=d.env_valid?d.air_temp.toFixed(1)+' C':'--';
    $('airHum').textContent=d.env_valid?d.air_humidity.toFixed(0)+' %':'--';
    $('airPres').textContent=d.env_valid?d.air_pressure.toFixed(0)+' hPa':'--';
    $('plantH').textContent=d.height_valid?d.plant_height+' mm':'--';
    $('motorH').textContent=d.motor_pos+' mm';
    $('lightSt').textContent=d.lights_on?'Paalla':'Pois';
    $('lightSt').className='val '+(d.lights_on?'ok':'');
    $('waterT').textContent=d.water_temp_valid?d.water_temp.toFixed(1)+' C':'--';
    $('tds').textContent=d.tds_valid?d.tds_ppm+' PPM':'--';
    $('waterLvl').textContent=d.water_ok?'OK':'Matala!';
    $('waterLvl').className='val '+(d.water_ok?'ok':'err');
    $('waterOvf').textContent=d.water_overflow?'YLIVUOTO!':'OK';
    $('waterOvf').className='val '+(d.water_overflow?'err':'ok');
    $('airPump').textContent=d.air_pump_on?'Paalla':'Pois';
    $('airPump').className='val '+(d.air_pump_on?'ok':'');

    if($('ppfdLive')){
      $('ppfdLive').textContent=d.ppfd_valid
        ? (d.ppfd.toFixed(2)+' µmol/m²/s, DLI '+((typeof d.dli==='number')?d.dli.toFixed(2):'--')+' mol/vrk')
        : '--';
    }
    if($('powerLive')){
      $('powerLive').textContent=d.power_valid ? (d.power_current_ma.toFixed(1)+' mA') : '--';
    }

    if($('pumpOvfLatch')){
      const latched=!!d.pump_overflow_latched;
      $('pumpOvfLatch').textContent=latched?'LUKITTU (kuittaa kun valuma loppu)':'vapaa';
      $('pumpOvfLatch').className='val '+(latched?'err':'ok');
    }
    $('batt').textContent=d.battery_v.toFixed(1)+'V ('+d.battery_pct+'%)';
    $('lora').textContent=d.lora_ready?d.lora_rssi+'dBm':'Ei yhteyttä';
    $('lora').className='val '+(d.lora_ready?'ok':'err');
    $('uptime').textContent=d.uptime;
    $('apTime').textContent=d.ap_remaining;

    $('sta-status').textContent=d.wifi_sta_connected?'Yhdistetty':'Ei yhteyttä';
    $('sta-status').className='val '+(d.wifi_sta_connected?'ok':'err');
    $('sta-ip').textContent=d.wifi_sta_ip||'-';
    $('sta-rssi').textContent=(typeof d.wifi_sta_rssi==='number')?(d.wifi_sta_rssi+' dBm'):'-';

    if(typeof d.ebb_state!=='undefined'){
      const stateName=d.ebb_state_name||String(d.ebb_state);
      const faultActive=!!d.ebb_fault;
      $('ebb-card').style.display='';
      $('ebb-state').textContent=stateName;
      $('ebb-state').className='val '+ebbClass(stateName);
      $('ebb-fault').textContent=faultActive?(d.ebb_fault_name||('Koodi '+(d.ebb_fault_code||0))):'Ei vikaa';
      $('ebb-fault').className='val '+(faultActive?'err':'ok');
      const cycleAge=(typeof d.uptime_ms==='number'&&typeof d.ebb_last_cycle_ms==='number')
        ? formatSince(Math.max(0,d.uptime_ms-d.ebb_last_cycle_ms))
        : '--';
      $('ebb-cycle').textContent=cycleAge;
      if($('ebb-effective-interval')&&typeof d.ebb_effective_interval_min!=='undefined'){
        const ei=d.ebb_effective_interval_min;
        $('ebb-effective-interval').textContent=ei>0?(ei+' min'):'pois';
        $('ebb-effective-interval').className='val '+(ei>0?'ok':'');
      }
      if($('ebb-flood-reason')&&typeof d.ebb_flood_reason!=='undefined'){
        $('ebb-flood-reason').textContent=d.ebb_flood_reason;
      }
      if($('ebb-grow')){
        const ga=!!d.grow_active;
        const ph=d.grow_phase_name||'';
        $('ebb-grow').textContent=ga?('Aktiivinen'+(ph?(' — '+ph):'')):'Ei käynnissä';
        $('ebb-grow').className='val '+(ga?'ok':'warn');
      }
      if($('ebb-next-cycle')){
        const ns=d.ebb_next_cycle_sec;
        if(typeof ns!=='number'||ns<0){
          $('ebb-next-cycle').textContent='—';$('ebb-next-cycle').className='val';
        } else if(ns===0){
          $('ebb-next-cycle').textContent='nyt';$('ebb-next-cycle').className='val ok';
        } else {
          $('ebb-next-cycle').textContent='~'+formatSince(ns*1000);
          $('ebb-next-cycle').className='val ok';
        }
      }
      if($('ebb-circulate')){
        const ca=!!d.circulate_active, ce=!!d.ebb_circulate_enabled, cs=d.ebb_next_circulate_sec;
        if(ca){ $('ebb-circulate').textContent='Aktiivinen';$('ebb-circulate').className='val ok'; }
        else if(!ce){ $('ebb-circulate').textContent='pois';$('ebb-circulate').className='val'; }
        else if(typeof cs!=='number'||cs<0){ $('ebb-circulate').textContent='—';$('ebb-circulate').className='val'; }
        else if(cs===0){ $('ebb-circulate').textContent='nyt';$('ebb-circulate').className='val ok'; }
        else { $('ebb-circulate').textContent='~'+formatSince(cs*1000);$('ebb-circulate').className='val ok'; }
      }
      $('ebb-ack-btn').style.display=faultActive?'':'none';
      if($('ebb-tune-card')) $('ebb-tune-card').style.display='';
      if($('ebb-calib-card')){
        $('ebb-calib-card').style.display='';
        const cp=d.ebb_calib_phase|0;
        $('ebbCalibPhase').textContent=
          cp===1?'Täyttö käynnissä — merkitse TÄYSI':
          cp===2?'Tyhjenee — merkitse TYHJÄ':'Vapaa (ei kalibrointia)';
        $('ebbCalibPhase').className='val '+(cp?'ok':'');
      }
      if($('soak-calib-card')){
        $('soak-calib-card').style.display='';
        const sa=!!d.soak_calib_active;
        const sd=(typeof d.pump_duty_pct==='number')?d.pump_duty_pct:0;
        if($('soakPhase')){
          $('soakPhase').textContent=sa?('Aktiivinen — '+sd+' %'):'Ei kalibrointia';
          $('soakPhase').className='val '+(sa?'ok':'');
        }
        if($('soakDuty')) $('soakDuty').textContent=sd+' %';
      }
    }

    if($('motor-pos')) $('motor-pos').textContent=(typeof d.motor_pos==='number')?d.motor_pos:0;
    if($('motor-target')) $('motor-target').textContent=(typeof d.motor_target_mm==='number')?d.motor_target_mm:0;
    if($('calMotorPos')){
      const mm=(typeof d.motor_pos==='number')?d.motor_pos:'?';
      const steps=(typeof d.motor_pos_steps==='number')?d.motor_pos_steps:'?';
      $('calMotorPos').textContent=mm+' mm ('+steps+' stepit)';
    }

    if($('tstSt')){
      $('tstSt').textContent=(d.lights_on?'Valo ON':'Valo OFF')+(d.air_pump_on?' ILP ON':' ILP OFF');
      $('tstMo').textContent=d.motor_pos+'mm'+(d.motor_moving?' liikkeessa':'');
      $('tstPu').textContent=d.pump_running?'KÄYNNISSÄ':'Pois';
      $('tstPu').className='val '+(d.pump_running?'warn':'');
    }

    if($('tstDev')){
      const stName=d.device_state_name||'?';
      const isFault=(d.device_state===5);
      $('tstDev').textContent=stName+(d.device_faults?(' (faults=0x'+d.device_faults.toString(16)+')'):'');
      $('tstDev').className='val '+(isFault?'err':(stName==='IDLE'?'ok':''));
      if($('tstClearFaultBtn')) $('tstClearFaultBtn').style.display=isFault?'':'none';
    }

    // Toimintopalkki: yksi tilavetoinen ensisijainen toiminto. Korvaa
    // vanhat faultBanner/obBanner/readyCard-erilliselementit.
    renderActionBar(d);

    if($('modeName')){
      const tm=!!d.test_mode;
      $('modeName').textContent=tm?'Test':'Normal';
      $('modeName').style.color=tm?'#f39c12':'#4cd97b';
      $('modeAp').textContent=tm?'Pysyy pystyssa ikuisesti':'Sammuu 15 min jalkeen';
    }

    if(authRequired && !authAuthorized){
      showAuthModal(true);
      portalDataLoaded=false;
      return;
    }

    showAuthModal(false);
    if(!portalDataLoaded){
      portalDataLoaded=true;
      loadPlants();
      loadConfig();
      refreshHistoryChart(true);
    }

    refreshHistoryChart(false);
    loadGrow();
    loadGrowSim();
  }).catch(()=>{
    failCount++;
    setConnStatus(false,'Ei yhteyttä ('+failCount+')');
  });
}

function esc(s){
  return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

// Oletusnakymat hakevat AINA simulate=0: kun kasvatus ei ole kaynnissa,
// nakymat sanovat sen suoraan eivatka keksi vaihetta (rautatesti 16.7.2026:
// portal_pickSimulatedPhase-demo selasi 56 vrk ohjelmaa 15 s valein ja
// kayttaja luki sen neuvoksi). Simulaatio nakyy vain Huollon API-testi-
// kortissa (loadGrowSim), aina SIMULAATIO-leimalla.
// PE-tavoite vs. nykyarvo (docs/kehitys/pe-ohjausmalli.md V1-naytto).
// Tavoite tulee aktiivivaiheesta (g = /api/grow), nykyarvo antureilta
// (S = /api/status). Puuttuva/epavalidi anturi -> "--", EI nolla joka
// nayttaisi mitatulta. Vari: vihrea = alueella/tavoitteessa, keltainen =
// ulkona, harmaa = ei mittausta. Laite ei viela AJA naita — vain nayttaa.
function petFmt(id,cur,valid,unit,tgt,ok){
  const el=$(id); if(!el) return;
  const c=(valid&&typeof cur==='number')?(Math.round(cur*10)/10+' '+unit):'--';
  el.innerHTML=c+' <span style="color:#8899aa">/ '+tgt+'</span>';
  el.style.color=!valid?'#8899aa':(ok?'#4cd97b':'#f39c12');
}
function renderPETargets(g){
  const card=$('peCard'); if(!card) return;
  const active=!!(g&&g.grow_active&&g.phase_count>0);
  card.style.display=active?'':'none';
  if(!active) return;
  const s=S||{};
  // DLI: kertyy paivan mittaan, joten "alle tavoitteen" on normaalia — ei
  // varoiteta vajaasta, vain nollasta paivan lopussa (V2 hoitaa arvion).
  const dliV=(typeof s.dli==='number'), dliT=g.phase_dli_target_mol||0;
  petFmt('peDli',s.dli,dliV,'mol',dliT+' mol/vrk',true);
  const ppfdV=!!s.ppfd_valid, ppfdT=g.phase_ppfd_target_umol||0;
  petFmt('pePpfd',s.ppfd,ppfdV,'µmol',ppfdT+' µmol/s',ppfdV&&s.ppfd>=ppfdT*0.7);
  const vpdV=!!s.vpd_valid, vpdMin=g.phase_vpd_min_kpa||0, vpdMax=g.phase_vpd_max_kpa||0;
  petFmt('peVpd',s.vpd_kpa,vpdV,'kPa',vpdMin+'–'+vpdMax+' kPa',vpdV&&s.vpd_kpa>=vpdMin&&s.vpd_kpa<=vpdMax);
  const co2V=!!s.air_co2_valid, co2T=g.phase_co2_target_ppm||0;
  petFmt('peCo2',s.co2_ppm,co2V,'ppm',co2T+' ppm',co2V&&s.co2_ppm>=co2T*0.9);
  const leafV=!!s.leaf_temp_valid, leafT=g.phase_leaf_temp_max_c||0;
  petFmt('peLeaf',s.leaf_temp_c,leafV,'°C','<'+leafT+' °C',leafV&&s.leaf_temp_c<leafT);
}
function loadGrow(){
  apiGet('/api/grow?simulate=0').then(g=>{
    G=g;
    renderHomeGrow(g);
    if(!$('ggMode')) return;
    if(!g.ok || !g.enabled){
      $('ggMode').textContent='Pois käytöstä';
      $('ggPhase').textContent='--';
      $('ggDay').textContent='--';
      $('ggRemain').textContent='--';
      $('ggInstr').textContent=g.error||'Guided Growing ei ole käytössä';
      $('ggTimeline').textContent='Ei vaihedataa';
      return;
    }

    const active=!!g.grow_active;
    const st=g.step||{};
    const stepActive=active&&!!st.active;
    $('ggMode').textContent=active?'Aktiivinen':'Ei käynnissä';
    $('ggMode').className='val '+(active?'ok':'warn');
    // Vaihetiedot vain kun kasvatus on oikeasti kaynnissa — muuten '—',
    // ei config-jaanteita jotka nayttaisivat vaiheelta (H2/H3).
    // Kun alitehtava on kesken, kehysta vaiheotsikko idatyksena/juurtumisena:
    // "Taimivaihe, 14 pv" oli harhaanjohtava kun kyse on vasta siemenista
    // kuvun alla (rautatesti 16.7.2026). Suffiksi tulee aloitustavasta.
    let phaseTxt=(active&&g.phase_count>0)?((g.phase_index+1)+'. '+(g.phase_label||'--')):'—';
    if(stepActive) phaseTxt+=((g.grow_start_method|0)===0?' (juurtuminen käynnissä)':' (idätys käynnissä)');
    $('ggPhase').textContent=phaseTxt;
    // Vaihepaiva: kun alitehtava kesken, ALA nayta koko vaiheen ikaa (se
    // laskee kylvopaivasta ja nayttaisi taimen iaksi jo idatysvaiheessa).
    $('ggDay').textContent=stepActive?'Alitehtävä kesken':(active?((g.phase_elapsed_days||0)+' pv'):'—');
    $('ggRemain').textContent=!active?'—':((g.phase_days_left===-1)?'Ei määritetty':(g.phase_days_left+' pv'));
    $('ggInstr').textContent=active?(g.phase_guidance||g.instruction||'Ei vaihekohtaista ohjetta')
                                   :'Kasvatus ei ole käynnissä. Aloita se sivun yläreunan napista tai laitteen napista.';
    // Pysayta nakyy VAIN kun on jotain pysaytettavaa. Ennen 17.7.2026 iso
    // punainen "Pysayta kasvatus" oli aina nakyvissa, myos kortin lukiessa
    // "Ei kaynnissa" — kayttaja: "epaloogista ja toimii huonosti".
    if($('ggStopBtn')) $('ggStopBtn').style.display=active?'':'none';
    renderPETargets(g);
    renderGrowStep(g);

    // Aikajana: NOW-korostus vain aktiivisessa kasvatuksessa; muuten
    // ohjelma esitetaan neutraalina esittelyna.
    //
    // Vaiheet ovat kahden polun UNIONI, eivat yksi jono: "Juurtuminen" on
    // PISTOKKAAN polku (leikattu varsi kasvattaa juuret) ja "Taimivaihe"
    // sisaltaa idatyksen (structs.h: "Seeds germinate"). Aloitustapa valitsee
    // sisaanmenon (input_router.h), joten siemenesta aloittavalle Juurtuminen
    // ei koske lainkaan. Ennen 17.7.2026 se nakyi silti listan karjessa —
    // kayttaja luki siita etta idatys puuttuu ja jarjestys on vaarin.
    const START_TYPE=['juurtuminen','taimivaihe','kasvuvaihe'];
    const startType=START_TYPE[(g.grow_start_method|0)]||'juurtuminen';
    let startIdx=(g.phases||[]).findIndex(p=>p.type===startType);
    if(startIdx<0) startIdx=0;
    const phases=(g.phases||[]).map((ph,i)=>{
      const skipped=i<startIdx;
      const cur=active&&ph.is_current;
      const classes='phase-item'+(cur?' current':'');
      const chips=cur?'<span class="chip chip-current">NOW</span>':'';
      const dur=ph.duration_days===0?'jatkuva':(ph.duration_days+' pv');
      // ph.type on sama sana kuin ph.label ("Juurtuminen (juurtuminen, 7 pv)")
      // -> naytetaan vain label. Ei toistoa.
      const style=skipped?' style="opacity:.45"':'';
      const note=skipped?' <span class="chip">ei tällä aloitustavalla</span>':'';
      return '<div class="'+classes+'"'+style+'>'+esc(ph.label)+' — '+dur+note+chips+'</div>';
    });
    $('ggTimeline').innerHTML=phases.length?phases.join('<br>'):'Ei vaiheita';

    // Huolto-valilehden "Kasvatuksen tilan saato" -kortti. Taytetaan samasta
    // /api/grow-datasta: vaihe-pudotusvalikko + askelvihje + tilamuistutus.
    // Kortti on aina nakyvissa (dev-tyokalu), mutta saato vaikuttaa vain kun
    // kasvatus on kaynnissa (router: "wrong state") — siksi eksplisiittinen note.
    const setStateNote=$('setStateNote');
    if(setStateNote){
      setStateNote.textContent=active?'Käynnissä — säädöt vaikuttavat':'Ei käynnissä — säädöt eivät vaikuta';
      setStateNote.className='val '+(active?'ok':'warn');
    }
    const setPhaseSel=$('setPhaseSel');
    if(setPhaseSel){
      const prev=setPhaseSel.value;
      setPhaseSel.innerHTML='';
      (g.phases||[]).forEach(ph=>{
        const o=document.createElement('option');
        o.value=ph.index;
        o.textContent=(ph.index+1)+'. '+ph.label+(ph.is_current?' (nyt)':'');
        setPhaseSel.appendChild(o);
      });
      // Sailyta aiempi valinta jos yha olemassa, muuten osoita nykyiseen vaiheeseen.
      if(prev!==''&&setPhaseSel.querySelector('option[value="'+prev+'"]')) setPhaseSel.value=prev;
      else if(active) setPhaseSel.value=g.phase_index;
    }
    const setStepHint=$('setStepHint');
    if(setStepHint){
      const sc=(g.step&&g.step.count)?g.step.count:0;
      setStepHint.textContent=sc>0
        ?('Aktiivinen askel: '+((g.step.index|0)+1)+'/'+sc+' (syötä 0..'+(sc-1)+')')
        :'Ei aktiivista askelsarjaa tässä vaiheessa';
    }
  }).catch(()=>{
    if($('ggMode')) $('ggMode').textContent='Ei yhteyttä';
  });
}

// Kesto vrk/h/min-muodossa (BUTTON-askeleen kulunut aika).
function fmtDurDH(sec){
  sec=Math.max(0,sec|0);
  const d=Math.floor(sec/86400),h=Math.floor((sec%86400)/3600),m=Math.floor((sec%3600)/60);
  if(d>0) return d+' vrk '+h+' h';
  if(h>0) return h+' h '+m+' min';
  return m+' min';
}

// Aktiivinen alitehtava (grow_steps.h/grow_step_fsm.h) -> #ggStepCard.
// TIMER-askel: alaslaskuri. BUTTON-askel jolla timer: kulunut aika.
// Valmis-nappi vain kun step.ack_allowed (BUTTON-askel, kuittaus etenee).
function renderGrowStep(g){
  const card=$('ggStepCard'); if(!card) return;
  const st=g&&g.step;
  if(!g||!g.grow_active||!st||!st.active){ card.style.display='none'; return; }
  card.style.display='';
  $('ggStepTitle').textContent='Alitehtävä '+((st.index|0)+1)+'/'+(st.count|0);
  $('ggStepText').textContent=st.text||'';
  const timer=$('ggStepTimer'),lbl=$('ggStepTimerLbl');
  if(typeof st.remaining_sec==='number' && st.remaining_sec>=0){
    lbl.textContent='Aikaa jäljellä';
    timer.textContent='noin '+Math.ceil(st.remaining_sec/60)+' min jäljellä';
    timer.className='val';
  } else if((st.timer_min|0)>0){
    lbl.textContent='Kulunut';
    timer.textContent=fmtDurDH(st.elapsed_sec|0);
    timer.className='val'+(st.awaiting_user?' warn':'');
  } else {
    lbl.textContent='Tila';
    timer.textContent=st.awaiting_user?'Odottaa sinua':'Käynnissä';
    timer.className='val'+(st.awaiting_user?' warn':'');
  }
  $('ggStepAckBtn').style.display=st.ack_allowed?'':'none';
}

// Kodin kasvatuskortti: sama /api/grow-data ihmisen kielella.
function renderHomeGrow(g){
  if(!$('hgState')) return;
  if(!g||!g.ok||!g.enabled){
    $('hgState').textContent='--';
    $('hgDay').textContent='';
    $('hgGuid').textContent='--';
    return;
  }
  if(g.grow_active){
    $('hgState').textContent=(g.plant_name||'Kasvatus')+' — '+(g.phase_label||'käynnissä');
    $('hgDay').textContent='Vaihepäivä '+(g.phase_elapsed_days||0)
      +((g.phase_days_left===-1)?'':(' · jäljellä '+g.phase_days_left+' pv'));
    $('hgGuid').textContent=g.phase_guidance||g.instruction||'';
  } else {
    $('hgState').textContent='Ei kasvatusta käynnissä';
    $('hgDay').textContent='';
    $('hgGuid').textContent='Laite lepää valmiustilassa. Aloita kasvatus yläpalkista tai laitteen napista.';
  }
}

// Huollon API-testikortti on koko UI:n ainoa simulate=1-kuluttaja.
// Leima kertoo aina kun naytetty vaihe on simuloitu.
function loadGrowSim(){
  if(activeTab!==3) return;
  apiGet('/api/grow?simulate=1').then(g=>{
    if(!$('tGrowMode')) return;
    if(!g.ok||!g.enabled){
      $('tGrowMode').textContent='Pois käytöstä';
      return;
    }
    $('tGrowMode').textContent=(g.grow_active?'Aktiivinen':'Ei käynnissä')+(g.simulated?' (SIMULAATIO)':'');
    $('tGrowMode').className='val '+(g.grow_active?'ok':'warn');
    $('tGrowPhase').textContent=(g.phase_count>0)?((g.phase_index+1)+'. '+(g.phase_label||'--')):'Ei vaiheita';
    if(g.grow_pending_advance){
      $('tGrowPending').textContent='KYLLÄ';
      $('tGrowPending').className='val warn';
    } else {
      $('tGrowPending').textContent='EI';
      $('tGrowPending').className='val';
    }
    if(g.sim_override_active){
      $('tGrowOverride').textContent='Vaihe '+(g.sim_override_phase+1)+', pv '+(g.sim_override_day||1);
      $('tGrowOverride').className='val warn';
    } else {
      $('tGrowOverride').textContent='Ei';
      $('tGrowOverride').className='val';
    }
    if($('tGrowStartMethod')) $('tGrowStartMethod').value=String(g.grow_start_method||0);
  }).catch(()=>{});
}

function growPost(path, body){
  return apiPost(path,body);
}

function handleGrowResult(d, successMessage, after){
  if(!d.ok){
    showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    return;
  }
  if(successMessage) showMsg(successMessage,'ok');
  if(after) after();
}

// Kasvatuksen kaynnistys — koko UI:n AINOA kaynnistyspolku (toimintopalkin
// "Aloita kasvatus" + Huollon GROW_START API-testinappi growStartFrom()).
// Aloitustapa luetaan configista, jotta kaynnistys ei voi ylikirjoittaa
// Kasvatus-nakymassa tehtya valintaa (rautatestin H1/H6 juurisyy oli
// juuri kaksi erillista valikko+nappi-paria samalle paatokselle).
function startGrow(){
  growPost('/api/grow/start',{start_method:+(S.grow_start_method||0)}).then(d=>{
    handleGrowResult(d,'Kasvatus käynnistetty',update);
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function loadCalibration(){
  apiGet('/api/calib').then(d=>{
    $('calMotorDown').value=parseInt(d.motor_steps_down||0,10);
    $('calMotorUp').value=parseInt(d.motor_steps_up||0,10);
    if($('ppfdFactor')){
      $('ppfdFactor').textContent=(typeof d.ppfd_calibration_factor==='number')
        ? d.ppfd_calibration_factor.toFixed(6)+(d.ppfd_calibration_factor===1?' (kalibroimaton)':'') : '--';
    }
    if($('powerCalFactor')){
      $('powerCalFactor').textContent=(typeof d.power_cal_factor==='number')
        ? ('shunt '+(d.power_shunt_ohms||'?')+' Ω, kerroin '+d.power_cal_factor.toFixed(4)) : '--';
    }
  }).catch(()=>showMsg('Kalibroinnin haku epäonnistui','err'));
}

function saveCalibration(){
  apiPost('/api/calib',{
    motor_steps_down:parseInt($('calMotorDown').value,10),
    motor_steps_up:parseInt($('calMotorUp').value,10)
  }).then(d=>{
    if(d.ok) showMsg('Kalibrointi tallennettu','ok'); else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    loadCalibration();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function calibProbe(dir){
  apiPost('/api/calib/motor/probe_'+dir,{}).then(d=>{
    if(d.ok) showMsg('Moottori probe '+dir,'ok'); else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function ppfdCal(){
  const v=parseFloat($('ppfdRef').value);
  if(!(v>0)){ showMsg('Anna referenssi-PPFD (>0 µmol/m²/s)','err'); return; }
  apiPost('/api/calib/ppfd/save',{referencePpfdUmol:v}).then(d=>{
    if(d.ok) showMsg('PPFD-kalibrointi tallennettu (kerroin '+d.newFactor.toFixed(6)+')','ok');
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    loadCalibration();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function ppfdReset(){
  apiPost('/api/calib/ppfd/reset',{}).then(d=>{
    if(d.ok) showMsg('PPFD-kerroin nollattu (1.0, suhteellinen)','ok');
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    loadCalibration();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function powerCal(){
  const v=parseFloat($('powerRefMa').value);
  if(!(v>0)){ showMsg('Anna referenssivirta (>0 mA)','err'); return; }
  apiPost('/api/calib/power/apply',{i_ref_ma:v}).then(d=>{
    if(d.ok) showMsg('Virtakalibrointi tallennettu (kerroin '+d.new_factor.toFixed(4)+')','ok');
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    loadCalibration();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function powerReset(){
  apiPost('/api/calib/power/reset',{}).then(d=>{
    if(d.ok) showMsg('Virtakalibrointikerroin nollattu (1.0)','ok');
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    loadCalibration();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function calibCaptureLimit(side){
  apiPost('/api/calib/motor/capture_limit',{side:side}).then(d=>{
    if(!d.ok){ showMsg('Virhe: '+(d.error||'tuntematon'),'err'); return; }
    showMsg((side==='up'?'Yläraja':'Alaraja')+' kaapattu: '+d.steps+' stepit','ok');
    loadCalibration();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function growStartFrom(selectId){
  const sel = $(selectId);
  const method = sel ? +sel.value : 0;
  growPost('/api/grow/start',{start_method:method}).then(d=>{
    handleGrowResult(d,'Kasvatus käynnistetty',update);
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function growAction(action){
  growPost('/api/grow/'+action,{}).then(d=>{
    handleGrowResult(d,'Toiminto '+action+' suoritettu',update);
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// Huolto: kasvatuksen tilan suora saato. Reititys /api/command -> intent
// (doCmd nayttaa OK/virhe + kutsuu update():n joka refreshaa /api/grow:n).
// Nama vaikuttavat OIKEAAN kasvatukseen; kortti on Huolto-valilehdella.
function setGrowPhase(){
  const sel=$('setPhaseSel');
  if(!sel||sel.value===''){ showMsg('Ei vaihetta valittuna','err'); return; }
  doCmd('GROW_PHASE_SET',String(sel.value));
}
// Selaa vaihetta yksi taakse/eteen. Kohdeindeksi lasketaan viimeksi ladatusta
// /api/grow-datasta (G) ja clampataan rajoihin. GROW_NEXT palvelisi eteenpain,
// mutta symmetria (sama komento molempiin) pitaa logiikan yksinkertaisena.
function stepGrowPhase(delta){
  if(!G||!G.grow_active){ showMsg('Kasvatus ei käynnissä','err'); return; }
  const cur=G.phase_index|0, cnt=G.phase_count|0;
  const idx=cur+delta;
  if(idx<0){ showMsg('Jo ensimmäisessä vaiheessa','info'); return; }
  if(idx>=cnt){ showMsg('Jo viimeisessä vaiheessa','info'); return; }
  doCmd('GROW_PHASE_SET',String(idx));
}
function setGrowDay(){
  const v=$('setDayIn');
  if(!v||v.value===''){ showMsg('Anna vaihepäivä','err'); return; }
  doCmd('GROW_DAY_SET',String(v.value|0));
}
function setGrowStep(){
  const v=$('setStepIn');
  if(!v||v.value===''){ showMsg('Anna askelindeksi','err'); return; }
  doCmd('GROW_STEP_SET',String(v.value|0));
}

function loadPlants(){
  apiGet('/api/plants').then(d=>{
    let sel=$('plantSel');sel.innerHTML='';
    d.forEach(p=>{let o=document.createElement('option');
      o.value=p.id;o.textContent=p.name+' ('+p.id+')';
      if(p.id===S.current_plant)o.selected=true;
      sel.appendChild(o)});
    if(sel.options.length>0) loadPlant();
  }).catch(()=>showMsg('Kasvilistaa ei saatu haettua','err'));
}

// Kasvivalinta: TALLENNA + lataa parametrit.
//
// Ennen 16.7.2026 dropdownin onchange kutsui pelkkaa loadPlant():ia, joka vain
// taytti lomakkeen — valittu kasvi tallentui vasta Asetukset-tabin "Tallenna
// asetukset" -napista (saveConfig lahettaa plant_id:n). Kayttoonottobanneri
// ohjaa Kasvi-tabiin, joten ummikko valitsi kasvin eika valinta mennyt minnekaan.
// Sama kuvio kuin saveStartMethod():lla: valinta tallentuu heti kun se tehdaan.
function selectPlant(){
  const id=$('plantSel').value;
  loadPlant();
  apiPost('/api/config',{plant_id:id}).then(d=>{
    if(d.ok){ showMsg('Kasvi valittu','ok'); update(); }
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function loadPlant(){
  let id=$('plantSel').value;
  apiGet('/api/plant?id='+encodeURIComponent(id)).then(p=>{
    $('pHeight').value=p.max_height_mm;
    $('pLight').value=p.light_hours;
    $('pWater').value=p.water_ml_per_dose;
    $('pInterval').value=p.water_interval_hours;
    // pTds/pTmin/pTmax poistettu 18.7.2026 — PE-tavoitteet ovat per vaihe.
    if($('plantDirty')) $('plantDirty').style.display='none';
    const phCard=$('phase-editor-card');
    const phases=Array.isArray(p.phases)?p.phases:[];
    if(phCard){
      if(phases.length>0){
        phCard.style.display='';
        const ph=phases[0];
        currentPhaseIndex=ph.index||0;
        if($('phaseEditorLabel')) $('phaseEditorLabel').textContent=ph.label||'Vaihe 1';
        if($('phLightHours')) $('phLightHours').value=ph.light_hours||0;
        if($('phFloodIntervalMin')) $('phFloodIntervalMin').value=ph.flood_interval_min||0;
        if($('phFloodDurationSec')) $('phFloodDurationSec').value=ph.flood_duration_sec||0;
        // PE-tavoitteet (docs/kehitys/pe-ohjausmalli.md)
        if($('phDli')) $('phDli').value=ph.dli_target_mol||0;
        if($('phPpfd')) $('phPpfd').value=ph.ppfd_target_umol||0;
        if($('phVpdMin')) $('phVpdMin').value=ph.vpd_min_kpa||0;
        if($('phVpdMax')) $('phVpdMax').value=ph.vpd_max_kpa||0;
        if($('phCo2')) $('phCo2').value=ph.co2_target_ppm||0;
        if($('phLeafMax')) $('phLeafMax').value=ph.leaf_temp_max_c||0;
        if($('phDurationDays')) $('phDurationDays').value=ph.duration_days||0;
      } else {
        phCard.style.display='none';
      }
    }
  }).catch(()=>showMsg('Kasviparametrien haku epäonnistui','err'));
}

function savePhase(){
  const plantId=$('plantSel').value;
  const body={plant_id:plantId,phase_index:currentPhaseIndex,
    light_hours:+$('phLightHours').value,
    flood_interval_min:+$('phFloodIntervalMin').value,
    flood_duration_sec:+$('phFloodDurationSec').value,
    dli_target_mol:+$('phDli').value,
    ppfd_target_umol:+$('phPpfd').value,
    vpd_min_kpa:+$('phVpdMin').value,
    vpd_max_kpa:+$('phVpdMax').value,
    co2_target_ppm:+$('phCo2').value,
    leaf_temp_max_c:+$('phLeafMax').value,
    duration_days:+$('phDurationDays').value};
  apiPost('/api/plant/phase',body).then(d=>{
    if(d.ok) showMsg('Vaihe tallennettu','ok');
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function savePlant(){
  let body={id:$('plantSel').value,
    max_height_mm:+$('pHeight').value,light_hours:+$('pLight').value,
    water_ml_per_dose:+$('pWater').value,water_interval_hours:+$('pInterval').value};
  // PE-tavoitteet tallentuvat per vaihe (savePhase), eivat kasvitasolla.
  apiPost('/api/plant',body).then(d=>{
      if(d.ok){ showMsg('Kasvi tallennettu','ok'); if($('plantDirty')) $('plantDirty').style.display='none'; }
      else showMsg('Virhe: '+d.error,'err')
    }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// Näytä "muokattu"-merkki kun kasvuparametreja muutetaan (ennen tallennusta).
function markPlantDirty(){
  if($('plantDirty')) $('plantDirty').style.display='';
}

// Palauta valitun kasvin parametrit + vaiheet tehdasoletuksiin.
function resetPlantDefaults(){
  const id=$('plantSel').value;
  if(!confirm('Palautetaanko kasvin oletusarvot? Tämä korvaa nykyiset asetukset ja vaiheet.')) return;
  apiPost('/api/plant/reset',{id:id}).then(d=>{
    if(d.ok){ showMsg('Oletusarvot palautettu','ok'); loadPlant(); loadGrow(); }
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// Asetusten tallennusmalli: muutos tallentuu heti kun sen tekee (onchange,
// yksi avain kerrallaan). Korvaa sivutason "Tallenna asetukset" -napin —
// rautatestin H4: kaksi tallennusnappia samalla sivulla hammensi ("painan
// ainoaa oikeaa nappia"). Ainoa jaljelle jaava nappi on salaisuuskortin
// wifiSave, koska salaisuuksien lahetys on tahallinen ele.
function saveNum(key,id){
  const el=$(id);
  if(!el) return;
  const body={};
  body[key]=+el.value;
  apiPost('/api/config',body).then(d=>{
    if(d.ok) showMsg('Tallennettu','ok'); else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// §8.1: per-nakyman "Palauta oletukset" — autotallennuksen paluutie.
// JS tietaa vain AVAIMET (mitka kentat kuuluvat nakymaan); oletusARVOT
// tulevat aina laitteesta (config_reset.h + config_setDefaults), joten
// oletuksia ei ole kirjoitettu kahteen paikkaan. confirm() riittaa:
// palautus koskee vain yhta nakymaa eika hukkaa mitaan jota ei voi
// asettaa uudelleen (ei ole FACTORY_RESET).
function resetView(keys,label){
  if(!confirm('Palautetaanko '+label+' tehdasoletuksiin? Koskee vain tätä näkymää.')) return;
  apiPost('/api/config/reset',{keys:keys}).then(d=>{
    if(d.ok){
      showMsg('Oletukset palautettu','ok');
      loadConfig();
      update();
    } else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}
function resetSettingsView(){
  resetView(['light_on_hour','lora_address','lora_network_id','lora_target'],
    'Asetukset-näkymän kentät');
}
function resetMaintView(){
  resetView(['button_action','ebb_flood_interval_min','ebb_flood_duration_sec',
    'ebb_soak_duration_sec','ebb_soak_pwm_pct','ebb_drain_timeout_sec',
    'ebb_overflow_auto_clear','ebb_circulate_enabled','ebb_circulate_interval_min',
    'ebb_circulate_duration_sec','ebb_circulate_duty_pct'],
    'Huollon säädöt (napin toiminto + ebb-ajat)');
}

function saveButtonAction(){
  apiPost('/api/config',{button_action:+$('btnAction').value}).then(d=>{
    if(d.ok) showMsg('Napin toiminto tallennettu','ok'); else showMsg('Virhe: '+(d.error||'tuntematon'),'err')
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// Aloitustapa: nappi lukee taman configista kun kasvatus aloitetaan, joten se
// on asetettava ENNEN aloitusta. Vaikuttaa aloitusvaiheeseen ja ohjeteksteihin.
function saveStartMethod(){
  apiPost('/api/config',{grow_start_method:+$('growStartMethod').value}).then(d=>{
    if(d.ok) showMsg('Aloitustapa tallennettu','ok'); else showMsg('Virhe: '+(d.error||'tuntematon'),'err')
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// Show whether live timing overrides are active (any > 0) vs. falling back to
// the grow-phase / config.h defaults. Makes the "0 = oletus" rule visible so the
// operator knows at a glance which set of timings is actually driving flooding.
function updateEbbOverrideBanner(){
  const el=$('ebbOverrideBanner'); if(!el) return;
  const iv=+($('ebbInterval').value||0), fd=+($('ebbFloodDur').value||0),
        sk=+($('ebbSoak').value||0), dr=+($('ebbDrain').value||0);
  const active=(iv>0||fd>0||sk>0||dr>0);
  el.style.display='';
  if(active){
    el.textContent='⚠ Testiasetukset voimassa — live-arvot ohittavat kasvuvaiheen/config.h-oletukset. Nollaa kentat (0) palataksesi oletuksiin.';
    el.style.background='#3a2e10'; el.style.color='#f39c12';
  } else {
    el.textContent='✓ Oletusasetukset voimassa (kasvuvaihe / config.h). Anna arvo > 0 ohittaaksesi oletuksen.';
    el.style.background='#13301f'; el.style.color='#4cd97b';
  }
}

function saveEbbTiming(){
  let body={ebb_flood_interval_min:+$('ebbInterval').value,
    ebb_flood_duration_sec:+$('ebbFloodDur').value,
    ebb_soak_duration_sec:+$('ebbSoak').value,
    ebb_soak_pwm_pct:+$('ebbSoakPwm').value,
    ebb_drain_timeout_sec:+$('ebbDrain').value,
    ebb_overflow_auto_clear:$('ebbAutoClear').checked,
    ebb_circulate_enabled:$('ebbCirculate').checked,
    ebb_circulate_interval_min:+$('ebbCircInterval').value,
    ebb_circulate_duration_sec:+$('ebbCircDur').value,
    ebb_circulate_duty_pct:+$('ebbCircDuty').value};
  apiPost('/api/config',body).then(d=>{
    if(d.ok){ showMsg('Ebb-ajat tallennettu (0 = oletus)','ok'); updateEbbOverrideBanner(); } else showMsg('Virhe: '+(d.error||'tuntematon'),'err')
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function saveAutoClear(){
  apiPost('/api/config',{ebb_overflow_auto_clear:$('ebbAutoClear').checked}).then(d=>{
    if(d.ok) showMsg('Automaattikuittaus '+($('ebbAutoClear').checked?'PÄÄLLÄ':'pois'),'ok'); else showMsg('Virhe: '+(d.error||'tuntematon'),'err')
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function ebbCalib(step){
  const labels={fill_start:'Täyttö aloitettu',capture_full:'TÄYSI merkitty',
    capture_empty:'TYHJÄ merkitty',cancel:'Kalibrointi keskeytetty'};
  apiPost('/api/calib/ebb/'+step,{}).then(d=>{
    if(d.ok){
      let extra='';
      if(typeof d.flood_duration_sec==='number') extra=' — täyttö '+d.flood_duration_sec+' s';
      if(typeof d.drain_timeout_sec==='number') extra=' — tyhjennys '+d.drain_timeout_sec+' s';
      showMsg((labels[step]||'OK')+extra,'ok');
      if(typeof loadConfig==='function') loadConfig();
    } else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    update();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// ── Soak-pidätys (jatkuva PWM-taso) ──────────────────────────────
function soakCalib(step){
  let body={};
  if(step==='save'){ const dv=+$('soakDur').value; if(dv>0) body.duration_sec=dv; }
  apiPost('/api/calib/soak/'+step,body).then(d=>{
    if(d.ok){
      if(step==='save'){
        showMsg('Soak tallennettu: '+d.soak_pwm_pct+' % / '+d.soak_duration_sec+' s','ok');
        if(typeof loadConfig==='function') loadConfig();
      } else if(step==='cancel'){
        showMsg('Soak-kalibrointi keskeytetty','ok');
      } else {
        showMsg('Soak: '+(step==='start'?'täyttö käynnissä':'pidetään')+' ('+(d.duty_pct||0)+' %)','ok');
      }
      if(typeof d.duty_pct==='number' && $('soakDutyIn')) $('soakDutyIn').value=d.duty_pct;
    } else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    update();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// delta !== 0 → suhteellinen (+/-), delta === 0 → absoluuttinen numerokentästä
function soakAdjust(delta){
  const body=(delta===0)?{pct:+$('soakDutyIn').value}:{delta:delta};
  apiPost('/api/calib/soak/adjust',body).then(d=>{
    if(d.ok){ if($('soakDutyIn')) $('soakDutyIn').value=d.duty_pct; }
    else showMsg('Virhe: '+(d.error||'tuntematon'),'err');
    update();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function doCmd(cmd,val){
  const label=cmd+(val?(' '+val):'');
  if($('tstLast')) $('tstLast').textContent=label+' (lahetetty...)';
  apiPost('/api/command',{cmd:cmd,value:val||''}).then(d=>{
      if(!d.ok){
        showMsg('Virhe: '+(d.error||'tuntematon'),'err');
        if($('tstLast')){
          $('tstLast').textContent=label+' — VIRHE: '+(d.error||'?');
          $('tstLast').className='val err';
        }
      } else {
        if($('tstLast')){
          $('tstLast').textContent=label+' — OK ('+new Date().toLocaleTimeString()+')';
          $('tstLast').className='val ok';
        }
      }
      update();
    }).catch(()=>{
      showMsg('Virhe: ei yhteytta','err');
      if($('tstLast')){
        $('tstLast').textContent=label+' — yhteysvirhe';
        $('tstLast').className='val err';
      }
    });
}

// ── Ylivuotosuojan kuittaus ──────────────────────────────────────
function pumpClearOverflow(){
  fetch('/api/test/pump_clear_overflow',{method:'POST',credentials:'same-origin'})
    .then(apiHandleResponse).then(d=>{
      if(d.ok) showMsg('Ylivuotosalpa kuitattu','ok');
      else showMsg('Virhe: '+(d.error||'?'),'err');
      update();
    }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

// ── Toimintopalkki ──
// Renderoi /api/status "primary_action" -kentasta. Prioriteetit paatetaan
// laitepaassa (portal_primary_action.h, testattu test_portal_primary_action)
// — tama on vain naytto, sama tyonjako kuin onboarding-portilla. Avaimet
// pidettava synkassa firmware-resolverin portal_primaryActionName():n kanssa.
// Kayttoonotto ei nay taalla lainkaan: /setup-sivu omistaa sen polun.
const START_METHODS=['pistokkaasta','siemenestä','valmiista taimesta'];

function abarBtnShow(label,fn){
  const b=$('abarBtn');
  if(!b) return;
  b.textContent=label;
  b.onclick=fn;
  b.style.display='';
}

function faultLabel(d){
  const f=d.device_faults||0;
  const parts=[];
  if(f&0x01) parts.push('self-test');
  if(f&0x02) parts.push('sensori');
  if(f&0x04) parts.push('moottori');
  if(f&0x08) parts.push('vesi');
  if(f&0x10) parts.push('LoRa');
  if(f&0x20) parts.push('brownout');
  if(f&0x80) parts.push('tuntematon');
  return parts.length?parts.join(', '):('0x'+f.toString(16));
}

function renderActionBar(d){
  const bar=$('abar');
  if(!bar) return;
  const pa=d.primary_action||'none';
  if($('abarBtn')) $('abarBtn').style.display='none';
  if($('abarGrid')) $('abarGrid').style.display='none';
  let cls='abar', title='', text='';
  if(pa==='clear_fault'){
    cls+=' abar-err';
    title='⚠ Laite vikatilassa';
    text='Vika: '+faultLabel(d)+' — aktuaattorit lukittu kunnes vika tyhjennetään.';
    abarBtnShow('Tyhjennä vika → IDLE',()=>doCmd('CLEAR_FAULT'));
  } else if(pa==='step_ack'){
    cls+=' abar-ok';
    title='Seuraava askel';
    text=d.step_text||'';
    abarBtnShow('Tehty ✓',()=>doCmd('GROW_STEP_ACK'));
  } else if(pa==='approve_phase'){
    cls+=' abar-ok';
    title='Kasvuvaihe valmis';
    text='Siirrytäänkö vaiheeseen '+((G&&G.pending_next_label)||'seuraava')+'?';
    if($('abarGrid')) $('abarGrid').style.display='';
  } else if(pa==='advisory'){
    cls+=' abar-warn';
    title='Huomio';
    text=d.advisory_msg||'Laite toimii, mutta jokin oheislaite vaatii huomiota.';
  } else if(pa==='start_grow'){
    cls+=' abar-ok';
    title='Valmiina kasvatukseen';
    const pname=d.plant_name||d.current_plant||'kasvi';
    const m=START_METHODS[d.grow_start_method|0]||'';
    text=pname+(m?(' · '+m):'')+' — laite ei aloita itsestään.';
    abarBtnShow('Aloita kasvatus',startGrow);
  } else {
    bar.style.display='none';
    return;
  }
  bar.className=cls;
  $('abarTitle').textContent=title;
  $('abarMsg').textContent=text;
  bar.style.display='';
}

function setMode(testMode){
  apiPost('/api/mode',{test_mode:!!testMode}).then(d=>{
    if(!d.ok){ showMsg('Virhe: '+(d.error||'tuntematon'),'err'); return; }
    showMsg(testMode?'Test mode paalla — AP pysyy pystyssa':'Normal mode — AP sammuu 15 min jalkeen','ok');
    update();
  }).catch(()=>showMsg('Virhe: ei yhteytta','err'));
}

function loadConfig(){
  apiGet('/api/config').then(d=>{
    $('cAddr').value=d.lora_address;
    $('cNet').value=d.lora_network_id;
    $('cTarget').value=d.lora_target;
    $('cLightH').value=d.light_on_hour;
    applyDevMode(!!d.dev_mode);   // Huolto-valilehden nakyvyys laitteen tilasta
    $('wifiSsid').value=d.wifi_ssid||'';
    wifiSelectedSsid=d.wifi_ssid||'';
    // Salaisuuksia EI kirjoiteta kenttiin. Aiemmin tassa oli
    // $('wifiPw').value=d.wifi_password — kentta tayttyi laitteen
    // maskilla, ja Tallenna lahetti maskin takaisin salasanaksi.
    // Kentan tila kerrotaan placeholderilla; tyhja kentta tarkoittaa
    // "ala muuta" (ks. wifiSave).
    $('wifiPw').value='';
    $('wifiPw').placeholder=d.wifi_password_set?'Tallennettu — jata tyhjaksi jos et vaihda'
                                               :'Ei salasanaa (avoin verkko)';
    $('wifiAutoConnect').checked=!!d.wifi_auto_connect;
    $('adminPin').value='';
    $('adminPin').placeholder=d.admin_pin_set?'Tallennettu — jata tyhjaksi jos et vaihda'
                                             :'Ei PIN-koodia';
    if($('wifiPwClear')) $('wifiPwClear').checked=false;
    if($('adminPinClear')) $('adminPinClear').checked=false;
    if($('wifiPwClearWrap')) $('wifiPwClearWrap').style.display=d.wifi_password_set?'':'none';
    if($('adminPinClearWrap')) $('adminPinClearWrap').style.display=d.admin_pin_set?'':'none';
    if($('btnAction')&&typeof d.button_action!=='undefined') $('btnAction').value=String(d.button_action);
    if($('growStartMethod')&&typeof d.grow_start_method!=='undefined') $('growStartMethod').value=String(d.grow_start_method);
    if($('ebbInterval')&&typeof d.ebb_flood_interval_min!=='undefined') $('ebbInterval').value=d.ebb_flood_interval_min;
    if($('ebbFloodDur')&&typeof d.ebb_flood_duration_sec!=='undefined') $('ebbFloodDur').value=d.ebb_flood_duration_sec;
    if($('ebbSoak')&&typeof d.ebb_soak_duration_sec!=='undefined') $('ebbSoak').value=d.ebb_soak_duration_sec;
    if($('ebbDrain')&&typeof d.ebb_drain_timeout_sec!=='undefined') $('ebbDrain').value=d.ebb_drain_timeout_sec;
    if($('ebbSoakPwm')&&typeof d.ebb_soak_pwm_pct!=='undefined') $('ebbSoakPwm').value=d.ebb_soak_pwm_pct;
    if($('ebbAutoClear')&&typeof d.ebb_overflow_auto_clear!=='undefined') $('ebbAutoClear').checked=d.ebb_overflow_auto_clear;
    if($('ebbCirculate')&&typeof d.ebb_circulate_enabled!=='undefined') $('ebbCirculate').checked=d.ebb_circulate_enabled;
    if($('ebbCircInterval')&&typeof d.ebb_circulate_interval_min!=='undefined') $('ebbCircInterval').value=d.ebb_circulate_interval_min;
    if($('ebbCircDur')&&typeof d.ebb_circulate_duration_sec!=='undefined') $('ebbCircDur').value=d.ebb_circulate_duration_sec;
    if($('ebbCircDuty')&&typeof d.ebb_circulate_duty_pct!=='undefined') $('ebbCircDuty').value=d.ebb_circulate_duty_pct;
    if($('soakDur')&&d.ebb_soak_duration_sec>0) $('soakDur').value=d.ebb_soak_duration_sec;
    if($('soakDutyIn')&&d.ebb_soak_pwm_pct>0) $('soakDutyIn').value=d.ebb_soak_pwm_pct;
    if(typeof d.wifi_sta_connected!=='undefined'){
      $('sta-status').textContent=d.wifi_sta_connected?'Yhdistetty':'Ei yhteyttä';
      $('sta-status').className='val '+(d.wifi_sta_connected?'ok':'err');
      $('sta-ip').textContent=d.wifi_sta_ip||'-';
      $('sta-rssi').textContent=(typeof d.wifi_sta_rssi==='number')?(d.wifi_sta_rssi+' dBm'):'-';
    }
    updateEbbOverrideBanner();
  }).catch(()=>showMsg('Asetusten haku epäonnistui','err'));
}

if($('authPin')) $('authPin').addEventListener('keydown',e=>{if(e.key==='Enter')authLogin();});
['ebbInterval','ebbFloodDur','ebbSoak','ebbDrain'].forEach(id=>{
  const e=$(id); if(e) e.addEventListener('input',updateEbbOverrideBanner);
});
update();
loadCalibration();
// Adaptiivinen pollaus: Huolto-tabissa 1 s (live-status), muuten 5 s.
let pollTimer=null;
function setupPollTimer(){
  if(pollTimer) clearInterval(pollTimer);
  const interval=(activeTab===3)?1000:5000;
  pollTimer=setInterval(update,interval);
}
setupPollTimer();
const origShowTab=showTab;
showTab=function(n){
  origShowTab(n);
  setupPollTimer();
  if(n===3) loadGrowSim();
};
</script>
)rawliteral";

#endif // WIFI_PORTAL_HTML_SCRIPT_H
