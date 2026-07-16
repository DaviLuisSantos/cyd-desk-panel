#pragma once
#include "screens.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

typedef void (*IconFn)(TFT_eSPI&, int, int, int, uint16_t);

class PcStatsScreen : public Screen {
public:
    const char* name() override { return "PC"; }
    uint16_t accentColor() override { return DRACULA_GREEN; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconCpu(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;
        dispCpu_ = dispRam_ = dispDisk_ = 0;
        targetCpu_ = targetRam_ = targetDisk_ = 0;
        lastDrawnCpu_ = lastDrawnRam_ = lastDrawnDisk_ = -1;
        netDown_ = netUp_ = -1;

        theme::beginLabelFont(tft);
        drawRowLabel(tft, ROW_CPU, theme::iconCpu, "CPU");
        drawRowLabel(tft, ROW_RAM, theme::iconRam, "RAM");
        drawRowLabel(tft, ROW_DISK, theme::iconDisk, "DISCO");
        theme::endLabelFont(tft);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ >= AGENT_POLL_MS || !drawn_) {
            lastPoll_ = now;
            poll(tft);
        }
        // Anima as barras a cada tick, independente do poll (efeito de easing)
        animate(tft, ROW_CPU, dispCpu_, targetCpu_, lastDrawnCpu_);
        animate(tft, ROW_RAM, dispRam_, targetRam_, lastDrawnRam_);
        animate(tft, ROW_DISK, dispDisk_, targetDisk_, lastDrawnDisk_);
    }

private:
    static const int ROW_CPU = 76;
    static const int ROW_RAM = 130;
    static const int ROW_DISK = 184;
    static const int ROW_NET = 210;
    static const int ROW_STATUS = 234;

    void drawRowLabel(TFT_eSPI& tft, int y, IconFn icon, const char* label) {
        icon(tft, 24, y, 12, DRACULA_COMMENT);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
        tft.drawString(label, 42, y, 2);
    }

    void animate(TFT_eSPI& tft, int y, float& displayed, float target, int& lastDrawn) {
        displayed += (target - displayed) * 0.3f;
        if (fabsf(displayed - target) < 0.1f) displayed = target;

        int intPct = (int)(displayed + 0.5f);
        if (intPct == lastDrawn) return;
        lastDrawn = intPct;

        theme::drawBar(tft, 96, y - 11, 148, 22, displayed, barColor(displayed));

        char buf[6];
        snprintf(buf, sizeof(buf), "%3d%%", intPct);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_FG, DRACULA_BG);
        tft.setTextPadding(52);
        tft.drawString(buf, 254, y, 4);
        tft.setTextPadding(0);
    }

    uint16_t barColor(float pct) {
        if (pct >= 90) return DRACULA_RED;
        if (pct >= 70) return DRACULA_ORANGE;
        return DRACULA_GREEN;
    }

    void poll(TFT_eSPI& tft) {
        if (WiFi.status() != WL_CONNECTED) {
            theme::drawStatusDot(tft, 310, ROW_STATUS, DRACULA_RED, "sem wifi");
            return;
        }

        HTTPClient http;
        String url = String("http://") + AGENT_HOST + ":" + AGENT_PORT + "/stats";
        http.setTimeout(1500);
        http.begin(url);
        int code = http.GET();

        if (code != 200) {
            http.end();
            theme::drawStatusDot(tft, 310, ROW_STATUS, DRACULA_ORANGE, "agente offline");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) {
            theme::drawStatusDot(tft, 310, ROW_STATUS, DRACULA_ORANGE, "json invalido");
            return;
        }

        targetCpu_  = doc["cpu"]  | 0.0f;
        targetRam_  = doc["ram"]  | 0.0f;
        targetDisk_ = doc["disk"] | 0.0f;
        float dl = doc["net_down_mbps"] | 0.0f;
        float up = doc["net_up_mbps"]   | 0.0f;
        drawNet(tft, dl, up);

        theme::drawStatusDot(tft, 310, ROW_STATUS, DRACULA_GREEN, "ok");
        drawn_ = true;
    }

    void drawNet(TFT_eSPI& tft, float dl, float up) {
        if (dl == netDown_ && up == netUp_) return;
        netDown_ = dl;
        netUp_ = up;

        theme::iconArrowDown(tft, 30, ROW_NET, 5, DRACULA_CYAN);
        char bufDl[16];
        snprintf(bufDl, sizeof(bufDl), "%.1f Mb/s", dl);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_FG, DRACULA_BG);
        tft.setTextPadding(90);
        tft.drawString(bufDl, 46, ROW_NET, 4);
        tft.setTextPadding(0);

        theme::iconArrowUp(tft, 190, ROW_NET, 5, DRACULA_PURPLE);
        char bufUp[16];
        snprintf(bufUp, sizeof(bufUp), "%.1f Mb/s", up);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_FG, DRACULA_BG);
        tft.setTextPadding(90);
        tft.drawString(bufUp, 206, ROW_NET, 4);
        tft.setTextPadding(0);
    }

    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
    float dispCpu_ = 0, dispRam_ = 0, dispDisk_ = 0;
    float targetCpu_ = 0, targetRam_ = 0, targetDisk_ = 0;
    int lastDrawnCpu_ = -1, lastDrawnRam_ = -1, lastDrawnDisk_ = -1;
    float netDown_ = -1, netUp_ = -1;
};
