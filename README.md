# CYD Desk Panel

Painel auxiliar de mesa para o ESP32-2432S028R (Cheap Yellow Display).

Painel auxiliar ao PC — não é foco relógio/clima (já tem outro dispositivo pra isso). O foco é dados do PC e mercado: stats de CPU/RAM/disco/rede e cotações (USD/AUD/BTC/SOL), via agente HTTP na rede local. Tema visual escuro (Dracula), navegação por abas com ícones no topo.

## Estrutura

```
cyd-desk-panel/
├── platformio.ini          # config da placa + TFT_eSPI via build flags
├── src/
│   ├── main.cpp            # setup WiFi/touch/NTP + loop
│   ├── config.h            # SUAS credenciais e IPs (editar antes de gravar)
│   ├── theme.h             # paleta Dracula, ícones vetoriais, widgets (barra/badge/status dot) — formas suavizadas (anti-aliased)
│   ├── NotoSansBold15.h    # smooth font (anti-aliased) embutida em flash, usada nos labels estáticos
│   ├── screens.h           # interface Screen + ScreenManager (tab bar no topo)
│   ├── screen_clock.h      # tela de relógio (herança do MVP, não é foco)
│   ├── screen_pcstats.h    # tela de stats do PC (CPU/RAM/disco/rede)
│   └── screen_quotes.h     # tela de cotações (USD/AUD/BTC/SOL)
└── agent/
    ├── pc_stats_agent.py   # agente que roda no PC (psutil + HTTP)
    ├── start_agent.bat     # inicia o agente (duplo-clique)
    ├── stop_agent.bat      # para o agente (duplo-clique)
    └── restart_agent.bat   # reinicia o agente (duplo-clique)
```

## Setup

### 1. Firmware

1. Instale o [PlatformIO](https://platformio.org/) (extensão do VS Code resolve)
2. Edite `src/config.h`: SSID, senha do WiFi e IP da sua máquina em `AGENT_HOST`
3. Conecte a placa via USB e rode:

```
pio run -t upload
```

Se a porta não for detectada automaticamente, o driver é o CH340.

### 2. Agente no PC

```
pip install psutil
python agent/pc_stats_agent.py
```

Ele expõe dois endpoints:

`GET http://SEU_IP:8377/stats`:

```json
{"cpu": 23.5, "ram": 61.2, "disk": 54.0, "disk_read_mbps": 1.02, "disk_write_mbps": 11.88, "net_down_mbps": 4.31, "net_up_mbps": 0.87}
```

`GET http://SEU_IP:8377/quotes` (USD/AUD/BTC/SOL em BRL, atualizado a cada 60s a partir da AwesomeAPI e CoinGecko):

```json
{"USD": {"brl": 5.42, "change_pct": -0.18}, "AUD": {"brl": 3.58, "change_pct": 0.05}, "BTC": {"brl": 350000.0, "change_pct": 2.1}, "SOL": {"brl": 850.5, "change_pct": -1.2}, "updated_at": 1234567890}
```

Teste no navegador antes de culpar o ESP32.

**Iniciar/parar fácil (Windows):** dê duplo-clique em `agent/start_agent.bat`, `stop_agent.bat` ou `restart_agent.bat`. O `start` detecta se já tem um agente rodando na porta 8377 e não duplica; o `stop` mata qualquer processo `pc_stats_agent.py` que esteja rodando (mesmo se tiver mais de um por acidente).

Como é um processo manual, **ele não sobrevive a um reboot** — depois de reiniciar o PC, rode `start_agent.bat` de novo (ou configure pra rodar automático: agendador de tarefas apontando para `pythonw.exe pc_stats_agent.py` no logon). Libere a porta 8377 no firewall para rede privada.

## Navegação

- Tab bar fixa no topo (34px) com o ícone de cada tela; a aba ativa "gruda" no conteúdo (mesma cor) com uma barrinha de destaque embaixo.
- Toque em qualquer aba troca de tela direto.
- Toque abaixo da tab bar: repassado para a tela ativa (hook `onTouch`).

## Adicionando telas novas

1. Crie `screen_minhatela.h` implementando a interface `Screen` (`name`, `drawIcon`, `onEnter`, `update`, opcionalmente `accentColor`)
2. No `main.cpp`: instancie e chame `manager.add(&minhaTela)`
3. Use as cores e widgets de `theme.h` (paleta Dracula, `drawBar`, `drawPctBadge`, `drawStatusDot`, ícones vetoriais) em vez de `TFT_*` cru, pra manter a tela consistente com o tema.

O padrão de desenho: layout estático no `onEnter`, atualização incremental no `update` (com `setTextPadding` para apagar o valor anterior sem redesenhar a tela toda).

## Calibração do touch

Se o toque estiver deslocado, ajuste `TOUCH_MIN/MAX_X/Y` no `config.h`. Os valores crus aparecem se você logar `p.x`/`p.y` no `main.cpp` via Serial.
