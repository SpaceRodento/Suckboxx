/*=====================================================================
  eink_user_art.h - User Editable E-ink Artwork Overlay

  Draws a custom monochrome bitmap in the right-side art area when
  ENABLE_EINK_USER_ART is true in config.h.
=====================================================================*/

#ifndef EINK_USER_ART_H
#define EINK_USER_ART_H

#include "config.h"
#include "artwork/generated/eink_user_art_bitmap.h"

static void drawUserArtOverlay(int areaX, int areaY, int areaW, int areaH) {
#if ENABLE_EINK_USER_ART
  int drawX = areaX + (areaW - EINK_USER_ART_WIDTH) / 2;
  int drawY = areaY + (areaH - EINK_USER_ART_HEIGHT) / 2;
  if (drawX < areaX) drawX = areaX;
  if (drawY < areaY) drawY = areaY;

  display.drawBitmap(drawX, drawY,
                     EINK_USER_ART_BITMAP,
                     EINK_USER_ART_WIDTH,
                     EINK_USER_ART_HEIGHT,
                     GxEPD_BLACK);
#else
  (void)areaX;
  (void)areaY;
  (void)areaW;
  (void)areaH;
#endif
}

#endif // EINK_USER_ART_H
