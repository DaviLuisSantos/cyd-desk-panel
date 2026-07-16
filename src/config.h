#pragma once

// ===== WiFi =====
#define WIFI_SSID     "virus"
#define WIFI_PASSWORD "Davi@2025"

// ===== Agente de stats do PC =====
// IP da sua máquina na rede local + porta do agente Python
#define AGENT_HOST "192.168.1.11"
#define AGENT_PORT 8377
#define AGENT_POLL_MS 2000     // intervalo de consulta a /stats
#define QUOTES_POLL_MS 30000   // intervalo de consulta a /quotes (o agente só atualiza a cada 60s)

// ===== Navegação =====
#define AUTO_NEXT_MS 10000  // troca de aba automática (0 = desativa). Reinicia a cada toque.

// ===== Relógio (NTP) =====
#define NTP_SERVER "pool.ntp.org"
#define TZ_INFO    "<-03>3"  // Brasília (sem horário de verão)

// ===== Touch (XPT2046) — pinos fixos da CYD =====
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Calibração do touch (ajuste fino se necessário)
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 240
#define TOUCH_MAX_Y 3800
