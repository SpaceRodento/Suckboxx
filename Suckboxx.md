Suckbox                     9.8.2026
-----------


Vapaamuotoinen spec:

- Niin, tarkoitus mitata kolmen eri anturin alipainepulsseja ja jotenkin suodattaa ne niin, että saadaan kunkin anturin keskiarvo tai maksimi tai mistä lie saakaan puhtaimman käyrän ilman kaikenmaailman harmonisia kertautumia, kun se imuputki varmaan soi kuin urkupilli. Sit vertaillaan kolmea kanavaa ja samalla lasketaan mikä on taajuus pulsseja minuutissa
- saadaan säätäessä pelkästä imusarjan sykkeestä tarkka kierrosluku
    - Tässä laitteen potentiaalia mietin, niin siitähän saa ulos myös kierrosluvun ja kierrosten tasaisuudellekin jonkun +/- toleranssin.
    - Aina kun yksi sylinteri imaisee kerran, nelitahtimoottori on pyörähtänyt kaksi kierrosta
    - Koska normaalit pelkä alipainemittarit maksaa vähintään 50€ ja ne ei kerro rpm
    - Rpm yritetään pitää jossain 800-900 välissä muistaakseni
    - Paineelle ei ole optimia, vain tasapainoa haetaan
- Maalataan se sateekaariväreillä ja se on Gay Carb Suck Box
- 
-----------

Suunnitelmaa luotu Geminin avulla:

# Perämoottorin digitaalinen alipainemittaus ja synkronointijärjestelmä
Tämä raportti kokoaa yhteen konseptin, jossa kolmisylinterisen perämoottorin kaasuttimet (tai kaasuläpät) synkronoidaan sähköisesti kolmen alipaineanturin avulla. Raportti sisältää tarkan toteutusehdotuksen yksinkertaisimmalle "Minimum Viable Product" (MVP) -versiolle sekä katsauksen siihen, mihin muihin suuntiin laitteistoa voidaan tulevaisuudessa kääntää (pivotoida).

1. Perustoteutus (MVP): Langaton synkronointityökalu älypuhelimeen
Yksinkertaisimmillaan järjestelmä on huoltotyökalu. Laite kytketään moottorin imusarjoihin, se luo oman Wi-Fi-verkon ja näyttää reaaliaikaiset alipainearvot mekaanikon älypuhelimessa selaimen kautta.

Tarvittava laitteisto
Aivot: ESP32-kehityskortti (esim. NodeMCU ESP32).

Anturit: 3 kpl analogisia alipaine/MAP-antureita (esim. edulliset autokäyttöön tarkoitetut 1 Bar MAP-anturit tai NXP MPX -sarja).

Signaalin sovitus: Autojen MAP-anturit antavat usein ulos 0–5V jännitettä, mutta ESP32:n analogiapinnit (ADC) kestävät vain 3,3V. Jokaiseen anturin signaalijohtoon tarvitaan yksinkertainen kahden vastuksen jännitteenjakaja (esim. 10kΩ ja 20kΩ) signaalin skaalaamiseksi turvalliselle tasolle.

Virtalähde: 12V -> 5V Step-Down (Buck) -muunnin (esim. LM2596-moduuli), jolla otetaan virta suoraan veneen akusta ESP32:n 5V/VIN-pinniin.

Ohjelmistoarkkitehtuuri (C++ / Arduino IDE)
Toteutus on täysin itsenäinen, eikä se vaadi ulkoista internet-yhteyttä tai erillistä mobiilisovellusta.

Wi-Fi Access Point: ESP32 ohjelmoidaan luomaan oma avoin tai salasanasuojattu Wi-Fi-verkko (esim. SSID: Moottori_Sync).

Web-palvelin: ESP32:ssa pyörii kevyt asynkroninen web-palvelin (esim. ESPAsyncWebServer -kirjasto). Kun käyttäjä liittyy verkkoon ja avaa selaimessa osoitteen 192.168.4.1, ESP32 lähettää puhelimeen pienen HTML/CSS/JS-tiedoston.

Datan luku ja välitys: ESP32:n pääloopissa luetaan kolmen ADC-pinnin jännitearvot jatkuvasti. Nämä luvut tarjoillaan JSON-muodossa (esim. {"cyl1": 45, "cyl2": 44, "cyl3": 45}) web-palvelimen rajapinnasta.

Reaaliaikaisuus: Nopein ja kevyin tapa siirtää data selaimeen ilman pätkimistä on käyttää WebSocketia tai Server-Sent Events (SSE) -tekniikkaa, jotka päivittävät arvot selaimeen 10–20 kertaa sekunnissa.

Käyttöliittymä (Frontend puhelimessa)
Puhelimen selaimeen latautuu tummalla teemalla (näkyy paremmin ulkona) varustettu sivu.

Visuaalinen ulkoasu: Sivulla on kolme rinnakkaista, leveää pystypalkkia (bar graph).

Toiminta: JavaScript kuuntelee ESP32:n lähettämää dataa ja muuttaa palkkien CSS-tyylin height-arvoa (esim. 0% - 100%) datan perusteella.

Käyttökokemus: Kun moottori on käynnissä, mekaanikko näkee kolme elävää pylvästä. Kun kaikkien pylväiden yläreunat pomppivat täsmälleen samalla tasolla, kaasuttimet ovat synkronoitu.

2. Jatkokehityssuunnat (Pivotit)
Kun alkuperäinen laitteisto (ESP32 ja kolme alipaineanturia) on saatu toimimaan, sama fyysinen asennus voidaan ohjelmistoa muuttamalla tai laajentamalla pivotoida täysin uusiin käyttötarkoituksiin.

Pivot A: Älykäs diagnostiikka ja Dataloggari
Huoltotyökalun sijaan laitteesta tehdään pitkäaikaisen tiedonkeruun työkalu moottorin sisäisen terveyden seurantaan.

Miten se toimii: Ohjelmistoa muutetaan siten, että se tunnistaa alipainepulssien huiput ja laaksot erittäin nopealla näytteenotolla. Näistä pulsseista ohjelmisto laskee moottorin tarkan kierrosluvun (RPM).

Uusi arvo: Lisäämällä ESP32:een MicroSD-kortinlukija, laite tallentaa kaikki käyntihäiriöt (esim. jos yhden sylinterin imupaine romahtaa rasituksessa) aikaleimalla muistikortille. Vianhaku helpottuu merkittävästi.

Pivot B: Moottorinohjauksen (ECU) esikäsittelijä EFI-konversiossa
Jos moottoriin halutaan myöhemmin asentaa moderni elektroninen polttoaineen ruiskutus (EFI, esim. Speeduino), erillisillä kaasuläpillä (ITB) varustetun moottorin alipainesignaali on tyypillisesti liian kaoottinen suoraan ECUlle syötettäväksi.

Miten se toimii: ESP32 toimii signaalin "puhdistajana". Se lukee kaikkien kolmen sylinterin alipaineen, yhdistää ne ohjelmallisesti (esim. liikkuvalla keskiarvolla tai lukemalla vain imutahdin pohjalukeman) ja luo synteettisen, täysin tasaisen digitaalisen (tai DACin kautta analogisen) MAP-signaalin.

Uusi arvo: Mahdollistaa ruiskutusjärjestelmän asentamisen moottoriin, jota olisi muuten äärimmäisen vaikea saada säätöihin nykivän alipaineen vuoksi.

Pivot C: Kiinteä NMEA 2000 -mittaristo ajokäyttöön
Huoltokäytöstä siirrytään kiinteään veneasennukseen. Laitteesta tulee "näkymätön" osa veneen elektroniikkaverkkoa.

Miten se toimii: Ohjelmiston web-palvelin poistetaan. Laitteistoon lisätään halpa CAN-lähetin (esim. SN65HVD230). ESP32 ohjelmoidaan muuttamaan alipaineesta laskettu kuormitustieto ja RPM standardinmukaisiksi NMEA 2000 -viesteiksi.

Uusi arvo: Kännykkää ei enää tarvita. Kun laite kytketään veneen NMEA 2000 -verkkoon, veneen oma kallis karttaplotteri tunnistaa laitteen "moottorina" ja alkaa automaattisesti piirtää tyylikkäitä kierrosluku- ja kuormitusmittareita plotterin ruudulle.