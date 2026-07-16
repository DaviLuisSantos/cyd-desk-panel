#pragma once
#include <TFT_eSPI.h>
#include "NotoSansBold15.h"

// ===== Paleta Dracula (convertida pra RGB565) =====
#define DRACULA_BG      0x2946  // #282a36 — fundo
#define DRACULA_CURRENT 0x422B  // #44475a — fundo secundário (tab bar, trilho de barra)
#define DRACULA_FG      0xFFDE  // #f8f8f2 — texto principal
#define DRACULA_COMMENT 0x6394  // #6272a4 — texto apagado/secundário
#define DRACULA_CYAN    0x8F5F  // #8be9fd
#define DRACULA_GREEN   0x57CF  // #50fa7b
#define DRACULA_ORANGE  0xFDCD  // #ffb86c
#define DRACULA_PINK    0xFBD8  // #ff79c6
#define DRACULA_PURPLE  0xBC9F  // #bd93f9
#define DRACULA_RED     0xFAAA  // #ff5555
#define DRACULA_YELLOW  0xF7D1  // #f1fa8c

// ===== Layout =====
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int TAB_BAR_H = 34;
static const int CONTENT_TOP = TAB_BAR_H + 6;

namespace theme {

// ---- Ícones vetoriais (desenhados com primitivas, sem bitmap/filesystem) ----
// Recebem o CENTRO (cx, cy) e um `size` (raio/meia-largura aproximados —
// size=8 é o tamanho clássico usado na tab bar; nas linhas de conteúdo
// usa-se algo maior, tipo 11-12, pra ganhar presença visual).

inline void iconClock(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawCircle(cx, cy, size, color);
    tft.drawLine(cx, cy, cx, cy - size * 5 / 8, color);
    tft.drawLine(cx, cy, cx + size / 2, cy + size / 4, color);
}

inline void iconCpu(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int half = size;
    int pin = size / 2;
    int off = half * 2 / 3;
    tft.drawRect(cx - half, cy - half, half * 2, half * 2, color);
    for (int i = -off; i <= off; i += (off > 0 ? off : 1)) {
        tft.drawFastVLine(cx + i, cy - half - pin, pin, color);
        tft.drawFastVLine(cx + i, cy + half, pin, color);
        tft.drawFastHLine(cx - half - pin, cy + i, pin, color);
        tft.drawFastHLine(cx + half, cy + i, pin, color);
    }
}

inline void iconRam(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int halfW = size;
    int halfH = size * 5 / 8;
    int r = size / 4;
    int off = halfW * 3 / 4;
    int step = halfW / 2;
    tft.drawRoundRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2, r, color);
    for (int i = -off; i <= off; i += (step > 0 ? step : 1)) {
        tft.drawFastVLine(cx + i, cy + halfH, size * 3 / 8, color);
    }
}

inline void iconDisk(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int halfW = size;
    int halfH = size * 7 / 8;
    tft.drawRoundRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2, size * 3 / 8, color);
    tft.drawFastHLine(cx - halfW, cy, halfW * 2, color);
    tft.fillCircle(cx + halfW / 2, cy + size * 3 / 8, (size / 8 > 1 ? size / 8 : 1), color);
}

inline void iconNet(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawCircle(cx, cy + size / 4, size, color);
    tft.drawFastHLine(cx - size, cy + size / 4, size * 2, color);
    tft.drawFastVLine(cx, cy - size * 3 / 4, size * 2, color);
}

inline void iconCoin(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawCircle(cx, cy, size, color);
    tft.drawCircle(cx, cy, size * 3 / 4, color);
    tft.drawFastVLine(cx, cy - size / 2, size + 1, color);
}

inline void iconArrowDown(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.fillTriangle(cx - size, cy - size, cx + size, cy - size, cx, cy + size, color);
}

inline void iconArrowUp(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.fillTriangle(cx - size, cy + size, cx + size, cy + size, cx, cy - size, color);
}

// ---- Widgets ----

// Barra de progresso com cantos arredondados e bordas suavizadas (anti-aliased)
// sobre trilho DRACULA_CURRENT.
inline void drawBar(TFT_eSPI& tft, int x, int y, int w, int h, float pct, uint16_t fillColor) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    tft.fillSmoothRoundRect(x, y, w, h, h / 2, DRACULA_CURRENT, DRACULA_BG);
    int fillW = (int)(pct / 100.0f * (w - 4));
    if (fillW < h - 4) fillW = (pct > 0) ? (h - 4) : 0;
    if (fillW > 0) {
        tft.fillSmoothRoundRect(x + 2, y + 2, fillW, h - 4, (h - 4) / 2, fillColor, DRACULA_CURRENT);
    }
}

// "Pill" colorido e suavizado com a variação percentual (verde/vermelho), alinhado à direita de boxRightX.
inline void drawPctBadge(TFT_eSPI& tft, int boxRightX, int cy, float pct) {
    const int boxW = 74, h = 20;
    int x0 = boxRightX - boxW;
    tft.fillRect(x0, cy - h / 2 - 1, boxW, h + 2, DRACULA_BG);

    char buf[10];
    snprintf(buf, sizeof(buf), "%+.1f%%", pct);
    tft.setTextFont(2);
    int textW = tft.textWidth(buf);
    int w = textW + 16;
    if (w > boxW - 4) w = boxW - 4;
    int x = boxRightX - w;
    uint16_t bg = (pct >= 0) ? DRACULA_GREEN : DRACULA_RED;

    tft.fillSmoothRoundRect(x, cy - h / 2, w, h, h / 2, bg, DRACULA_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DRACULA_BG, bg);
    tft.drawString(buf, x + w / 2, cy + 1, 2);
}

// Indicador de status discreto (dot suavizado + label pequeno), alinhado à direita de x.
inline void drawStatusDot(TFT_eSPI& tft, int rightX, int y, uint16_t color, const char* label) {
    tft.fillRect(rightX - 140, y - 10, 140, 20, DRACULA_BG);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
    tft.drawString(label, rightX - 10, y, 2);
    tft.fillSmoothCircle(rightX - 4, y, 4, color, DRACULA_BG);
}

// Carrega/descarrega a smooth font (anti-aliased) usada nos labels estáticos das
// linhas de conteúdo. Enquanto carregada, ela SOBREPÕE qualquer número de fonte
// clássica passado a drawString — por isso só fica ativa ao redor do onEnter()
// (labels são desenhados uma vez só), nunca durante o update() dos valores.
inline void beginLabelFont(TFT_eSPI& tft) {
    tft.loadFont(NotoSansBold15);
}

inline void endLabelFont(TFT_eSPI& tft) {
    tft.unloadFont();
}

} // namespace theme
