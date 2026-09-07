import csv
import os

ROUTE_ID = "601"
GTFS_DIR = "/Users/jettmu/Developer/VSCode/GTFS Parser/static-gtfs/data/yrt_archive"
DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")


def main():
    trips = []
    shape_ids = set()
    with open(os.path.join(GTFS_DIR, "trips.txt"), newline="") as f:
        for row in csv.DictReader(f):
            if row["route_id"] != ROUTE_ID:
                continue
            trips.append((row["trip_id"], row["shape_id"]))
            shape_ids.add(row["shape_id"])

    shape_points = []
    with open(os.path.join(GTFS_DIR, "shapes.txt"), newline="") as f:
        for row in csv.DictReader(f):
            if row["shape_id"] not in shape_ids:
                continue
            shape_points.append(
                (row["shape_id"], row["shape_pt_lat"], row["shape_pt_lon"], row["shape_pt_sequence"])
            )
    shape_points.sort(key=lambda r: (r[0], int(r[3])))

    with open(os.path.join(DATA_DIR, "route_601_trips.csv"), "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["trip_id", "shape_id"])
        writer.writerows(trips)

    with open(os.path.join(DATA_DIR, "route_601_shapes.csv"), "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["shape_id", "lat", "lng", "seq"])
        for shape_id, lat, lng, seq in shape_points:
            writer.writerow([shape_id, lat, lng, seq])

    print(f"wrote {len(trips)} trips, {len(shape_points)} shape points across {len(shape_ids)} shapes")


if __name__ == "__main__":
    main()
