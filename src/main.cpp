// CYD Desk Panel — ESP32-2432S028R
// Painel auxiliar de mesa: relógio + stats do PC + cotações
// Navegação: tab bar touch no topo (+ auto-next configurável)

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <time.h>

#include "config.h"
#include "screens.h"
#include "screen_clock.h"
#include "screen_pcstats.h"
#include "screen_procs.h"
#include "screen_sysinfo.h"
#include "screen_quotes.h"
#include "screen_youtube.h"

TFT_eSPI tft;

// Touch usa um barramento SPI próprio (VSPI) — pinos diferentes do display
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

ScreenManager manager;
ClockScreen screenClock;
PcStatsScreen screenPc;
ProcsScreen screenProcs;
SysInfoScreen screenSys;
QuotesScreen screenQuotes;
YouTubeScreen screenYt;

unsigned long lastTouchMs = 0;
const unsigned long TOUCH_DEBOUNCE_MS = 300;

void connectWiFi() {
    tft.fillScreen(DRACULA_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DRACULA_FG, DRACULA_BG);
    tft.drawString("Conectando WiFi...", 160, 110, 4);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(DRACULA_CYAN, DRACULA_BG);
        tft.drawString(WiFi.localIP().toString(), 160, 150, 2);
        delay(800);
    } else {
        tft.setTextColor(DRACULA_ORANGE, DRACULA_BG);
        tft.drawString("Sem WiFi (segue offline)", 160, 150, 2);
        delay(1200);
    }
}

const int BL_PWM_CH = 0;

void setup() {
    Serial.begin(115200);

    // Backlight via PWM (permite fade-in suave no boot em vez de ligar seco)
    ledcSetup(BL_PWM_CH, 5000, 8);
    ledcAttachPin(TFT_BL, BL_PWM_CH);
    ledcWrite(BL_PWM_CH, 0);

    // Display em landscape, USB pra direita
    tft.init();
    tft.setRotation(1);
    // Muitos CYD (ESP32-2432S028R) mostram as cores invertidas com o
    // ILI9341_2_DRIVER — sem isso, o fundo escuro do tema aparece esbranquiçado.
    // Se ficar pior (cores nítidas ao contrário do esperado), troque pra false.
    tft.invertDisplay(true);
    tft.fillScreen(DRACULA_BG);

    // Fade-in do backlight com a tela já limpa no fundo do tema
    for (int duty = 0; duty <= 255; duty += 5) {
        ledcWrite(BL_PWM_CH, duty);
        delay(4);
    }

    // Touch no barramento próprio
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);

    connectWiFi();

    // NTP
    configTzTime(TZ_INFO, NTP_SERVER);

    // Registra telas — pra adicionar uma nova, é só criar a classe e dar add()
    manager.add(&screenClock);
    manager.add(&screenPc);
    manager.add(&screenProcs);
    manager.add(&screenSys);
    manager.add(&screenQuotes);
    manager.add(&screenYt);
    manager.begin(tft);
}

void loop() {
    // Touch com debounce
    if (touch.tirqTouched() && touch.touched()) {
        unsigned long now = millis();
        if (now - lastTouchMs > TOUCH_DEBOUNCE_MS) {
            lastTouchMs = now;
            TS_Point p = touch.getPoint();
            // Mapeia coordenadas cruas -> tela 320x240 (landscape)
            int x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 320);
            int y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 240);
            x = constrain(x, 0, 319);
            y = constrain(y, 0, 239);
            manager.handleTouch(x, y);
        }
    }

    manager.update();
    delay(20);
}
