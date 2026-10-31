# S15 — Korjausehdotuksen luonnos ESPAsyncWebServerille (ei lähetetä)

**Laji:** dokumentaatio (tutkimus). **Laitekoodi:** ei.
**Riippuvuus:** R13 on `dev/integraatio`ssa.

## Havainto
`AsyncEventSourceClient::_onDisconnect` nollaa `_client`in ilman lukkoa myös versiossa
3.12.1 (docs/firmware.md §4.9, `hal/web.cpp`:n analyysi). docs/jatko.md:n kahden rivin
ehdotus (`_lockmq` `_onDisconnect`issa) on R13-katselmuksen mukaan vajaa (löydös L4): lukko
pitää vapauttaa ennen `_handleDisconnect`ia, ja `connected()` lukee `_client`in edelleen ilman
lukkoa.

## Tehtävä
Kirjoita `docs/upstream/espasyncwebserver-ondisconnect.md`: kilpatilanteen kuvaus
aikajanana (kaksi taskia, rivinumerot versiosta 3.12.1 tai kirjaston uusimmasta mainista),
toistettavuusarvio, korjausehdotus diffinä ja perustelu, miksi se ei aiheuta lukkiutumista.
Hae lähde: `git clone --depth 1 https://github.com/ESP32Async/ESPAsyncWebServer` (ja tagi
v3.12.1). Jos verkko ei toimi, kirjaa se **Estynyt**-kohtaan. Tarkista, onko kirjastossa jo
avoin issue tai PR samasta aiheesta, ja linkitä se.

**Älä lähetä mitään kirjaston repoon** (Y11). Omistaja päättää lähettämisestä.

## Valmis kun
Luonnos on valmis lähetettäväksi sellaisenaan, ja diffin rivit vastaavat nimettyä versiota.
