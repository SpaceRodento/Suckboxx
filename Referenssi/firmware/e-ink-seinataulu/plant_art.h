/*=====================================================================
  plant_art.h - ASCII Art Plant Visualization

  Draws a proportionally growing plant on the e-ink display.
  Plant takes ~1/3 of the screen width on the right side.
  Growth is proportional to real plant height vs. max height.

  Each plant type has unique ASCII art. The art is defined
  bottom-to-top: index 0 = base stem, last index = crown.
  More rows are revealed as the plant grows.
=====================================================================*/

#ifndef PLANT_ART_H
#define PLANT_ART_H

#include "config.h"

// ═══════════════════════════════════════════════════════════════════
// ART DEFINITIONS — bottom-to-top order, 11 chars wide
// ═══════════════════════════════════════════════════════════════════

// Basil — round bushy clusters
static const char* ART_BASIL[] = {
  "     |     ",   // 0  base stem
  "     |     ",   // 1
  "     |     ",   // 2
  "    \\|/    ",  // 3  lower junction
  "   (   )   ",   // 4  lower leaves
  "  (* * *)  ",   // 5
  "   (   )   ",   // 6
  "    \\|/    ",  // 7  upper junction
  "     |     ",   // 8
  "   (   )   ",   // 9  upper leaves
  "  (* * *)  ",   // 10
  "   ( * )   ",   // 11
  "    .*.    ",   // 12 crown
};
#define ART_BASIL_ROWS 13

// Dill — tall, feathery, umbrella top
static const char* ART_DILL[] = {
  "     |     ",   // 0
  "     |     ",   // 1
  "     |     ",   // 2
  "     |     ",   // 3
  "     |     ",   // 4
  "   ~\\|/~  ",   // 5  lower feathers
  "     |     ",   // 6
  "  ~~\\|/~~ ",   // 7  upper feathers
  "     |     ",   // 8
  "    \\|/   ",   // 9  umbrella
  "   \\|||/  ",   // 10
  "  \\|||||/ ",   // 11
  "   \\|||/  ",   // 12 flower head
};
#define ART_DILL_ROWS 13

// Mint — wide spreading leaves
static const char* ART_MINT[] = {
  "     |     ",   // 0
  "     |     ",   // 1
  "     |     ",   // 2
  "    \\|/    ",  // 3
  "   {   }   ",   // 4  lower leaves
  "  { * * }  ",   // 5
  "   {   }   ",   // 6
  "    \\|/    ",  // 7
  "   {   }   ",   // 8  upper leaves
  "  { * * }  ",   // 9
  "   {   }   ",   // 10
  "    -*-    ",   // 11 flower buds
  "   ~*-*~   ",   // 12
};
#define ART_MINT_ROWS 13

// Chives — thin parallel stalks
static const char* ART_CHIVES[] = {
  "     |     ",   // 0
  "     |     ",   // 1
  "    |||    ",   // 2
  "    |||    ",   // 3
  "   |||||   ",   // 4
  "   |||||   ",   // 5
  "   |||||   ",   // 6
  "   |||||   ",   // 7
  "  |||||||  ",   // 8
  "  |||||||  ",   // 9
  "   |||||   ",   // 10
  "    |||    ",   // 11 tips
  "     |     ",   // 12
};
#define ART_CHIVES_ROWS 13

// Parsley — curly ruffled leaves
static const char* ART_PARSLEY[] = {
  "     |     ",   // 0
  "     |     ",   // 1
  "     |     ",   // 2
  "    \\|/    ",  // 3
  "    {*}    ",   // 4  lower curls
  "   {***}   ",   // 5
  "    {*}    ",   // 6
  "    \\|/    ",  // 7
  "    {*}    ",   // 8  upper curls
  "   {***}   ",   // 9
  "  {*****}  ",   // 10
  "   {***}   ",   // 11
  "    ~*~    ",   // 12 top
};
#define ART_PARSLEY_ROWS 13

// Cilantro — fan-shaped leaves
static const char* ART_CILANTRO[] = {
  "     |     ",   // 0
  "     |     ",   // 1
  "     |     ",   // 2
  "     |     ",   // 3
  "    \\|/    ",  // 4
  "    / \\    ",  // 5  lower fan
  "   / * \\   ",  // 6
  "    \\|/    ",  // 7
  "    / \\    ",  // 8  upper fan
  "   / * \\   ",  // 9
  "   /* *\\   ",  // 10
  "    /*\\    ",  // 11
  "    .*.    ",   // 12 top
};
#define ART_CILANTRO_ROWS 13

// Pot — same for all plants (3 rows)
static const char* ART_POT[] = {
  " \\_______/ ",  // top rim
  " |       | ",   // body
  " |_______| ",   // bottom
};
#define ART_POT_ROWS 3

// ═══════════════════════════════════════════════════════════════════
// PLANT LOOKUP
// ═══════════════════════════════════════════════════════════════════

struct PlantArtDef {
  const char** lines;
  int rowCount;
  const char* name;
  int maxHeightMm;
};

static const PlantArtDef PLANT_ARTS[] = {
  { ART_BASIL,    ART_BASIL_ROWS,    "Basilika",     400 },
  { ART_DILL,     ART_DILL_ROWS,     "Tilli",        500 },
  { ART_MINT,     ART_MINT_ROWS,     "Minttu",       350 },
  { ART_CHIVES,   ART_CHIVES_ROWS,   "Ruohosipuli",  300 },
  { ART_PARSLEY,  ART_PARSLEY_ROWS,  "Persilja",     300 },
  { ART_CILANTRO, ART_CILANTRO_ROWS, "Korianteri",   300 },
};
#define PLANT_ART_COUNT 6

static const char* PLANT_IDS[] = {
  "basil", "dill", "mint", "chives", "parsley", "cilantro"
};

static const PlantArtDef* getPlantArt(const char* plantId) {
  for (int i = 0; i < PLANT_ART_COUNT; i++) {
    if (strcmp(plantId, PLANT_IDS[i]) == 0) {
      return &PLANT_ARTS[i];
    }
  }
  return &PLANT_ARTS[0];   // Default: basil
}

// ═══════════════════════════════════════════════════════════════════
// RENDERING
// ═══════════════════════════════════════════════════════════════════

// Draw a single centered art line at given Y position
static void drawArtLine(int areaX, int areaW, int y, const char* line) {
  int charW = 12;   // textSize(2) = 12px per char
  int linePixels = strlen(line) * charW;
  int offsetX = areaX + (areaW - linePixels) / 2;
  display.setCursor(offsetX, y);
  display.print(line);
}

// Main draw function
// areaX/Y/W/H: bounding rectangle for the plant art
// plantId: species ID ("basil", "dill", etc.)
// motorMm: current lamp height (proxy for plant growth)
// sensorDistMm: VL53L0X distance (smaller = taller plant)
void drawPlantArt(int areaX, int areaY, int areaW, int areaH,
                  const char* plantId, int motorMm, int sensorDistMm) {

  const PlantArtDef* art = getPlantArt(plantId);
  int maxH = art->maxHeightMm;

  // Estimate actual plant height from sensor data
  // Plant height = lamp height - sensor distance
  int plantHeightMm = motorMm - sensorDistMm;
  if (plantHeightMm < 0) plantHeightMm = 0;
  if (sensorDistMm <= 0 && motorMm > 0) plantHeightMm = motorMm;

  float growth = (maxH > 0) ? constrain((float)plantHeightMm / maxH, 0.0f, 1.0f)
                             : 0.0f;

  // If no sensor data, use motor height as rough estimate
  if (motorMm <= 0 && sensorDistMm <= 0) {
    growth = 0.0f;
  }

  display.setFont(NULL);      // Built-in 6x8 monospace
  display.setTextSize(2);     // 12x16 per character
  int ch = 16;                // Character height at textSize(2)

  // Calculate layout
  int potH = ART_POT_ROWS * ch;
  int progressH = ch * 2;    // Space for growth text
  int availH = areaH - potH - progressH - ch;  // Available for plant
  int maxPlantRows = availH / ch;
  if (maxPlantRows > art->rowCount) maxPlantRows = art->rowCount;

  // How many plant rows to show
  int showRows = (int)(maxPlantRows * growth);
  if (showRows < 0) showRows = 0;
  if (growth > 0.01f && showRows == 0) showRows = 1;   // Show sprout

  // ── Draw plant (bottom to top) ────────────────────────────────

  // Start Y: pot is at the bottom of available area
  int potTopY = areaY + areaH - progressH - potH;
  int plantBaseY = potTopY - ch;   // Just above pot

  // Draw plant rows (index 0 = bottom, index N = top)
  for (int i = 0; i < showRows; i++) {
    int screenRow = showRows - 1 - i;   // 0 = topmost, showRows-1 = bottom
    int y = plantBaseY - screenRow * ch;
    if (y < areaY) break;
    drawArtLine(areaX, areaW, y, art->lines[i]);
  }

  // ── Draw pot ──────────────────────────────────────────────────

  for (int i = 0; i < ART_POT_ROWS; i++) {
    drawArtLine(areaX, areaW, potTopY + i * ch, ART_POT[i]);
  }

  // ── Draw growth indicator ─────────────────────────────────────

  int progY = potTopY + potH + 4;

  // "150 / 400 mm"
  char buf[24];
  snprintf(buf, sizeof(buf), "%d / %d mm", plantHeightMm, maxH);
  display.setFont(NULL);
  display.setTextSize(1);     // Smaller text for progress
  int smallCh = 6;
  int textW = strlen(buf) * smallCh;
  display.setCursor(areaX + (areaW - textW) / 2, progY);
  display.print(buf);

  // Simple progress bar
  int barW = areaW - 40;
  int barH = 8;
  int barX = areaX + 20;
  int barY = progY + 12;
  display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
  int fillW = (int)(barW * growth);
  if (fillW > 0) {
    display.fillRect(barX, barY, fillW, barH, GxEPD_BLACK);
  }

  // Percentage
  snprintf(buf, sizeof(buf), "%d%%", (int)(growth * 100));
  textW = strlen(buf) * smallCh;
  display.setCursor(areaX + (areaW - textW) / 2, barY + barH + 4);
  display.print(buf);

  display.setTextSize(2);   // Restore
}

#endif // PLANT_ART_H
