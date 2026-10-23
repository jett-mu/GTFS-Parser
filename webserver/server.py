from flask import Flask, jsonify, send_from_directory, request # type: ignore
import subprocess
import json
import os
import urllib.error
import urllib.parse
import urllib.request

app = Flask(__name__)

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

MAX_BETWEEN_IDS = 20  # per side of /api/between

@app.route('/api/between/<from_ids>/<to_ids>/<date>')
def get_between(from_ids, to_ids, date):
    """Direct trips from any stop in from_ids to any stop in to_ids (comma-separated stop_id lists)."""
    try:
        try:
            year, month, day = (int(part) for part in date.split('-'))
        except ValueError:
            return jsonify({'error': 'date must be YYYY-MM-DD'}), 400
        sides = [[s for s in ids.split(',') if s] for ids in (from_ids, to_ids)]
        if not all(sides):
            return jsonify({'error': 'need at least one stop on each side'}), 400
        if any(len(ids) > MAX_BETWEEN_IDS for ids in sides):
            return jsonify({'error': f'at most {MAX_BETWEEN_IDS} stops per side'}), 400
        frm, to = (','.join(urllib.parse.quote(s, safe='') for s in ids) for ids in sides)
        data, status = fast_get(f"/api/between/{frm}/{to}/{year}/{month}/{day}/")
        if status != 200:
            return jsonify(data), status
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

MAX_JOURNEYS = 40  # per /api/journeys call

@app.route('/api/journeys/<from_ids>/<to_ids>/<date>')
def get_journeys(from_ids, to_ids, date):
    """Journeys with transfers and short walks between stop_id lists; ?after=&until= are seconds after midnight, ?limit= journeys."""
    try:
        try:
            year, month, day = (int(part) for part in date.split('-'))
        except ValueError:
            return jsonify({'error': 'date must be YYYY-MM-DD'}), 400
        sides = [[s for s in ids.split(',') if s] for ids in (from_ids, to_ids)]
        if not all(sides):
            return jsonify({'error': 'need at least one stop on each side'}), 400
        if any(len(ids) > MAX_BETWEEN_IDS for ids in sides):
            return jsonify({'error': f'at most {MAX_BETWEEN_IDS} stops per side'}), 400
        params = {}
        for key in ('after', 'until', 'limit'):
            if key in request.args:
                try:
                    params[key] = int(request.args[key])
                except ValueError:
                    return jsonify({'error': f'{key} must be an integer'}), 400
        if 'limit' in params and not 1 <= params['limit'] <= MAX_JOURNEYS:
            return jsonify({'error': f'limit must be 1 to {MAX_JOURNEYS}'}), 400
        frm, to = (','.join(urllib.parse.quote(s, safe='') for s in ids) for ids in sides)
        query = f"?{urllib.parse.urlencode(params)}" if params else ''
        data, status = fast_get(f"/api/journeys/{frm}/{to}/{year}/{month}/{day}/{query}")
        if status != 200:
            return jsonify(data), status
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/stopinfo/<stop_id>')
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
def index():
    return send_from_directory('web', 'index.html')

@app.route('/depbd')
def departure_board():
    return send_from_directory('web', 'depboard.html')

@app.route('/rtebd')
def route_board():
    return send_from_directory('web', 'rteboard.html')

@app.route('/flipbd')
def flip_board():
    return send_from_directory('web', 'flipboard.html')

@app.route('/at-a-glance')
def at_a_glance():
    return send_from_directory('web', 'at-a-glance.html')

# Self-hosted fonts + Leaflet (no third-party requests from visitors' browsers)
@app.route('/vendor/<path:filename>')
def vendor(filename):
    return send_from_directory('vendor', filename)

# OPTIONAL: OFFLINE TILES
@app.route('/tiles/<int:z>/<int:x>/<int:y>.png')
def tiles(z, x, y):
    return send_from_directory(f'tiles/{z}/{x}', f'{y}.png')

@app.route('/api/routeinfo/<route_id>')
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


@app.route("/crash")
def home():
    raise Exception("test error") 


if __name__ == '__main__':
    app.run(debug=False, port=5015, host = 'localhost')
