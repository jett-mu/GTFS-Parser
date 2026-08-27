import json
import os
import subprocess
import sys

from flask import Flask, jsonify, request, send_from_directory

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import infer

app = Flask(__name__, static_folder=None)

ROUTE_MEDIAN_DELAY_TOOL = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "gtfs-rt", "proto-conversion", "webserver-implementation", "routeMedianDelay",
)


@app.route("/")
def index():
    return send_from_directory(os.path.dirname(os.path.abspath(__file__)), "index.html")


@app.route("/api/median-delay")
def median_delay():
    result = subprocess.run(
        [ROUTE_MEDIAN_DELAY_TOOL, "601"], capture_output=True, text=True
    )
    if result.returncode != 0 or not result.stdout.strip():
        return jsonify({"error": result.stderr.strip() or "tool failed"}), 502
    return jsonify(json.loads(result.stdout))


@app.route("/api/predict")
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
