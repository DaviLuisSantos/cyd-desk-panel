# CLAUDE.md

Orientações para agentes trabalhando neste repositório. Respostas e comentários de código em **português** (o projeto todo é em pt-BR).

## O que é

Firmware para o **ESP32-2432S028R (Cheap Yellow Display / CYD)** — um painel auxiliar de mesa. Seis telas navegáveis por toque:

- **Relógio** (`screen_clock.h`) — hora/data via NTP. Herança do MVP inicial, não é foco (o Davi já tem outro dispositivo pra relógio/clima).
- **PC Stats** (`screen_pcstats.h`) — anéis de CPU/RAM, sparkline de histórico da CPU, disco e rede. É o coração do painel.
- **Processos** (`screen_procs.h`) — top 4 processos por CPU (agregados por nome no agente).
- **Rede** (`screen_sysinfo.h`) — ping, uptime, IP público e sparkline de download.
- **Cotações** (`screen_quotes.h`) — USD, AUD, BTC, SOL em BRL.
- **YouTube** (`screen_youtube.h`) — inscritos/views/vídeos dos canais configurados no agente (requer API key).

Todas menos o Relógio consultam um agente HTTP (`agent/pc_stats_agent.py`) rodando na máquina local (`/stats`, `/quotes`, `/procs`, `/sysinfo`, `/youtube`).

**GPU não interessa** — o Davi pediu explicitamente pra não incluir métricas de GPU no painel.

**Visual: tema Dracula, navegação por abas.** Tab bar fixa no topo (34px) com ícones vetoriais (sem bitmap/filesystem); tocar numa aba troca de tela direto — sem mais swipe nas bordas. Ver seção "Tema visual" abaixo.

## Build / gravação

Projeto **PlatformIO** (não Arduino IDE). Ambiente único: `[env:cyd]`.

```
pio run                # compila
pio run -t upload      # compila e grava (upload_speed 921600)
pio device monitor     # serial @ 115200
```

Driver USB da placa: **CH340**. Sem git inicializado neste diretório.

## Visão / Roadmap

O objetivo é ser uma **tela auxiliar permanente ao lado do PC**, mostrando informações de relance.

**Foco:** o Davi já tem OUTRO dispositivo dedicado a relógio/clima/tempo. Este painel NÃO é sobre isso — ele mostra o que aquele não mostra: dados do PC, mercado e afins. Não priorizar/expandir tela de relógio ou clima aqui (a `screen_clock.h` atual é herança do MVP e pode até sair no futuro).

Direção pretendida (múltiplas telas navegáveis por toque):

- **PC Stats** (`screen_pcstats.h`) — feito com CPU/RAM/disco/rede. Ainda dá pra expandir com temperatura, GPU, processos, etc. (temperatura/GPU exigem libs extras no agente — `psutil.sensors_temperatures` não funciona no Windows).
- **Cotações** (`screen_quotes.h`) — feito com USD/AUD/BTC/SOL em BRL.
- Novas telas de "coisas que o Davi julga importante" ver de relance, cada uma como um `Screen`.

**Decisão de arquitetura para dados externos (cotações):** resolvido — o `pc_stats_agent.py` busca as cotações (AwesomeAPI para USD/AUD, CoinGecko para BTC/SOL, ambas via `urllib` da stdlib, sem chave) e expõe em `/quotes`, cacheado em memória e atualizado a cada 60s por uma thread de background. O ESP32 só fala HTTP puro com o agente, nunca HTTPS direto com APIs externas. Faz sentido porque o painel já é "auxiliar ao PC" — só é útil com o PC ligado mesmo.

## Arquitetura

- **Toda a config da TFT_eSPI vem por `build_flags` no `platformio.ini`** — NÃO editar `User_Setup.h` da lib. Display ILI9341 320x240, HSPI. Se mexer em pinos/driver do display, é lá.
- **Dois barramentos SPI distintos**: display no HSPI (pinos nos build_flags), touch XPT2046 no VSPI (`touchSPI` em `main.cpp`, pinos em `config.h`). Não misturar.
- **Cores invertidas — `tft.invertDisplay(true)` em `main.cpp`, logo após `tft.init()`.** Muitas unidades do CYD (ESP32-2432S028R) mostram cor invertida com `ILI9341_2_DRIVER` (a sequência de init alternativa do driver corrige outros problemas do painel, mas não define a polaridade de inversão). Sintoma sem essa chamada: o fundo escuro do tema Dracula (`#282a36`, quase preto) aparece **branco/cinza-claro** — inverter um RGB565 escuro dá um valor claro, então "fundo branco" com tema escuro configurado é o sinal clássico disso. Se depois de gravar as cores ficarem estranhas na direção oposta (era certo, virou errado), trocar para `tft.invertDisplay(false)` — é hardware-dependente, sem como cravar de antemão sem o board físico na mão.
- **`src/config.h`** guarda credenciais WiFi, IP/porta do agente, servidor NTP/timezone e calibração do touch. É o único arquivo que o usuário edita antes de gravar.
- **`src/screens.h`** define o contrato `Screen` (`name`/`accentColor`/`drawIcon`/`onEnter`/`update`/`onTouch`) e o `ScreenManager` (tab bar fixa no topo + navegação por toque na aba). Telas são registradas em `main.cpp` com `manager.add(&tela)`.
- **`src/theme.h`** — paleta Dracula em RGB565, constantes de layout (`TAB_BAR_H`, `CONTENT_TOP`, etc.) e os ícones vetoriais/widgets compartilhados (`theme::iconCpu`, `theme::drawBar`, `theme::drawPctBadge`, `theme::drawStatusDot`...). Toda tela nova deve puxar cor e widgets daqui, não usar `TFT_*` cru.

## Tema visual

- **Paleta:** Dracula (`DRACULA_BG`, `DRACULA_FG`, `DRACULA_CURRENT`, `DRACULA_CARD`, `DRACULA_CYAN/GREEN/ORANGE/PINK/PURPLE/RED/YELLOW`), calculada em RGB565 e definida em `theme.h`. Nunca usar as constantes `TFT_*` do TFT_eSPI (TFT_BLACK, TFT_WHITE etc.) — sempre `DRACULA_*`. `DRACULA_CARD` (#313442, entre BG e CURRENT) é o fundo dos cards de conteúdo (`theme::drawCard`).
- **Desenho sem flicker via sprite (`theme::Canvas`):** todo elemento dinâmico (texto que atualiza, barra, anel, badge, sparkline) é desenhado num `TFT_eSprite` compartilhado e enviado com um único `pushSprite` — atualização atômica, zero piscada. O padrão é create/push/delete por uso (os tamanhos se repetem a cada ciclo, o heap estabiliza; nada de sprite gigante permanente disputando RAM com o WiFi). `theme::drawValue(tft, x, y, w, h, texto, fonte, cor, bg, datum)` substitui o padrão antigo de `setTextPadding` + `drawString` direto para valores dinâmicos — a região inteira (w×h) é redesenhada no sprite, então o valor antigo some junto. **Sempre passar o `bg` real de onde o widget está** (`DRACULA_BG` ou `DRACULA_CARD`). **Pegadinha de altura (bug já corrigido uma vez):** se `h` for menor que a altura da fonte (font 4 = 26px), texto centralizado estoura o topo do sprite e os dígitos saem decapitados — por isso o `drawValue` ancora o texto pelo topo quando `fontHeight > h` (cortar por baixo é invisível pra números, é área de descender). Mesmo assim, prefira `h` ≥ altura da fonte quando o layout permitir (font 4 → h=22-26).
- **Navegação por abas:** `ScreenManager` desenha uma tab bar de `TAB_BAR_H`=34px no topo com o ícone de cada `Screen` (via `drawIcon`). A aba ativa vira da cor do conteúdo (`DRACULA_BG`, efeito "aba conectada") com uma barrinha de destaque (`accentColor()` daquela tela) por baixo; as inativas ficam com ícone em `DRACULA_COMMENT` sobre fundo `DRACULA_CURRENT`. Tocar em qualquer ponto com `y < TAB_BAR_H` troca de aba direto (calcula o índice pela posição x); toque abaixo disso vai pro `onTouch` da tela ativa. Não tem mais swipe nas bordas — cada tela é acessada diretamente pela aba.
- **Auto-next:** o `ScreenManager` troca de aba sozinho a cada `AUTO_NEXT_MS` (`config.h`, padrão 10s) sem interação — dá pra deixar o painel "passando" as telas sozinho. Qualquer toque (numa aba ou no conteúdo) reinicia o timer via `lastActivityMs_`, então não interrompe quem tá mexendo. `AUTO_NEXT_MS = 0` desativa.
- **Ícones são 100% vetoriais** (`theme::iconClock/iconCpu/iconRam/iconDisk/iconNet/iconCoin/iconList/iconPlay/iconArrowUp/iconArrowDown`), desenhados com primitivas do TFT_eSPI (`drawCircle`, `fillTriangle`, etc.) — de propósito, pra não precisar de SPIFFS/LittleFS nem bitmaps embutidos no firmware. Recebem `cx, cy, size, color` — `size=8` é o padrão usado na tab bar; nas linhas de conteúdo usa-se algo maior (11-12) pra ganhar presença visual sem depender de bitmap. Se for adicionar um ícone novo, seguir esse padrão (função livre em `theme.h`, parametrizada por tamanho).
- **Distribuição vertical do conteúdo:** o conteúdo usa a altura toda abaixo da tab bar, não só o topo — evitar layout apertado em cima com vazio embaixo (erro já corrigido uma vez) e **deixar folga real (3px+) entre blocos vizinhos** — bloco encostando em bloco (card terminando na mesma linha y em que a região de texto seguinte começa) parece sobreposição (erro também já corrigido uma vez, no rodapé de rede do PC Stats). PC Stats: anéis CPU/RAM (centro y=90) → sparkline (y=136) → card do disco (y=158, h=36) → rodapé rede (centro y=208) + status (y=231). Cotações: 4 cards de 42px a cada 46px a partir de y=36, status em y=231.
- **Widgets reutilizáveis em `theme.h`:** `drawBar` (barra de progresso arredondada sobre trilho `DRACULA_CURRENT`), `drawRingGauge` (anel/gauge com `drawSmoothArc`, arco de 30° a 330° com abertura embaixo, valor no centro — usado pra CPU/RAM), `drawSparkline` (mini-gráfico de linha do histórico, valores 0-100 em ordem cronológica), `drawCard` (fundo de card `DRACULA_CARD` arredondado — estático, desenhado no `onEnter`), `drawPctBadge` (pill verde/vermelho pra variação percentual), `drawStatusDot` (dot colorido + label pequeno pra status de rede/agente). Todos os dinâmicos desenham via `theme::Canvas` (sprite) e recebem um `bg` explícito (a cor que já está por trás) pro blend anti-aliased das bordas — sempre passar a cor de fundo real do local (BG ou CARD), não deixar no default.
- **Smooth font (anti-aliased) nos labels estáticos:** `src/NotoSansBold15.h` é uma fonte suavizada (não é bitmap 1-bit) embutida como array em flash (sem precisar de SPIFFS/LittleFS — segue a mesma filosofia dos ícones vetoriais). Usada via `theme::beginLabelFont(tft)` / `theme::endLabelFont(tft)`, que fazem `tft.loadFont(...)`/`tft.unloadFont()`. **Importante:** enquanto uma smooth font está carregada, ela sobrepõe QUALQUER número de fonte clássica passado a `drawString` (é uma particularidade documentada do TFT_eSPI) — por isso só é usada ao redor de texto estático desenhado uma vez (labels em `onEnter`, data do relógio), nunca em volta de valores que atualizam com frequência (percentual das barras, preço, hora), que continuam nas fontes clássicas rápidas (2/4/6/7/8) com o padrão de `setTextPadding` de sempre. Se for redesenhar algo com smooth font no MESMO lugar depois (não é o caso hoje), lembrar do 3º parâmetro de `setTextColor(fg, bg, true)` — sem ele o fundo não é preenchido e o texto antigo não é apagado.
- **Animações leves, todas via `millis()` sem lib de animação:**
  - Relógio: dois-pontos piscando a cada 500ms (troca `:` por espaço no buffer, redesenha via sprite só quando o estado de piscar muda) + barrinha de progresso dos segundos dentro do minuto.
  - PC Stats: anéis de CPU/RAM e barra do disco fazem *easing* suave até o valor-alvo a cada tick do `loop()` (~20ms, fator 0.3 por tick), redesenhando só quando o percentual inteiro exibido muda. O polling HTTP em si continua no intervalo de `AGENT_POLL_MS`; a animação roda independente disso. O sparkline guarda os últimos 56 polls de CPU (~2 min) em RAM e redesenha a cada poll.
  - Cotações: preços "rolam" com easing (fator 0.25 por tick) até o valor novo, redesenhando só quando a string formatada muda; o badge de variação só redesenha quando o percentual muda. Ao reentrar na aba, `disp` é igualado ao `target` pra não rolar do zero de novo.
  - Transição de aba: o `ScreenManager` faz um *wipe* — uma linha vertical na cor de destaque da tela nova varre a área de conteúdo (esquerda→direita indo pra frente/dando a volta, direita→esquerda voltando), apagando a tela antiga atrás de si (~100ms, bloqueante via `delay(3)` por fatia).
  - Boot: fade-in do backlight via PWM (`ledcSetup`/`ledcWrite`, canal 0 em `main.cpp`) — a tela acende suave já no fundo do tema em vez de ligar seco.
- Cada tela expõe `accentColor()` (cor de identidade daquela aba — Relógio=CYAN, PC Stats=GREEN, Cotações=ORANGE) usada na tab bar e em detalhes do conteúdo (ex.: barra de segundos do relógio usa CYAN).

## Padrão de desenho de tela (importante)

Displays SPI são lentos — **nunca redesenhar a tela inteira em `update()`**. O padrão:

- `onEnter`: desenha o layout estático (cards, ícones, labels) uma vez, direto na tela.
- `update`: atualiza só o que mudou, guardando o último valor em membro (`lastMinute_`, `lastDrawnCpu_`, `drawn_`, etc.) e redesenhando só na mudança.
- Valores dinâmicos são redesenhados via sprite (`theme::drawValue` e os widgets de `theme.h`) — push atômico apaga o valor antigo junto, sem flicker. O padrão antigo de `setTextPadding` só sobrevive em texto estático redesenhado raramente (data do relógio).

Ver `screen_clock.h` (redesenha hora só quando o minuto muda/pisca) e `screen_pcstats.h`/`screen_quotes.h` (poll a cada `AGENT_POLL_MS`/`QUOTES_POLL_MS`, com `drawn_` pra forçar o primeiro desenho e status na base da tela pra erro de rede/agente).

## Adicionar uma tela nova

1. Criar `src/screen_minhatela.h` com uma classe que herda de `Screen`, implementando `name()`, `drawIcon()` (ícone da aba — usar/criar um helper em `theme.h`), `onEnter()`, `update()` e opcionalmente `accentColor()` (padrão é `DRACULA_PINK`).
2. Em `main.cpp`: incluir o header, instanciar global, e `manager.add(&minhaTela)` no `setup` (antes de `manager.begin`).
3. Usar sempre as cores/widgets de `theme.h`, nunca `TFT_*` cru — mantém a tela consistente com o tema Dracula das outras.

## Agente do PC

`agent/pc_stats_agent.py` — servidor HTTP (stdlib + `psutil`) na porta 8377, cinco endpoints (cada um alimentado por uma thread de background própria; o handler HTTP só lê cache):

- `GET /stats` — CPU, RAM, disco (uso % + MB/s leitura/escrita) e rede (MB/s down/up). Rede e disco são medidos por delta em thread de background (`_io_monitor`, a cada `IO_INTERVAL`=1s).
- `GET /quotes` — cotações USD/AUD/BTC/SOL em BRL. Buscadas por thread de background (`_quotes_monitor`, a cada `QUOTES_INTERVAL`=60s) via AwesomeAPI (fiat) e CoinGecko (cripto), cacheadas em memória — o handler HTTP nunca bate na API externa por request. Se o fetch falhar, mantém o último valor conhecido e loga o erro (não derruba a thread).
- `GET /procs` — top 4 processos por CPU, agregados por nome (todos os `chrome.exe` viram uma linha), com RAM somada em MB. `_procs_monitor` a cada `PROCS_INTERVAL`=3s; CPU normalizada pelo nº de núcleos. Mesma pegadinha do `cpu_percent`: por-processo ele também mede desde a última chamada naquele objeto `Process` — o `process_iter()` cacheia as instâncias, então funciona numa thread única.
- `GET /sysinfo` — `ping_ms` (TCP connect na porta 53 do 8.8.8.8 — ICMP puro exigiria admin no Windows), `public_ip` (api.ipify.org, a cada 10min), `uptime_s` e as taxas de rede atuais. `_sys_monitor` a cada `SYS_INTERVAL`=5s.
- `GET /youtube` — título/inscritos/views/vídeos dos canais em `YOUTUBE_CHANNEL_IDS` via YouTube Data API v3 (`_youtube_monitor`, a cada `YT_INTERVAL`=30min — 1 unidade de quota por fetch, folgadíssimo). **Requer `YOUTUBE_API_KEY`** (criar em console.cloud.google.com com a YouTube Data API v3 habilitada e colar na constante). Sem chave, responde `{"configured": false}` e a tela mostra o aviso. O canal do Davi (`UCkm45JSC0beQPEpYYfX3XKA`) já está pré-configurado na lista.

Testar no navegador (`http://IP:8377/stats` e `/quotes`) antes de suspeitar do ESP32. Porta 8377 precisa estar liberada no firewall (rede privada). Sem novas dependências pip — `urllib` é da stdlib.

**Pegadinha resolvida — `psutil.cpu_percent(interval=None)` é por-thread:** ele mede o uso desde a *última chamada feita naquela mesma thread*. O `ThreadingHTTPServer` cria uma thread nova a cada request, então chamar `cpu_percent` direto no handler faz cada request parecer "a primeira chamada" — sempre retorna 0.0. Por isso o CPU é medido dentro do `_io_monitor` (thread única e persistente) e cacheado em `_io_state["cpu_pct"]`; o handler só lê o cache. Se for adicionar qualquer outra métrica baseada em delta/estado interno do psutil, medir na thread de background, nunca no handler.

**Nota de dev (Windows + Git Bash):** rodar `python pc_stats_agent.py &` no Git Bash pode deixar processos zumbis presos na porta 8377 (o `HTTPServer`/`ThreadingHTTPServer` do Python tem `allow_reuse_address=True` por padrão, então o Windows deixa múltiplos processos "escutarem" a mesma porta sem erro, e as respostas saem de qualquer um deles). Se o `/stats` parecer não refletir mudanças no código, cheque `Get-CimInstance Win32_Process -Filter "Name='python.exe'"` e mate processos `pc_stats_agent.py` duplicados antes de reiniciar.

**Scripts de conveniência:** `agent/start_agent.bat` / `stop_agent.bat` / `restart_agent.bat` (duplo-clique no Windows) automatizam exatamente essa checagem/limpeza — `start` não duplica se já tem algo na porta 8377, `stop` mata todo processo cujo command line contenha `pc_stats_agent.py` (não só um PID, por causa da questão dos zumbis acima). O agente é um processo manual, não sobrevive a reboot — não tem serviço/agendador configurado por padrão. **Os `.bat` terminam com `pause`** — rodá-los de uma sessão não-interativa (agente de IA, CI) trava no final; nesse caso, iniciar o agente direto com `Start-Process python .../pc_stats_agent.py` redirecionando stdout/stderr pra log.

**Pegadinha de teste (Git Bash + `cmd.exe`):** ao testar esses `.bat` via Git Bash, `cmd.exe /c arquivo.bat` falha silenciosamente — o MSYS converte `/c` num path estilo Windows antes de repassar. Usar `cmd.exe //c arquivo.bat` (barra dupla) pra escapar, mesmo truque já usado com `taskkill //F //PID`. Isso é só uma pegadinha do ambiente de teste via Git Bash — não afeta o usuário final, que dá duplo-clique no `.bat` direto no Explorer.

## Notas

- `config.h` está versionado com credenciais reais preenchidas — cuidado ao compartilhar/commitar o diretório. Se for publicar, considere um `config.example.h`.
- Se o toque estiver deslocado, ajustar `TOUCH_MIN/MAX_X/Y` em `config.h` logando `p.x`/`p.y` do `getPoint()` no serial.
