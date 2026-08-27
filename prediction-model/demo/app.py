import json
import os
from functools import wraps

from flask import Flask, jsonify, request, Response, send_from_directory

import gtfs_rt
import infer
import trip_shape

app = Flask(__name__, static_folder=None)

INDEX_HTML_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html")
FAVICONS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "favicons")


def load_env_tokens():
    env_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if not os.path.exists(env_path):
        return ""
    with open(env_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            if key.strip() == "DEMO_TOKENS":
                return value.strip().strip('"').strip("'")
    return ""


# Azure App Service (and most PaaS hosts) inject config as real environment
# variables, not a checked-in .env file -- prefer that when present.
# Comma-separated so multiple tokens (e.g. one per person) can be valid at once.
DEMO_TOKENS = {
    t.strip() for t in (os.environ.get("DEMO_TOKENS") or load_env_tokens()).split(",") if t.strip()
}


def require_token(fn):
    @wraps(fn)
    def wrapper(*args, **kwargs):
        if DEMO_TOKENS:
            supplied = request.headers.get("X-Auth-Token") or request.args.get("token")
            if supplied not in DEMO_TOKENS:
                return jsonify({"error": "invalid or missing token"}), 401
        return fn(*args, **kwargs)
    return wrapper


@app.route("/")
@require_token
def index():
    with open(INDEX_HTML_PATH) as f:
        html = f.read()
    token = (request.headers.get("X-Auth-Token") or request.args.get("token") or "") if DEMO_TOKENS else ""
    injected = f'<script>window.DEMO_TOKEN = {json.dumps(token)};</script>\n'
    html = html.replace("<head>", "<head>\n" + injected, 1)
    return Response(html, mimetype="text/html")


# Not token-gated: browsers request <link> favicons/manifest directly, with
# no way to attach the X-Auth-Token header, and there's nothing sensitive here.
@app.route("/favicons/<path:filename>")
def favicons(filename):
    return send_from_directory(FAVICONS_DIR, filename)


@app.route("/api/mean-delay")
@require_token
def mean_delay():
    try:
        return jsonify(gtfs_rt.get_mean_delay())
    except Exception as e:
        return jsonify({"error": str(e)}), 502


@app.route("/api/route-vehicles")
@require_token
def route_vehicles():
    try:
        return jsonify(gtfs_rt.get_route_vehicles())
    except Exception as e:
        return jsonify({"error": str(e)}), 502


@app.route("/api/trip-shape/<trip_id>")
@require_token
def trip_shape_route(trip_id):
    pos_markers = trip_shape.get_trip_shape(trip_id)
    if pos_markers is None:
        return jsonify({"error": "unknown trip_id"}), 404
    return jsonify({"trip_id": trip_id, "route_color": "#009CDB", "pos_markers": pos_markers})


@app.route("/api/predict")
@require_token
def predict():
    try:
        secs = float(request.args["secs"])
        weekend = int(request.args["weekend"])
        lag10 = request.args.get("lag10")
        lag30 = request.args.get("lag30")
        lag10 = float(lag10) if lag10 not in (None, "") else None
        lag30 = float(lag30) if lag30 not in (None, "") else None
    except (KeyError, ValueError):
        return jsonify({"error": "invalid or missing parameters"}), 422

    delay = infer.predict_delay(secs, lag10, lag30, weekend)
    return jsonify({"delay": delay})


if __name__ == "__main__":
    app.run(port=5017, debug=True)
