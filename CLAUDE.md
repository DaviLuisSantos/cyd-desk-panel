# CLAUDE.md

Orientações para agentes trabalhando neste repositório. Respostas e comentários de código em **português** (o projeto todo é em pt-BR).

## O que é

Firmware para o **ESP32-2432S028R (Cheap Yellow Display / CYD)** — um painel auxiliar de mesa. Três telas navegáveis por toque:

- **Relógio** (`screen_clock.h`) — hora/data via NTP. Herança do MVP inicial, não é foco (o Davi já tem outro dispositivo pra relógio/clima).
- **PC Stats** (`screen_pcstats.h`) — CPU, RAM, disco e rede do PC. É o coração do painel.
- **Cotações** (`screen_quotes.h`) — USD, AUD, BTC, SOL em BRL.

PC Stats e Cotações consultam um agente HTTP (`agent/pc_stats_agent.py`) rodando na máquina local (`/stats` e `/quotes`).

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

- **Paleta:** Dracula (`DRACULA_BG`, `DRACULA_FG`, `DRACULA_CURRENT`, `DRACULA_CYAN/GREEN/ORANGE/PINK/PURPLE/RED/YELLOW`), calculada em RGB565 e definida em `theme.h`. Nunca usar as constantes `TFT_*` do TFT_eSPI (TFT_BLACK, TFT_WHITE etc.) — sempre `DRACULA_*`.
- **Navegação por abas:** `ScreenManager` desenha uma tab bar de `TAB_BAR_H`=34px no topo com o ícone de cada `Screen` (via `drawIcon`). A aba ativa vira da cor do conteúdo (`DRACULA_BG`, efeito "aba conectada") com uma barrinha de destaque (`accentColor()` daquela tela) por baixo; as inativas ficam com ícone em `DRACULA_COMMENT` sobre fundo `DRACULA_CURRENT`. Tocar em qualquer ponto com `y < TAB_BAR_H` troca de aba direto (calcula o índice pela posição x); toque abaixo disso vai pro `onTouch` da tela ativa. Não tem mais swipe nas bordas — cada tela é acessada diretamente pela aba.
- **Auto-next:** o `ScreenManager` troca de aba sozinho a cada `AUTO_NEXT_MS` (`config.h`, padrão 10s) sem interação — dá pra deixar o painel "passando" as telas sozinho. Qualquer toque (numa aba ou no conteúdo) reinicia o timer via `lastActivityMs_`, então não interrompe quem tá mexendo. `AUTO_NEXT_MS = 0` desativa.
- **Ícones são 100% vetoriais** (`theme::iconClock/iconCpu/iconRam/iconDisk/iconNet/iconCoin/iconArrowUp/iconArrowDown`), desenhados com primitivas do TFT_eSPI (`drawCircle`, `fillTriangle`, etc.) — de propósito, pra não precisar de SPIFFS/LittleFS nem bitmaps embutidos no firmware. Recebem `cx, cy, size, color` — `size=8` é o padrão usado na tab bar; nas linhas de conteúdo usa-se algo maior (11-12) pra ganhar presença visual sem depender de bitmap. Se for adicionar um ícone novo, seguir esse padrão (função livre em `theme.h`, parametrizada por tamanho).
- **Distribuição vertical do conteúdo (linhas de PC Stats/Cotações):** as linhas usam a altura toda abaixo da tab bar, não só o topo — primeira linha com respiro generoso da tab bar (~40px), linhas espaçadas ~52-56px, indicador de status numa faixa final compacta perto do rodapé. Evitar deixar as linhas grudadas na tab bar com um vazio grande embaixo (problema já corrigido uma vez — layout muito apertado em cima e vazio embaixo é o erro mais fácil de reintroduzir ao adicionar uma linha nova).
- **Widgets reutilizáveis em `theme.h`:** `drawBar` (barra de progresso com cantos arredondados sobre trilho `DRACULA_CURRENT`), `drawPctBadge` (pill colorido verde/vermelho pra variação percentual, usado nas cotações), `drawStatusDot` (indicador de status discreto — dot colorido + label pequeno — substituiu os banners de texto centralizados tipo "agente offline"). Todos usam as variantes *smooth* do TFT_eSPI (`fillSmoothRoundRect`, `fillSmoothCircle`) — bordas com anti-aliasing de verdade, não só `fillRoundRect` cru. Essas funções recebem um `bg_color` explícito (a cor que já está por trás da forma) pra fazer o blend da borda corretamente; sempre passar a cor de fundo real do local onde a forma é desenhada, não deixar no default.
- **Smooth font (anti-aliased) nos labels estáticos:** `src/NotoSansBold15.h` é uma fonte suavizada (não é bitmap 1-bit) embutida como array em flash (sem precisar de SPIFFS/LittleFS — segue a mesma filosofia dos ícones vetoriais). Usada via `theme::beginLabelFont(tft)` / `theme::endLabelFont(tft)`, que fazem `tft.loadFont(...)`/`tft.unloadFont()`. **Importante:** enquanto uma smooth font está carregada, ela sobrepõe QUALQUER número de fonte clássica passado a `drawString` (é uma particularidade documentada do TFT_eSPI) — por isso só é usada ao redor de texto estático desenhado uma vez (labels em `onEnter`, data do relógio), nunca em volta de valores que atualizam com frequência (percentual das barras, preço, hora), que continuam nas fontes clássicas rápidas (2/4/6/7/8) com o padrão de `setTextPadding` de sempre. Se for redesenhar algo com smooth font no MESMO lugar depois (não é o caso hoje), lembrar do 3º parâmetro de `setTextColor(fg, bg, true)` — sem ele o fundo não é preenchido e o texto antigo não é apagado.
- **Animações leves, todas via `millis()` sem lib de animação:**
  - Relógio: dois-pontos piscando a cada 500ms (troca `:` por espaço no buffer, redesenha só quando o estado de piscar muda) + barrinha de progresso dos segundos dentro do minuto.
  - PC Stats: barras de CPU/RAM/disco fazem *easing* suave até o valor-alvo a cada tick do `loop()` (~20ms, fator 0.3 por tick), redesenhando só quando o percentual inteiro exibido muda — dá uma sensação de movimento sem gastar SPI à toa. O polling HTTP em si continua no intervalo de `AGENT_POLL_MS`; a animação roda independente disso.
- Cada tela expõe `accentColor()` (cor de identidade daquela aba — Relógio=CYAN, PC Stats=GREEN, Cotações=ORANGE) usada na tab bar e em detalhes do conteúdo (ex.: barra de segundos do relógio usa CYAN).

## Padrão de desenho de tela (importante)

Displays SPI são lentos — **nunca redesenhar a tela inteira em `update()`**. O padrão:

- `onEnter`: desenha o layout estático (títulos, labels) uma vez.
- `update`: atualiza só o que mudou, guardando o último valor em membro (`lastMinute_`, `lastDay_`, `drawn_`, etc.) e redesenhando só na mudança.
- Para apagar o valor antigo sem limpar a tela, usar `setTextPadding(largura)` antes do `drawString` e `setTextPadding(0)` depois.

Ver `screen_clock.h` (redesenha hora só quando o minuto muda) e `screen_pcstats.h`/`screen_quotes.h` (poll a cada `AGENT_POLL_MS`/`QUOTES_POLL_MS`, com `drawn_` pra forçar o primeiro desenho e status na base da tela pra erro de rede/agente).

## Adicionar uma tela nova

1. Criar `src/screen_minhatela.h` com uma classe que herda de `Screen`, implementando `name()`, `drawIcon()` (ícone da aba — usar/criar um helper em `theme.h`), `onEnter()`, `update()` e opcionalmente `accentColor()` (padrão é `DRACULA_PINK`).
2. Em `main.cpp`: incluir o header, instanciar global, e `manager.add(&minhaTela)` no `setup` (antes de `manager.begin`).
3. Usar sempre as cores/widgets de `theme.h`, nunca `TFT_*` cru — mantém a tela consistente com o tema Dracula das outras.

## Agente do PC

`agent/pc_stats_agent.py` — servidor HTTP (stdlib + `psutil`) na porta 8377, dois endpoints:

- `GET /stats` — CPU, RAM, disco (uso % + MB/s leitura/escrita) e rede (MB/s down/up). Rede e disco são medidos por delta em thread de background (`_io_monitor`, a cada `IO_INTERVAL`=1s).
- `GET /quotes` — cotações USD/AUD/BTC/SOL em BRL. Buscadas por thread de background (`_quotes_monitor`, a cada `QUOTES_INTERVAL`=60s) via AwesomeAPI (fiat) e CoinGecko (cripto), cacheadas em memória — o handler HTTP nunca bate na API externa por request. Se o fetch falhar, mantém o último valor conhecido e loga o erro (não derruba a thread).

Testar no navegador (`http://IP:8377/stats` e `/quotes`) antes de suspeitar do ESP32. Porta 8377 precisa estar liberada no firewall (rede privada). Sem novas dependências pip — `urllib` é da stdlib.

**Pegadinha resolvida — `psutil.cpu_percent(interval=None)` é por-thread:** ele mede o uso desde a *última chamada feita naquela mesma thread*. O `ThreadingHTTPServer` cria uma thread nova a cada request, então chamar `cpu_percent` direto no handler faz cada request parecer "a primeira chamada" — sempre retorna 0.0. Por isso o CPU é medido dentro do `_io_monitor` (thread única e persistente) e cacheado em `_io_state["cpu_pct"]`; o handler só lê o cache. Se for adicionar qualquer outra métrica baseada em delta/estado interno do psutil, medir na thread de background, nunca no handler.

**Nota de dev (Windows + Git Bash):** rodar `python pc_stats_agent.py &` no Git Bash pode deixar processos zumbis presos na porta 8377 (o `HTTPServer`/`ThreadingHTTPServer` do Python tem `allow_reuse_address=True` por padrão, então o Windows deixa múltiplos processos "escutarem" a mesma porta sem erro, e as respostas saem de qualquer um deles). Se o `/stats` parecer não refletir mudanças no código, cheque `Get-CimInstance Win32_Process -Filter "Name='python.exe'"` e mate processos `pc_stats_agent.py` duplicados antes de reiniciar.

**Scripts de conveniência:** `agent/start_agent.bat` / `stop_agent.bat` / `restart_agent.bat` (duplo-clique no Windows) automatizam exatamente essa checagem/limpeza — `start` não duplica se já tem algo na porta 8377, `stop` mata todo processo cujo command line contenha `pc_stats_agent.py` (não só um PID, por causa da questão dos zumbis acima). O agente é um processo manual, não sobrevive a reboot — não tem serviço/agendador configurado por padrão.

**Pegadinha de teste (Git Bash + `cmd.exe`):** ao testar esses `.bat` via Git Bash, `cmd.exe /c arquivo.bat` falha silenciosamente — o MSYS converte `/c` num path estilo Windows antes de repassar. Usar `cmd.exe //c arquivo.bat` (barra dupla) pra escapar, mesmo truque já usado com `taskkill //F //PID`. Isso é só uma pegadinha do ambiente de teste via Git Bash — não afeta o usuário final, que dá duplo-clique no `.bat` direto no Explorer.

## Notas

- `config.h` está versionado com credenciais reais preenchidas — cuidado ao compartilhar/commitar o diretório. Se for publicar, considere um `config.example.h`.
- Se o toque estiver deslocado, ajustar `TOUCH_MIN/MAX_X/Y` em `config.h` logando `p.x`/`p.y` do `getPoint()` no serial.
