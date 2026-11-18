#!/usr/bin/env python3
"""
Smart Route Planner - Backend Server (Python)
Supports Flask if installed, with seamless zero-dependency fallback to Python standard library http.server.

Endpoints:
  POST /api/route - Invokes compiled C route_cli.exe with JSON query
  GET  /api/map   - Returns map_data.json
  GET  /api/health- Health check
  GET  /          - Serves public/ frontend assets
"""

import sys
import os
import json
import subprocess

PORT = int(os.environ.get("PORT", 3000))
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BINARY_NAME = "route_cli.exe" if sys.platform == "win32" else "route_cli"
BINARY_PATH = os.path.join(BASE_DIR, BINARY_NAME)
MAP_FILE_PATH = os.path.join(BASE_DIR, "map_data.json")
PUBLIC_DIR = os.path.join(BASE_DIR, "public")

def execute_routing_engine(query_dict):
    """Invokes the compiled C binary and returns the parsed JSON dict."""
    if not os.path.exists(BINARY_PATH):
        return {
            "success": False,
            "error": f"C binary not found at '{BINARY_PATH}'. Please compile with 'make all'."
        }, 500

    payload = {
        "source": int(query_dict.get("source", -1)),
        "destination": int(query_dict.get("destination", -1)),
        "mode": query_dict.get("mode", "time"),
        "waypoints": [int(w) for w in query_dict.get("waypoints", [])],
        "alternatives": bool(query_dict.get("alternatives", False)),
        "traffic_updates": query_dict.get("traffic_updates", []),
        "map_file": MAP_FILE_PATH.replace("\\", "/")
    }

    try:
        proc = subprocess.Popen(
            [BINARY_PATH],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=BASE_DIR,
            text=True
        )
        stdout, stderr = proc.communicate(input=json.dumps(payload), timeout=10)
        
        if stderr and stderr.strip():
            print(f"[C Engine Stderr]: {stderr.strip()}", file=sys.stderr)

        data = json.loads(stdout.strip())
        status_code = 200 if data.get("success") else 422
        return data, status_code

    except subprocess.TimeoutExpired:
        proc.kill()
        return {"success": False, "error": "C routing engine timed out after 10 seconds."}, 504
    except json.JSONDecodeError as jde:
        return {
            "success": False,
            "error": f"Failed to parse C engine JSON output: {str(jde)}",
            "raw_output": stdout[:300] if 'stdout' in locals() else ""
        }, 500
    except Exception as ex:
        return {"success": False, "error": str(ex)}, 500


def run_with_flask():
    """Runs with Flask framework if installed."""
    from flask import Flask, request, jsonify, send_from_directory
    from flask_cors import CORS

    app = Flask(__name__, static_folder=PUBLIC_DIR)
    CORS(app)

    @app.route("/api/route", methods=["POST"])
    def route_api():
        data = request.get_json(force=True, silent=True) or {}
        if "source" not in data or "destination" not in data:
            return jsonify({
                "success": False,
                "error": "Missing required 'source' and 'destination' fields."
            }), 400
        res, status = execute_routing_engine(data)
        return jsonify(res), status

    @app.route("/api/map", methods=["GET"])
    def map_api():
        if os.path.exists(MAP_FILE_PATH):
            with open(MAP_FILE_PATH, "r", encoding="utf-8") as f:
                return jsonify(json.load(f))
        return jsonify({"success": False, "error": "map_data.json not found"}), 404

    @app.route("/api/health", methods=["GET"])
    def health_api():
        return jsonify({
            "status": "ok",
            "framework": "Flask",
            "binary_path": BINARY_PATH,
            "binary_exists": os.path.exists(BINARY_PATH)
        })

    @app.route("/", defaults={"path": "index.html"})
    @app.route("/<path:path>")
    def serve_static(path):
        return send_from_directory(PUBLIC_DIR, path)

    print(f"Starting Flask server on http://localhost:{PORT}")
    app.run(host="0.0.0.0", port=PORT, debug=False)


def run_with_builtin_http():
    """Fallback using Python's built-in standard library http.server."""
    from http.server import HTTPServer, SimpleHTTPRequestHandler
    import urllib.parse

    class RouteHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=PUBLIC_DIR, **kwargs)

        def _set_cors_headers(self):
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")

        def do_OPTIONS(self):
            self.send_response(204)
            self._set_cors_headers()
            self.end_headers()

        def do_POST(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path == "/api/route":
                content_len = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(content_len).decode("utf-8") if content_len > 0 else "{}"
                try:
                    query = json.loads(body)
                except Exception:
                    query = {}

                if "source" not in query or "destination" not in query:
                    self.send_response(400)
                    self._set_cors_headers()
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(json.dumps({
                        "success": False,
                        "error": "Missing 'source' or 'destination' parameters."
                    }).encode("utf-8"))
                    return

                res, status = execute_routing_engine(query)
                self.send_response(status)
                self._set_cors_headers()
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps(res, indent=2).encode("utf-8"))
                return

            self.send_response(404)
            self.end_headers()

        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path == "/api/map":
                if os.path.exists(MAP_FILE_PATH):
                    with open(MAP_FILE_PATH, "rb") as f:
                        data = f.read()
                    self.send_response(200)
                    self._set_cors_headers()
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(data)
                    return
                self.send_response(404)
                self.end_headers()
                return

            if parsed.path == "/api/health":
                self.send_response(200)
                self._set_cors_headers()
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({
                    "status": "ok",
                    "engine": "Python http.server + C route_cli",
                    "binary_exists": os.path.exists(BINARY_PATH)
                }).encode("utf-8"))
                return

            # Default to static file serving from public/
            super().do_GET()

    httpd = HTTPServer(("0.0.0.0", PORT), RouteHandler)
    print("=======================================================")
    print("  Smart Route Planner Python Server (Zero Dependencies)")
    print(f"  Local URL:   http://localhost:{PORT}")
    print(f"  Route API:   POST http://localhost:{PORT}/api/route")
    print(f"  Map Data:    GET  http://localhost:{PORT}/api/map")
    print(f"  C Engine:    {BINARY_PATH}")
    print("=======================================================")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server.")
        httpd.server_close()


if __name__ == "__main__":
    try:
        import flask
        run_with_flask()
    except ImportError:
        run_with_builtin_http()
