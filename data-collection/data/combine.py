# program to combine all csv files in running directory, alphabetocally by their third column
import csv
import glob
import os

OUT = "combined.csv"

files = sorted(f for f in glob.glob("*.csv") if f != OUT)
if not files:
    raise SystemExit("no csv files found in " + os.getcwd())

# files may have different headers, so output the union of columns (first-seen order)
columns = []
rows = []
for path in files:
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for col in reader.fieldnames or []:
            if col not in columns:
                columns.append(col)
        header = reader.fieldnames
        third = header[2] if header and len(header) > 2 else None
        for row in reader:
            row["_key"] = row.get(third, "") if third else ""
            rows.append(row)
    print(f"read {path}")

# stable sort by each file's third column (timestamp, ISO so it sorts alphabetically)
rows.sort(key=lambda r: r["_key"])

with open(OUT, "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(rows)

print(f"wrote {len(rows)} rows from {len(files)} files to {OUT}")
