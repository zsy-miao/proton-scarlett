#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from http.server import BaseHTTPRequestHandler, HTTPServer
import os
from pathlib import Path
import subprocess
import sys
from threading import Thread


class Echo(BaseHTTPRequestHandler):
    def do_POST(self):
        if (self.path != "/echo" or self.headers.get("X-Probe") != "request"
                or self.headers.get("Content-Length") != "5"):
            self.send_error(400)
            return
        body = b"echo:" + self.rfile.read(5)
        self.send_response(201)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Probe", "response")
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    [directory] = sys.argv[1:]
    directory = Path(directory).resolve()
    exports = Path(__file__).with_name("required-exports.txt").read_text().splitlines()
    with HTTPServer(("127.0.0.1", 0), Echo) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        subprocess.run(["wine", "probe.exe", f"http://127.0.0.1:{server.server_port}/echo", *exports],
                       cwd=directory, timeout=120, check=True,
                       env={**os.environ, "WINEPREFIX": str(directory.parent / "test-prefix"),
                            "WINEDLLOVERRIDES": "mscoree,mshtml=;libHttpClient.GDK=n"})
