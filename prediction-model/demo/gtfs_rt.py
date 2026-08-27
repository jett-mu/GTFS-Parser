import time
import urllib.request

from google.transit import gtfs_realtime_pb2

from trip_shape import route_601_trip_ids

VEHICLE_POSITIONS_URL = "https://rtu.york.ca/gtfsrealtime/VehiclePositions"
TRIP_UPDATES_URL = "https://rtu.york.ca/gtfsrealtime/TripUpdates"

ROUTE_ID = "601"
ROUTE_SHORT_NAME = "blue"

# Same feed-cache cadence the old C++ tools used.
MAX_AGE_SECONDS = 15

# Same asymmetric outlier bounds as before: an "early" report is far less
# likely to be a legitimate multi-minute swing than a "late" one (traffic,
# detours), so it's flagged sooner.
OUTLIER_MIN_DELAY_SECONDS = -10 * 60
OUTLIER_MAX_DELAY_SECONDS = 15 * 60

_cache = {}  # url -> (fetched_at, FeedMessage)


def _fetch_feed(url):
    now = time.time()
    cached = _cache.get(url)
    if cached and now - cached[0] <= MAX_AGE_SECONDS:
        return cached[1]

    try:
        req = urllib.request.Request(url, headers={"User-Agent": "gbdt-inference-demo/1.0"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = resp.read()
        feed = gtfs_realtime_pb2.FeedMessage()
        feed.ParseFromString(data)
        _cache[url] = (now, feed)
        return feed
    except Exception:
        if cached:
            return cached[1]  # serve stale data rather than erroring out
        raise


def _stop_time_update_delay(stu):
    if stu.arrival.HasField("delay"):
        return stu.arrival.delay
    if stu.departure.HasField("delay"):
        return stu.departure.delay
    return None


def _delay_by_trip():
    feed = _fetch_feed(TRIP_UPDATES_URL)
    result = {}
    for entity in feed.entity:
        if not entity.HasField("trip_update"):
            continue
        trip_id = entity.trip_update.trip.trip_id
        if not trip_id:
            continue
        for stu in entity.trip_update.stop_time_update:
            delay = _stop_time_update_delay(stu)
            if delay is not None:
                result[trip_id] = delay
                break  # one representative sample per trip
    return result


def get_route_vehicles():
    valid_trip_ids = route_601_trip_ids()
    vp_feed = _fetch_feed(VEHICLE_POSITIONS_URL)
    delays = _delay_by_trip()

    vehicles = []
    for entity in vp_feed.entity:
        if not entity.HasField("vehicle"):
            continue
        vp = entity.vehicle
        trip_id = vp.trip.trip_id
        if not trip_id or trip_id not in valid_trip_ids:
            continue
        if not vp.HasField("position"):
            continue

        vehicles.append({
            "trip_id": trip_id,
            "vehicle_id": vp.vehicle.id if vp.HasField("vehicle") else "",
            "vehicle_label": vp.vehicle.label if vp.HasField("vehicle") else "",
            "lat": vp.position.latitude,
            "lng": vp.position.longitude,
            "bearing": vp.position.bearing if vp.position.HasField("bearing") else -1,
            "delay": delays.get(trip_id),
        })

    return {
        "route_id": ROUTE_ID,
        "route_short_name": ROUTE_SHORT_NAME,
        "feed_timestamp": vp_feed.header.timestamp if vp_feed.header.HasField("timestamp") else 0,
        "vehicles": vehicles,
    }


def get_mean_delay():
    valid_trip_ids = route_601_trip_ids()
    delays = [
        d for trip_id, d in _delay_by_trip().items()
        if trip_id in valid_trip_ids
        and OUTLIER_MIN_DELAY_SECONDS <= d <= OUTLIER_MAX_DELAY_SECONDS
    ]

    n = len(delays)
    mean = (sum(delays) / n) if n > 0 else None

    return {
        "route_id": ROUTE_ID,
        "route_short_name": ROUTE_SHORT_NAME,
        "sample_count": n,
        "mean_delay": mean,
    }
