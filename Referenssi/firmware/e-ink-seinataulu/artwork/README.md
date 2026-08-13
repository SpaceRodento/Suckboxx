## E-ink Artwork Workspace

Tama kansio on yksi selkea paikka e-ink-kuvien tekoon, konversioon, previewhin ja firmwareen vientiin.

---

## Mitä tällä voi tehdä

- staattinen user-art overlay oikeaan kuva-alueeseen
- kasvien kasvuanimaatiot (species-specific)
- automaattinen geneerinen fallback-animaatio puuttuville kasveille
- preview-kuvat normaalina ja invert-versiona ennen firmware-buildia

---

## Kansiorakenne

- templates/: piirto- ja layout-pohjat
- source/: staattisen user-artin lahdekuvat
- growth/: kasvuanimaatioiden lahdekuvat
- scripts/: konversiotyokalut
- generated/: generoitu C-headerit ja preview-kuvat

growth-kansio:

- growth/generic/: geneerinen fallback-animaatio
- growth/plants/basil/: basilikan oma animaatio
- growth/plants/<plant_id>/: muut kasvit samalla mallilla

---

## Riippuvuudet

Asenna Python-riippuvuudet kerran:

```bash
pip install pillow
```

---

## A) Staattinen user-art

1. Muokkaa pohjaa [templates/user_art_canvas_240x425.svg](templates/user_art_canvas_240x425.svg).
2. Vie PNG:ksi tiedostoon firmware/e-ink-seinataulu/artwork/source/user_art.png.
3. Generoi header:

```bash
python firmware/e-ink-seinataulu/artwork/scripts/png_to_epd_bitmap.py \
  --input firmware/e-ink-seinataulu/artwork/source/user_art.png \
  --output firmware/e-ink-seinataulu/artwork/generated/eink_user_art_bitmap.h \
  --symbol EINK_USER_ART_BITMAP
```

4. Aseta [../config.h](../config.h#L42) arvoksi ENABLE_EINK_USER_ART true.

---

## B) Kasvuanimaatiot (suositus)

Nopea aloitus (generoi realism-pass v1 -lahdeframet automaattisesti):

```bash
python firmware/e-ink-seinataulu/artwork/scripts/generate_basil_v1_frames.py
```

Generoitavat paketit:

- generic
- basil
- mint
- dill
- parsley
- chives
- cilantro

Frame-nimet:

- stage_00.png
- stage_01.png
- stage_02.png
- stage_03.png
- stage_04.png
- stage_05.png

Polut:

- geneerinen fallback: firmware/e-ink-seinataulu/artwork/growth/generic/
- lajikohtaiset: firmware/e-ink-seinataulu/artwork/growth/plants/<plant_id>/

Generointi:

```bash
python firmware/e-ink-seinataulu/artwork/scripts/build_growth_assets.py \
  --root firmware/e-ink-seinataulu/artwork/growth \
  --output firmware/e-ink-seinataulu/artwork/generated/growth_anim_assets.h \
  --preview-dir firmware/e-ink-seinataulu/artwork/generated/preview \
  --frames 6 \
  --width 152 \
  --height 288 \
  --dither bayer8 \
  --preview-both
```

Tulos:

- [generated/growth_anim_assets.h](generated/growth_anim_assets.h)
- preview-kuvat kansiossa firmware/e-ink-seinataulu/artwork/generated/preview/
- selaimessa avattava preview-sivu: firmware/e-ink-seinataulu/artwork/generated/preview/index.html

Firmware-kytkin:

- Aseta [../config.h](../config.h#L46) arvoksi ENABLE_EINK_GROWTH_ANIMATION true.
- Pida [../config.h](../config.h#L42) arvossa ENABLE_EINK_USER_ART false, jos haluat kasvuanimaation kayttoon.

---

## Konversiotyylit

build_growth_assets.py tukee:

- bayer8 (oletus, selkea retro-rasteri)
- bayer4
- floyd
- none

---

## Luotettava fallback runtime:ssa

Renderointiketju:

1. kasvikohtainen animaatio (esim. basil)
2. geneerinen animaatio
3. ASCII-plant fallback

Jos kasvipaketti puuttuu tai on kesken, naytto pysyy silti toiminnassa.

---

## Vinkit realistiseen retro-ilmeeseen

- piirra ensin selkeat siluetit, lisaa rasteri vasta lopussa
- pidä rungot ja lehdet paksuina e-ink-ystavallisina muotoina
- testaa aina preview normaalina ja invert-versiona
- kayta samaa valon suuntaa kaikissa frameissa, jotta animaatio ei vilku
