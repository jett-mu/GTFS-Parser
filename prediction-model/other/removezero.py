import csv

INPUT_FILE = "data.csv"
OUTPUT_FILE = "data.csv"


def main():
    with open(INPUT_FILE, newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        rows = [row for row in reader if int(row["delay"]) != 0]

    with open(OUTPUT_FILE, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Kept {len(rows)} rows with delay != 0.")


if __name__ == "__main__":
    main()
