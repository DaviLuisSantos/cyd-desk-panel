"""
Agente de stats do PC para o CYD Desk Panel.

Expõe:
  GET /stats  -> CPU, RAM, disco e velocidade de rede
  GET /quotes -> cotações de moedas/cripto (USD, AUD, BTC, SOL) em BRL

Roda na máquina que o ESP32 vai consultar.

Uso:
    pip install psutil
    python pc_stats_agent.py
"""

import json
import os
import time
import threading
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import psutil

PORT = 8377
IO_INTERVAL = 1.0        # janela de medição de rede/disco (segundos)
QUOTES_INTERVAL = 60.0   # intervalo de atualização das cotações (segundos)
HTTP_TIMEOUT = 5.0

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

    server = ThreadingHTTPServer(("0.0.0.0", PORT), StatsHandler)
    print(f"Agente rodando em http://0.0.0.0:{PORT}/stats e /quotes")
    server.serve_forever()


if __name__ == "__main__":
    main()
