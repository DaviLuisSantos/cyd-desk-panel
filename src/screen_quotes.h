#pragma once
#include "screens.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class QuotesScreen : public Screen {
public:
    const char* name() override { return "Cotacoes"; }
    uint16_t accentColor() override { return DRACULA_ORANGE; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconCoin(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        lastPoll_ = 0;
        drawn_ = false;

        theme::beginLabelFont(tft);
        drawRowLabel(tft, ROW_USD, "USD", DRACULA_GREEN);
        drawRowLabel(tft, ROW_AUD, "AUD", DRACULA_CYAN);
        drawRowLabel(tft, ROW_BTC, "BTC", DRACULA_ORANGE);
        drawRowLabel(tft, ROW_SOL, "SOL", DRACULA_PURPLE);
        theme::endLabelFont(tft);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ < QUOTES_POLL_MS && drawn_) return;
        lastPoll_ = now;

        if (WiFi.status() != WL_CONNECTED) {
            theme::drawStatusDot(tft, 310, ROW_STATUS, DRACULA_RED, "sem wifi");
            return;
        }

        HTTPClient http;
        String url = String("http://") + AGENT_HOST + ":" + AGENT_PORT + "/quotes";
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

        drawRow(tft, ROW_USD, doc["USD"]["brl"] | 0.0f, doc["USD"]["change_pct"] | 0.0f);
        drawRow(tft, ROW_AUD, doc["AUD"]["brl"] | 0.0f, doc["AUD"]["change_pct"] | 0.0f);
        drawRow(tft, ROW_BTC, doc["BTC"]["brl"] | 0.0f, doc["BTC"]["change_pct"] | 0.0f);
        drawRow(tft, ROW_SOL, doc["SOL"]["brl"] | 0.0f, doc["SOL"]["change_pct"] | 0.0f);

        theme::drawStatusDot(tft, 310, ROW_STATUS, DRACULA_GREEN, "ok");
        drawn_ = true;
    }

private:
    static const int ROW_USD = 74;
    static const int ROW_AUD = 126;
    static const int ROW_BTC = 178;
    static const int ROW_SOL = 214;
    static const int ROW_STATUS = 234;

    void drawRowLabel(TFT_eSPI& tft, int y, const char* label, uint16_t color) {
        theme::iconCoin(tft, 24, y, 11, color);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
        tft.drawString(label, 42, y, 2);
    }

    void drawRow(TFT_eSPI& tft, int y, float brl, float pct) {
        char priceBuf[16];
        if (brl >= 1000.0f) {
            snprintf(priceBuf, sizeof(priceBuf), "R$%.0f", brl);
        } else {
            snprintf(priceBuf, sizeof(priceBuf), "R$%.2f", brl);
        }
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(DRACULA_FG, DRACULA_BG);
        tft.setTextPadding(140);
        tft.drawString(priceBuf, 78, y, 4);
        tft.setTextPadding(0);

        theme::drawPctBadge(tft, 310, y, pct);
    }

    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
};
