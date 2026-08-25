from flask import Flask, jsonify, send_from_directory, request, abort # type: ignore
from functools import wraps
from t_confidental_info import token
import subprocess
import json
import os

app = Flask(__name__)

SECRET_TOKENS = token

def require_token(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        token = request.headers.get("X-Auth-Token") or request.args.get("token")
        if token not in SECRET_TOKENS:
            abort(401)
        return f(*args, **kwargs)
    return decorated

@app.route('/api/trip/<trip_id>')
@require_token
def get_trip(trip_id):
    try:
        result = subprocess.run(['curl', 'http://localhost:5016/api/trip/' + trip_id], capture_output=True, text=True)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/stop/<stop_id>/<date>')
@require_token
def get_stop(stop_id, date):
    try:
        year, month, day = date.split('-')
        result = subprocess.run(['./tools/stopjson', stop_id, year, month, day], capture_output=True, text=True)
        if result.returncode != 0:
            return jsonify({'error': result.stderr}), 500
        data = json.loads(result.stdout)
        if not data:
            return jsonify(data), 422
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/stopinfo/<stop_id>')
@require_token
def get_stop_info(stop_id):
    try:
        result = subprocess.run(['./tools/stopinfo', stop_id], capture_output=True, text=True)
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
        result = subprocess.run(['./tools/searchstop', query, '-t', '50'], capture_output=True, text=True)
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
        result = subprocess.run(['./tools/searchroute', query, '-t', '50'], capture_output=True, text=True)
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
        result = subprocess.run(['./tools/getneareststopsjson', lat, lon, '-t', limit], capture_output=True, text=True)
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
        result = subprocess.run(['../gtfs-rt/proto-conversion/webserver-implementation/./decodeTrip', tripID], capture_output=True, text=True)
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
        result = subprocess.run(['../gtfs-rt/proto-conversion/webserver-implementation/./decodeStop', stopID], capture_output=True, text=True)
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
    return send_from_directory('.', 'tokenindex.html')

@app.route('/<path:path>')
@require_token
def static_files(path):
    return send_from_directory('.', path)

# OPTIONAL: OFFLINE TILES
@app.route('/tiles/<int:z>/<int:x>/<int:y>.png')
@require_token
def tiles(z, x, y):
    return send_from_directory(f'tiles/{z}/{x}', f'{y}.png')

@app.route('/api/route/<route_id>/<year>/<month>/<day>')
@require_token
def get_trip_root(route_id, year, month, day):
    try:
        result = subprocess.run(['./tools/getTrips', route_id, year, month, day], capture_output=True, text=True)
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
