"""
Agente de stats do PC para o CYD Desk Panel.

Expõe:
  GET /stats   -> CPU, RAM, disco e velocidade de rede
  GET /quotes  -> cotações de moedas/cripto (USD, AUD, BTC, SOL) em BRL
  GET /procs   -> top 4 processos por uso de CPU (agregados por nome)
  GET /sysinfo -> latência (ping TCP), IP público e uptime do PC
  GET /youtube -> stats dos canais do YouTube (requer YOUTUBE_API_KEY)

Roda na máquina que o ESP32 vai consultar.

Uso:
    pip install psutil
    python pc_stats_agent.py
"""

import json
import os
import socket
import time
import threading
import unicodedata
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import psutil

PORT = 8377
IO_INTERVAL = 1.0        # janela de medição de rede/disco (segundos)
QUOTES_INTERVAL = 60.0   # intervalo de atualização das cotações (segundos)
PROCS_INTERVAL = 3.0     # intervalo de medição dos top processos (segundos)
SYS_INTERVAL = 5.0       # intervalo de medição do ping (segundos)
IP_INTERVAL = 600.0      # intervalo de atualização do IP público (segundos)
YT_INTERVAL = 1800.0     # intervalo de atualização do YouTube (segundos)
HTTP_TIMEOUT = 5.0

# ===== YouTube =====
# Crie uma chave em console.cloud.google.com (APIs & Services > Credentials)
# com a "YouTube Data API v3" habilitada e cole aqui. Sem chave, o /youtube
# responde {"configured": false} e a tela do painel mostra o aviso.
YOUTUBE_API_KEY = "AIzaSyD6Uim5cLBANkVyPa0jweMkjrVYUPdBbLov  "
YOUTUBE_CHANNEL_IDS = [
    "UCkm45JSC0beQPEpYYfX3XKA",  # Programador Filosofo
    "UCVjzHfxh-QBrvFXez6K7hHg",  # O Retardista (@oretardista)
]

MAIN_DRIVE = os.path.abspath(os.sep)  # "C:\\" no Windows, "/" no Linux/Mac

AWESOMEAPI_URL = "https://economia.awesomeapi.com.br/json/last/USD-BRL,AUD-BRL"
COINGECKO_URL = (
    "https://api.coingecko.com/api/v3/simple/price"
    "?ids=bitcoin,solana&vs_currencies=brl&include_24hr_change=true"
)

# ===== Medição de CPU + rede + disco em background (delta por segundo) =====
# cpu_percent(interval=None) mede o uso desde a ÚLTIMA CHAMADA NA MESMA THREAD.
# O ThreadingHTTPServer cria uma thread nova por request, então chamar isso
# direto no handler faz cada request parecer "a primeira chamada" e sempre
# retornar 0.0. Por isso medimos aqui, numa única thread persistente, e o
# handler só lê o valor cacheado.
_io_lock = threading.Lock()
_io_state = {
    "cpu_pct": 0.0,
    "net_down_mbps": 0.0,
    "net_up_mbps": 0.0,
    "disk_read_mbps": 0.0,
    "disk_write_mbps": 0.0,
    "disk_pct": 0.0,
}


def _io_monitor():
    psutil.cpu_percent(interval=None)  # prime (primeira chamada retorna 0, ignorar)
    prev_net = psutil.net_io_counters()
    prev_disk = psutil.disk_io_counters()
    while True:
        time.sleep(IO_INTERVAL)
        cpu_pct = psutil.cpu_percent(interval=None)
        cur_net = psutil.net_io_counters()
        down = (cur_net.bytes_recv - prev_net.bytes_recv) * 8 / IO_INTERVAL / 1_000_000
        up = (cur_net.bytes_sent - prev_net.bytes_sent) * 8 / IO_INTERVAL / 1_000_000
        prev_net = cur_net

        disk_read_mbps = 0.0
        disk_write_mbps = 0.0
        cur_disk = psutil.disk_io_counters()
        if cur_disk is not None and prev_disk is not None:
            disk_read_mbps = (cur_disk.read_bytes - prev_disk.read_bytes) / IO_INTERVAL / 1_000_000
            disk_write_mbps = (cur_disk.write_bytes - prev_disk.write_bytes) / IO_INTERVAL / 1_000_000
            prev_disk = cur_disk

        try:
            disk_pct = psutil.disk_usage(MAIN_DRIVE).percent
        except OSError:
            disk_pct = 0.0

        with _io_lock:
            _io_state["cpu_pct"] = cpu_pct
            _io_state["net_down_mbps"] = round(down, 2)
            _io_state["net_up_mbps"] = round(up, 2)
            _io_state["disk_read_mbps"] = round(disk_read_mbps, 2)
            _io_state["disk_write_mbps"] = round(disk_write_mbps, 2)
            _io_state["disk_pct"] = round(disk_pct, 1)


# ===== Cotações em background (evita bater na API externa a cada poll do ESP32) =====
_quotes_lock = threading.Lock()
_quotes_state = {
    "USD": {"brl": 0.0, "change_pct": 0.0},
    "AUD": {"brl": 0.0, "change_pct": 0.0},
    "BTC": {"brl": 0.0, "change_pct": 0.0},
    "SOL": {"brl": 0.0, "change_pct": 0.0},
    "updated_at": 0,
}


def _fetch_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "cyd-desk-panel-agent"})
    with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
        return json.loads(resp.read().decode())


def _to_ascii(s):
    # As fontes clássicas do painel não têm acentos — "Filósofo" vira "Filosofo"
    return unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()


def _fetch_text(url):
    req = urllib.request.Request(url, headers={"User-Agent": "cyd-desk-panel-agent"})
    with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
        return resp.read().decode().strip()


# ===== Top processos em background =====
# Mesma pegadinha do cpu_percent global: por-processo ele também mede desde a
# última chamada NAQUELE MESMO objeto Process. O process_iter() cacheia as
# instâncias internamente, então chamar em loop numa thread única funciona.
_procs_lock = threading.Lock()
_procs_state = {"procs": []}


def _procs_monitor():
    n_cpu = psutil.cpu_count() or 1
    skip = {"system idle process", "idle"}
    for p in psutil.process_iter():  # prime (primeira leitura é sempre 0)
        try:
            p.cpu_percent(None)
        except psutil.Error:
            pass
    while True:
        time.sleep(PROCS_INTERVAL)
        agg = {}
        for p in psutil.process_iter(attrs=["name", "memory_info"]):
            try:
                cpu = p.cpu_percent(None) / n_cpu
            except psutil.Error:
                continue
            name = p.info["name"] or "?"
            if name.lower() in skip:
                continue
            if name.lower().endswith(".exe"):
                name = name[:-4]
            entry = agg.setdefault(name, [0.0, 0])
            entry[0] += cpu
            mem = p.info["memory_info"]
            if mem:
                entry[1] += mem.rss
        top = sorted(agg.items(), key=lambda kv: kv[1][0], reverse=True)[:4]
        procs = [
            {"name": name[:20], "cpu": round(cpu, 1), "mem_mb": int(rss / 1048576)}
            for name, (cpu, rss) in top
        ]
        with _procs_lock:
            _procs_state["procs"] = procs


# ===== Ping + IP público em background =====
# Latência medida por TCP connect na porta 53 do 8.8.8.8 (ICMP puro exigiria
# admin no Windows; o connect TCP é um proxy bom o bastante pro painel).
_sys_lock = threading.Lock()
_sys_state = {"ping_ms": -1.0, "public_ip": "?"}


def _sys_monitor():
    last_ip_fetch = 0.0
    while True:
        t0 = time.perf_counter()
        try:
            conn = socket.create_connection(("8.8.8.8", 53), timeout=2.0)
            conn.close()
            ping_ms = round((time.perf_counter() - t0) * 1000, 1)
        except OSError:
            ping_ms = -1.0

        public_ip = None
        if time.time() - last_ip_fetch >= IP_INTERVAL:
            try:
                public_ip = _fetch_text("https://api.ipify.org")
                last_ip_fetch = time.time()
            except OSError as e:
                print(f"[sysinfo] falha ao buscar IP publico: {e}")

        with _sys_lock:
            _sys_state["ping_ms"] = ping_ms
            if public_ip:
                _sys_state["public_ip"] = public_ip

        time.sleep(SYS_INTERVAL)


# ===== YouTube em background =====
_yt_lock = threading.Lock()
_yt_state = {
    "configured": bool(YOUTUBE_API_KEY and YOUTUBE_CHANNEL_IDS),
    "channels": [],
    "updated_at": 0,
}


def _youtube_monitor():
    if not (YOUTUBE_API_KEY and YOUTUBE_CHANNEL_IDS):
        return
    url = (
        "https://www.googleapis.com/youtube/v3/channels"
        "?part=snippet,statistics&id=" + ",".join(YOUTUBE_CHANNEL_IDS)
        + "&key=" + YOUTUBE_API_KEY
    )
    while True:
        try:
            data = _fetch_json(url)
            channels = []
            for item in data.get("items", []):
                stats = item.get("statistics", {})
                channels.append({
                    "title": _to_ascii(item.get("snippet", {}).get("title", "?"))[:24],
                    "subs": int(stats.get("subscriberCount", 0)),
                    "views": int(stats.get("viewCount", 0)),
                    "videos": int(stats.get("videoCount", 0)),
                })
            with _yt_lock:
                _yt_state["channels"] = channels
                _yt_state["updated_at"] = int(time.time())
        except (OSError, ValueError, KeyError) as e:
            # Mantém os últimos valores conhecidos; só loga o problema
            print(f"[youtube] falha ao atualizar: {e}")

        time.sleep(YT_INTERVAL)


def _quotes_monitor():
    while True:
        try:
            fx = _fetch_json(AWESOMEAPI_URL)
            crypto = _fetch_json(COINGECKO_URL)

            with _quotes_lock:
                _quotes_state["USD"]["brl"] = round(float(fx["USDBRL"]["bid"]), 4)
                _quotes_state["USD"]["change_pct"] = round(float(fx["USDBRL"]["pctChange"]), 2)
                _quotes_state["AUD"]["brl"] = round(float(fx["AUDBRL"]["bid"]), 4)
                _quotes_state["AUD"]["change_pct"] = round(float(fx["AUDBRL"]["pctChange"]), 2)
                _quotes_state["BTC"]["brl"] = round(float(crypto["bitcoin"]["brl"]), 2)
                _quotes_state["BTC"]["change_pct"] = round(float(crypto["bitcoin"]["brl_24h_change"]), 2)
                _quotes_state["SOL"]["brl"] = round(float(crypto["solana"]["brl"]), 2)
                _quotes_state["SOL"]["change_pct"] = round(float(crypto["solana"]["brl_24h_change"]), 2)
                _quotes_state["updated_at"] = int(time.time())
        except (OSError, ValueError, KeyError) as e:
            # Mantém os últimos valores conhecidos; só loga o problema
            print(f"[quotes] falha ao atualizar: {e}")

        time.sleep(QUOTES_INTERVAL)


class StatsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/stats":
            self._reply(self._build_stats())
        elif self.path == "/quotes":
            with _quotes_lock:
                self._reply(dict(_quotes_state))
        elif self.path == "/procs":
            with _procs_lock:
                self._reply({"procs": list(_procs_state["procs"])})
        elif self.path == "/sysinfo":
            with _sys_lock:
                info = dict(_sys_state)
            info["uptime_s"] = int(time.time() - psutil.boot_time())
            with _io_lock:
                info["net_down_mbps"] = _io_state["net_down_mbps"]
                info["net_up_mbps"] = _io_state["net_up_mbps"]
            self._reply(info)
        elif self.path == "/youtube":
            with _yt_lock:
                self._reply({
                    "configured": _yt_state["configured"],
                    "channels": list(_yt_state["channels"]),
                    "updated_at": _yt_state["updated_at"],
                })
        else:
            self.send_response(404)
            self.end_headers()

    def _build_stats(self):
        with _io_lock:
            io = dict(_io_state)

        return {
            "cpu": io["cpu_pct"],
            "ram": psutil.virtual_memory().percent,
            "disk": io["disk_pct"],
            "disk_read_mbps": io["disk_read_mbps"],
            "disk_write_mbps": io["disk_write_mbps"],
            "net_down_mbps": io["net_down_mbps"],
            "net_up_mbps": io["net_up_mbps"],
        }

    def _reply(self, payload):
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass  # silencia log de cada request


def main():
    threading.Thread(target=_io_monitor, daemon=True).start()
    threading.Thread(target=_quotes_monitor, daemon=True).start()
    threading.Thread(target=_procs_monitor, daemon=True).start()
    threading.Thread(target=_sys_monitor, daemon=True).start()
    threading.Thread(target=_youtube_monitor, daemon=True).start()

    server = ThreadingHTTPServer(("0.0.0.0", PORT), StatsHandler)
    print(f"Agente rodando em http://0.0.0.0:{PORT} "
          "(/stats /quotes /procs /sysinfo /youtube)")
    server.serve_forever()


if __name__ == "__main__":
    main()
