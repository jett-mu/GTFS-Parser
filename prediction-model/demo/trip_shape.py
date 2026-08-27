import csv
import os
from collections import defaultdict

DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

_trip_to_shape = {}
_shape_points = defaultdict(list)  # shape_id -> [(seq, lat, lng), ...] sorted by seq


def _load():
    if _trip_to_shape:
        return

    with open(os.path.join(DATA_DIR, "route_601_trips.csv"), newline="") as f:
        for row in csv.DictReader(f):
            _trip_to_shape[row["trip_id"]] = row["shape_id"]

    with open(os.path.join(DATA_DIR, "route_601_shapes.csv"), newline="") as f:
        for row in csv.DictReader(f):
            _shape_points[row["shape_id"]].append(
                (int(row["seq"]), float(row["lat"]), float(row["lng"]))
            )
    for points in _shape_points.values():
        points.sort(key=lambda p: p[0])


def route_601_trip_ids():
    _load()
    return _trip_to_shape.keys()


def get_trip_shape(trip_id):
    _load()
    shape_id = _trip_to_shape.get(trip_id)
    if shape_id is None:
        return None
    return [
        {"lat": lat, "lng": lng, "sequence": seq}
        for seq, lat, lng in _shape_points[shape_id]
    ]
