#pragma once
#include "screens.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

// Cotações — um card por ativo, preço com easing ("rolando" até o valor novo,
// efeito de painel de bolsa) e badge colorido com a variação percentual.
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
        for (int i = 0; i < N_ROWS; i++) {
            // Ao voltar pra aba, mostra direto o último valor conhecido
            // (sem rolar do zero de novo); força o redesenho zerando o cache.
            rows_[i].disp = rows_[i].target;
            rows_[i].lastStr[0] = '\0';
            rows_[i].pctDrawn = false;
        }

        theme::beginLabelFont(tft);
        for (int i = 0; i < N_ROWS; i++) {
            int y = rowY(i);
            theme::drawCard(tft, 8, y, 304, CARD_H);
            theme::iconCoin(tft, 30, y + CARD_H / 2, 11, rows_[i].color);
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(DRACULA_COMMENT, DRACULA_CARD);
            tft.drawString(rows_[i].ticker, 48, y + CARD_H / 2);
        }
        theme::endLabelFont(tft);
    }

    void update(TFT_eSPI& tft) override {
        unsigned long now = millis();
        if (now - lastPoll_ >= QUOTES_POLL_MS || !drawn_) {
            lastPoll_ = now;
            poll(tft);
        }
        // Easing dos preços a cada tick, independente do poll
        for (int i = 0; i < N_ROWS; i++) animateRow(tft, i);
    }

private:
    static const int N_ROWS = 4;
    static const int CARD_H = 42;
    static const int STATUS_CY = 231;

    struct Row {
        const char* ticker;
        uint16_t color;
        float target = 0, disp = 0, pct = 0;
        char lastStr[16] = "";
        bool hasData = false;
        bool pctDrawn = false;
        Row(const char* t, uint16_t c) : ticker(t), color(c) {}
    };

    // 4º card termina em y=216 — folga de 5px pro status (221..240)
    int rowY(int i) { return 36 + i * 46; }

    void animateRow(TFT_eSPI& tft, int i) {
        Row& r = rows_[i];
        if (!r.hasData) return;

        r.disp += (r.target - r.disp) * 0.25f;
        // Converge de vez quando chega perto (proporcional à escala do ativo)
        if (fabsf(r.disp - r.target) < fmaxf(0.005f, r.target * 0.0001f)) {
            r.disp = r.target;
        }

        char buf[16];
        formatPrice(buf, sizeof(buf), r.disp);
        if (strcmp(buf, r.lastStr) != 0) {
            strncpy(r.lastStr, buf, sizeof(r.lastStr));
            theme::drawValue(tft, 92, rowY(i) + CARD_H / 2 - 12, 128, 24,
                             buf, 4, DRACULA_FG, DRACULA_CARD, ML_DATUM);
        }
        if (!r.pctDrawn) {
            r.pctDrawn = true;
            theme::drawPctBadge(tft, 298, rowY(i) + CARD_H / 2, r.pct, DRACULA_CARD);
        }
    }

    void formatPrice(char* buf, size_t len, float brl) {
        if (brl >= 1000.0f) {
            snprintf(buf, len, "R$%.0f", brl);
        } else {
            snprintf(buf, len, "R$%.2f", brl);
        }
    }

    void setRow(int i, float brl, float pct) {
        Row& r = rows_[i];
        r.target = brl;
        if (fabsf(pct - r.pct) >= 0.005f || !r.hasData) {
            r.pct = pct;
            r.pctDrawn = false;  // badge só redesenha quando a variação muda
        }
        r.hasData = true;
    }

    void poll(TFT_eSPI& tft) {
        if (WiFi.status() != WL_CONNECTED) {
            theme::drawStatusDot(tft, 310, STATUS_CY, DRACULA_RED, "sem wifi");
            return;
        }

        HTTPClient http;
        String url = String("http://") + AGENT_HOST + ":" + AGENT_PORT + "/quotes";
        http.setTimeout(1500);
        http.begin(url);
        int code = http.GET();

        if (code != 200) {
            http.end();
            theme::drawStatusDot(tft, 310, STATUS_CY, DRACULA_ORANGE, "agente offline");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) {
            theme::drawStatusDot(tft, 310, STATUS_CY, DRACULA_ORANGE, "json invalido");
            return;
        }

        setRow(0, doc["USD"]["brl"] | 0.0f, doc["USD"]["change_pct"] | 0.0f);
        setRow(1, doc["AUD"]["brl"] | 0.0f, doc["AUD"]["change_pct"] | 0.0f);
        setRow(2, doc["BTC"]["brl"] | 0.0f, doc["BTC"]["change_pct"] | 0.0f);
        setRow(3, doc["SOL"]["brl"] | 0.0f, doc["SOL"]["change_pct"] | 0.0f);

        theme::drawStatusDot(tft, 310, STATUS_CY, DRACULA_GREEN, "ok");
        drawn_ = true;
    }

    Row rows_[N_ROWS] = {
        {"USD", DRACULA_GREEN},
        {"AUD", DRACULA_CYAN},
        {"BTC", DRACULA_ORANGE},
        {"SOL", DRACULA_PURPLE},
    };
    unsigned long lastPoll_ = 0;
    bool drawn_ = false;
};
