#!/usr/bin/env python3
"""Junction lab: what the radio's cluster shows for a junction, on the host.

  python3 tools/junction_lab/junction_lab.py            # http://localhost:8765
  python3 tools/junction_lab/junction_lab.py --route 50.4501,30.5234 50.4547,30.5238

Routes FROM -> TO with OsmAnd's router (host build, junction_lab_tool) and
shows each manoeuvre's cluster symbol, computed by the backend's own code.
Put FROM on the street you come from and TO on the street you leave by, a
little past the junction. The page also re-runs a manoeuvre's junction with
edited angles (junction_probe), to try the classifier without a route.

Build first: tools/junction_lab/build.sh
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BUILD = ROOT / "build" / "junction_lab"
DEFAULT_MAPS = Path("/mnt/d/osmand-maps/ukraine")

KINDS = {"straight", "left", "slight_left", "sharp_left", "right", "slight_right",
         "sharp_right", "keep_left", "keep_right", "uturn", "roundabout", "arrive"}


class Router:
    """One junction_lab_tool process (the maps stay open between routes)."""

    def __init__(self, tool: Path, maps: Path, routing_xml: Path):
        self.process = subprocess.Popen(
            [str(tool), str(maps), str(routing_xml)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        ready = self.process.stdout.readline()
        if not ready:
            raise SystemExit("junction_lab_tool exited (see its message above)")
        self.lock = threading.Lock()

    def route(self, a: tuple[float, float], b: tuple[float, float]) -> dict:
        with self.lock:
            self.process.stdin.write(f"{a[0]} {a[1]} {b[0]} {b[1]}\n")
            self.process.stdin.flush()
            line = self.process.stdout.readline()
        if not line:
            return {"ok": False, "error": "junction_lab_tool exited"}
        return json.loads(line)


def probe(params: dict[str, str]) -> dict:
    kind = params.get("kind", "")
    if kind not in KINDS:
        return {"error": f"unknown kind {kind!r}"}
    args = [str(BUILD / "junction_probe"), "--kind", kind,
            "--route", str(float(params.get("route", "0"))),
            "--arms=" + ",".join(str(float(a)) for a in params.get("arms", "").split(",") if a.strip()),
            "--turn-angle", str(float(params.get("turn_angle", "0")))]
    if params.get("lanes"):
        args += ["--lanes", "".join(c for c in params["lanes"] if c in ".*")]
    if params.get("left_side") == "1":
        args.append("--left-side")
    result = subprocess.run(args, capture_output=True, text=True, timeout=10)
    if result.returncode != 0:
        return {"error": result.stderr.strip()}
    return json.loads(result.stdout)


def parse_point(text: str) -> tuple[float, float]:
    lat, lon = (float(v) for v in text.replace(" ", "").split(","))
    return lat, lon


def make_handler(router: Router):
    page = (HERE / "index.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def send(self, code: int, body: bytes, kind: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            url = urlparse(self.path)
            params = {k: v[0] for k, v in parse_qs(url.query).items()}
            try:
                if url.path in ("/", "/index.html"):
                    self.send(200, page, "text/html; charset=utf-8")
                    return
                if url.path == "/api/route":
                    result = router.route(parse_point(params["from"]), parse_point(params["to"]))
                elif url.path == "/api/probe":
                    result = probe(params)
                else:
                    self.send(404, b"not found", "text/plain")
                    return
            except (KeyError, ValueError) as error:
                result = {"ok": False, "error": f"bad request: {error}"}
            self.send(200, json.dumps(result, ensure_ascii=False).encode(), "application/json")

        def log_message(self, *args) -> None:
            pass

    return Handler


def describe(result: dict) -> str:
    if not result.get("ok"):
        return f"no route: {result.get('error')}"
    lines = []
    for m in result["maneuvers"]:
        j = m["junction"]
        arms = ",".join(f"{a:.0f}" for a in j["branches"])
        lines.append(
            f"{m['lat']:.6f},{m['lon']:.6f} {m['turn']:<6} kind={m['kind']} angle={m['turn_angle']:.0f} "
            f"route={j['route_angle']:.0f} arms={arms or '-'}{'+' if j['truncated'] else ''} "
            f"lanes={m['lanes'] or '-'} -> junction={m['junction_symbol']} "
            f"turn_type={m['turn_type_symbol']} SYMBOL={m['symbol']}  road={m['road']!r}")
    return "\n".join(lines) or "route has no manoeuvres"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--maps", type=Path, default=DEFAULT_MAPS, help="an .obf or a directory of them")
    parser.add_argument("--routing-xml", type=Path, default=BUILD / "routing.xml")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--route", nargs=2, metavar=("FROM", "TO"), help="print the manoeuvres and exit")
    args = parser.parse_args()
    tool = BUILD / "junction_lab_tool"
    if not tool.exists():
        print(f"{tool} missing: run tools/junction_lab/build.sh", file=sys.stderr)
        return 1
    print(f"opening maps in {args.maps} ...", file=sys.stderr)
    router = Router(tool, args.maps, args.routing_xml)
    if args.route:
        print(describe(router.route(parse_point(args.route[0]), parse_point(args.route[1]))))
        return 0
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(router))
    print(f"junction lab: http://localhost:{args.port}/", file=sys.stderr)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
