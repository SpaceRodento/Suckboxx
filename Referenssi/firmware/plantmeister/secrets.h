/*=====================================================================
  secrets.h — LOCAL ONLY, gitignored. Do not commit.

  Defines secret credentials. Included automatically by config.h.
=====================================================================*/

#ifndef SECRETS_H
#define SECRETS_H

// OTA firmware update credentials (HTTP Basic Auth at /update)
// 2026-05-21: testikäyttöön admin/admin, vaihdetaan ennen tuotantoa
#define OTA_USERNAME              "admin"
#define OTA_PASSWORD              "admin"

// WiFi STA -credit kehityskäyttöön. Yliajaa config.h:n tyhjat defaultit
// (ifndef-guardit config.h:n riveilla 472-484).
// Naita kaytetaan kun LittleFS on tyhja (esim. heti `pio run -t erase`:n
// jalkeen) tai kun config.json ei sisalla wifi-credit. Portaalin kautta
// asetetut credit (tallennettu config.jsoniin) menevat naiden ohi.
#define WIFI_STA_SSID_DEFAULT        "Ankkalinna"
#define WIFI_STA_PASSWORD_DEFAULT    "akuankka"
#define WIFI_STA_AUTOCONNECT_DEFAULT true

#endif // SECRETS_H
