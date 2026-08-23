import csv

INPUT_FILE = "data.csv"
OUTPUT_FILE = "data.csv"
DELAY_MIN = -600
DELAY_MAX = 600


def main():
    with open(INPUT_FILE, newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        rows = [row for row in reader if DELAY_MIN <= int(row["delay"]) <= DELAY_MAX]

    with open(OUTPUT_FILE, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Kept {len(rows)} rows with delay in [{DELAY_MIN}, {DELAY_MAX}].")


if __name__ == "__main__":
    main()
