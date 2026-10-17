from flask import Flask, jsonify, send_from_directory, request, abort # type: ignore
import hmac
from functools import wraps
from t_confidental_info import token
import subprocess
import json
import os
import urllib.error
import urllib.parse
import urllib.request

app = Flask(__name__)

SECRET_TOKENS = [token] if isinstance(token, str) else list(token)

def require_token(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        supplied = request.headers.get("X-Auth-Token") or request.args.get("token") or ""
        if not any(hmac.compare_digest(supplied.encode(), t.encode()) for t in SECRET_TOKENS):
            abort(401)
        return f(*args, **kwargs)
    return decorated

# /api/trip and /api/stop are answered by fast-static-gtfs/webserver, which must be running (see its README).
FAST_SERVER = 'http://127.0.0.1:5016'
FAST_TIMEOUT = 10   # seconds
TOOL_TIMEOUT = 120  # seconds per tools/ or decoder run; getTrips alone takes ~10 s on YRT

def fast_get(path):
    """GET a fast-static-gtfs endpoint -> (data, status). Its own errors (e.g. 503 while loading) pass through."""
    try:
        with urllib.request.urlopen(FAST_SERVER + path, timeout=FAST_TIMEOUT) as resp:
            return json.loads(resp.read()), resp.status
    except urllib.error.HTTPError as e:
        try:
            return json.loads(e.read()), e.code
        except ValueError:
            return {'error': f'fast-static-gtfs returned HTTP {e.code}'}, e.code
    except (urllib.error.URLError, TimeoutError) as e:
        return {'error': f'fast-static-gtfs server at {FAST_SERVER} is not reachable: {e}'}, 503

@app.route('/api/trip/<trip_id>')
@require_token
def get_trip(trip_id):
    try:
        data, status = fast_get('/api/trip/' + urllib.parse.quote(trip_id, safe=''))
        if status != 200:
            return jsonify(data), status
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/stop/<stop_id>/<date>')
@require_token
def get_stop(stop_id, date):
    try:
        try:
            year, month, day = (int(part) for part in date.split('-'))
        except ValueError:
            return jsonify({'error': 'date must be YYYY-MM-DD'}), 400
        data, status = fast_get(f"/api/stop/{urllib.parse.quote(stop_id, safe='')}/{year}/{month}/{day}/")
        if status != 200:
            return jsonify(data), status
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/stopinfo/<stop_id>')
@require_token
def get_stop_info(stop_id):
    try:
        result = subprocess.run(['./tools/stopinfo', stop_id], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/feedstatus')
@app.route('/api/feedstatus/<year>/<month>/<day>')
@require_token
def get_feed_status(year=None, month=None, day=None):
    try:
        cmd = ['./tools/gtfsVerify']
        if year is not None:
            cmd += [year, month, day]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/searchstop/<path:query>')
@require_token
def search_stop(query):
    try:
        result = subprocess.run(['./tools/searchstop', query, '-t', '50'], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/searchroute/<path:query>')
@require_token
def search_route(query):
    try:
        result = subprocess.run(['./tools/searchroute', query, '-t', '50'], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/nearest/<lat>/<lon>')
@require_token
def get_nearest_stops(lat, lon):
    try:
        limit = request.args.get('limit', '10')
        result = subprocess.run(['./tools/getneareststopsjson', lat, lon, '-t', limit], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/rt/location/<tripID>')
@require_token
def get_rt_location(tripID):
    try:
        result = subprocess.run(['../gtfs-rt/proto-conversion/webserver-implementation/./decodeTrip', tripID], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500
    
@app.route('/api/rt/stop/<stopID>')
@require_token
def get_rt_stop(stopID):
    try:
        result = subprocess.run(['../gtfs-rt/proto-conversion/webserver-implementation/./decodeStop', stopID], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/rt/alerts')
@require_token
def get_rt_alerts():
    try:
        result = subprocess.run(['../gtfs-rt/proto-conversion/webserver-implementation/./decodeAlerts'], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/favicon.ico')
def favicon():
    return send_from_directory('.', 'favicon.ico')

@app.route('/')
@require_token
def index():
    return send_from_directory('tksweb', 'index.html')

@app.route('/depbd')
@require_token
def departure_board():
    return send_from_directory('tksweb', 'depboard.html')

@app.route('/rtebd')
@require_token
def route_board():
    return send_from_directory('tksweb', 'rteboard.html')

@app.route('/flipbd')
@require_token
def flip_board():
    return send_from_directory('tksweb', 'flipboard.html')

@app.route('/at-a-glance')
@require_token
def at_a_glance():
    return send_from_directory('tksweb', 'at-a-glance.html')

# Self-hosted fonts + Leaflet (no third-party requests from visitors' browsers)
@app.route('/vendor/<path:filename>')
def vendor(filename):
    return send_from_directory('vendor', filename)

# OPTIONAL: OFFLINE TILES
@app.route('/tiles/<int:z>/<int:x>/<int:y>.png')
@require_token
def tiles(z, x, y):
    return send_from_directory(f'tiles/{z}/{x}', f'{y}.png')

@app.route('/api/routeinfo/<route_id>')
@require_token
def get_route_info(route_id):
    try:
        result = subprocess.run(['./tools/routeinfo', route_id], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/route/<route_id>/<year>/<month>/<day>')
@require_token
def get_trip_root(route_id, year, month, day):
    try:
        result = subprocess.run(['./tools/getTrips', route_id, year, month, day], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500



if __name__ == '__main__':
    app.run(debug=False, port=5015, host='0.0.0.0')
