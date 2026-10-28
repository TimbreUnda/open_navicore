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
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BUILD = ROOT / "build" / "junction_lab"
# $OPEN_NAVICORE_MAPS, else osmand-maps/ukraine next to this repository.
DEFAULT_MAPS = Path(os.environ.get("OPEN_NAVICORE_MAPS", ROOT.parent / "osmand-maps" / "ukraine"))

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
    """The radio's manoeuvres (Organic Maps' turns) with their cluster symbols."""
    if not result.get("ok"):
        return f"no route: {result.get('error')}"
    if result.get("omaps_error"):
        return f"Organic Maps failed (the radio shows no turns): {result['omaps_error']}"
    lines = []
    for m in result.get("omaps", []):
        j = m["junction"]
        arms = ",".join(f"{a:.0f}" for a in j["branches"])
        lines.append(
            f"{m['lat']:.6f},{m['lon']:.6f} {m['turn']:<20} kind={m['kind']} "
            f"route={j['route_angle']:.0f} arms={arms or '-'}{'+' if j['truncated'] else ''} "
            f"-> junction={m['junction_symbol']} turn_type={m['turn_type_symbol']} SYMBOL={m['symbol']}"
            f"  road={m['road']!r}")
    return "\n".join(lines) or "route has no manoeuvres"


def check(router: Router, path: Path) -> int:
    """Runs every case of a cases.tsv; returns the number that differ."""
    failed = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        start, finish, expected, why = (line.split("\t") + ["", ""])[:4]
        result = router.route(parse_point(start), parse_point(finish))
        # A token is OM's direction, or DIRECTION/kind to also pin the backend's
        # manoeuvre kind (e.g. TurnSlightLeft/keep_left).
        tokens = expected.split()
        turns = result.get("omaps", []) if result.get("ok") else []
        got_tokens = [m["turn"] + ("/" + m["kind"] if i < len(tokens) and "/" in tokens[i] else "")
                      for i, m in enumerate(turns)]
        got = " ".join(got_tokens) if result.get("ok") else "no route"
        if result.get("omaps_error"):
            got = "omaps_error: " + result["omaps_error"]
        ok = got == " ".join(tokens)
        failed += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {start} {finish}  {why}")
        if not ok:
            print(f"     expected: {expected.strip()}\n     got:      {got}")
    print(f"{failed} case(s) differ" if failed else "all cases match")
    return failed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--maps", type=Path, default=DEFAULT_MAPS, help="an .obf or a directory of them")
    parser.add_argument("--routing-xml", type=Path, default=BUILD / "routing.xml")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--route", nargs=2, metavar=("FROM", "TO"), help="print the manoeuvres and exit")
    parser.add_argument("--check", type=Path, metavar="CASES", help="run a cases.tsv and exit (1 if any differ)")
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
    if args.check:
        return 1 if check(router, args.check) else 0
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(router))
    print(f"junction lab: http://localhost:{args.port}/", file=sys.stderr)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
