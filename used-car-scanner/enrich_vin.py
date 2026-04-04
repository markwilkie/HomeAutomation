"""Enrich vehicles with NHTSA VIN decode data.

Reads {prefix}_results_by_distance.csv, looks up each VIN via the free NHTSA
vPIC API, and writes decoded drivetrain/body/engine/doors info into the
vin_data column.
"""
import csv
import json
import re
import sys
import time

import requests

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"

NHTSA_URL = "https://vpic.nhtsa.dot.gov/api/vehicles/decodevin/{}?format=json"

# Fields we care about from NHTSA response
NHTSA_FIELDS = {
    "Drive Type": "Drive",
    "Body Class": "Body",
    "Cab Type": "Cab",
    "Doors": "Doors",
    "Number of Doors": "Doors",
    "Number of Seats": "Seats",
    "Displacement (L)": "EngineL",
    "Engine Number of Cylinders": "Cyl",
    "Fuel Type - Primary": "Fuel",
    "Transmission Style": "Trans",
}


def decode_vin(vin: str) -> str:
    """Call NHTSA vPIC API and return a compact spec string."""
    try:
        resp = requests.get(NHTSA_URL.format(vin), timeout=15)
        resp.raise_for_status()
        data = resp.json()
    except Exception as exc:
        print(f"    [!]  NHTSA error for {vin}: {exc}")
        return ""

    results = data.get("Results", [])
    specs = {}
    for item in results:
        var = item.get("Variable", "")
        val = (item.get("Value") or "").strip()
        if not val:
            continue
        if var in NHTSA_FIELDS:
            specs[NHTSA_FIELDS[var]] = val

    # Build compact string
    parts = []
    for label in ["Drive", "Body", "Cab", "Doors", "Seats", "EngineL", "Cyl", "Fuel", "Trans"]:
        if label in specs:
            parts.append(f"{label}: {specs[label]}")
    return " | ".join(parts)


def extract_vin_from_url(url: str) -> str:
    """Try to extract a VIN from common URL patterns."""
    m = re.search(r"[/-]([A-HJ-NPR-Z0-9]{17})(?:[/.\-?]|$)", url)
    return m.group(1) if m else ""


def main():
    csv_path = f"{OUT}/{prefix}_results_by_distance.csv"
    rows = []
    with open(csv_path, encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fieldnames = list(reader.fieldnames)
        for row in reader:
            rows.append(row)

    if not rows:
        print("No rows to enrich")
        return

    # Ensure vin and vin_data columns exist
    if "vin" not in fieldnames:
        fieldnames.append("vin")
    if "vin_data" not in fieldnames:
        fieldnames.append("vin_data")

    # Extract VINs from URLs where missing
    for row in rows:
        if not row.get("vin"):
            vin = extract_vin_from_url(row.get("url", ""))
            if vin:
                row["vin"] = vin

    vins_to_lookup = [r for r in rows if r.get("vin") and not r.get("vin_data")]
    print(f"VIN enrichment: {len(vins_to_lookup)} VINs to decode ({len(rows)} total)")

    # Deduplicate API calls -- same VIN only needs one lookup
    vin_cache: dict[str, str] = {}
    decoded = 0
    for row in vins_to_lookup:
        vin = row["vin"]
        if vin in vin_cache:
            row["vin_data"] = vin_cache[vin]
            if vin_cache[vin]:
                decoded += 1
            continue

        result = decode_vin(vin)
        vin_cache[vin] = result
        row["vin_data"] = result
        if result:
            decoded += 1
            print(f"    {vin}: {result}")
        time.sleep(0.3)  # polite rate limit

    print(f"  Decoded {decoded}/{len(vins_to_lookup)} VINs via NHTSA")

    # Write back
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {csv_path}")


if __name__ == "__main__":
    main()
