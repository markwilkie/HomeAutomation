"""
Match AutoTrader listings to Cars.com/other source listings by price+year+mileage
to identify the actual dealer. For remaining unmatched ones, also try year+price only.
Then update the CSV.
"""
import csv
import re
import sys

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"

rows = []
with open(f"{OUT}/{prefix}_results_by_distance.csv", encoding="utf-8") as f:
    reader = csv.DictReader(f)
    fieldnames = reader.fieldnames
    for row in reader:
        rows.append(row)

def parse_num(s):
    cleaned = re.sub(r"[^\d]", "", s)
    return int(cleaned) if cleaned else 0

# Build index of non-AutoTrader listings
known_exact = {}   # year|price|mileage_rounded -> dealer
known_price = {}   # year|price -> dealer
for r in rows:
    if r["dealer"] == "AutoTrader":
        continue
    price = parse_num(r["price"])
    mileage = parse_num(r["mileage"])
    year = r["year"]
    dealer = r["dealer"]
    if price and year:
        if mileage:
            rounded_mi = (mileage // 1000) * 1000
            known_exact[f"{year}|{price}|{rounded_mi}"] = dealer
        known_price[f"{year}|{price}"] = dealer

print(f"Index: {len(known_exact)} exact, {len(known_price)} price-only\n")

# Match and update AutoTrader listings
matched = 0
unmatched_rows = []
for r in rows:
    if r["dealer"] != "AutoTrader":
        continue
    price = parse_num(r["price"])
    mileage = parse_num(r["mileage"])
    year = r["year"]
    
    rounded_mi = (mileage // 1000) * 1000 if mileage else 0
    
    dealer = None
    # Try exact match
    if rounded_mi:
        dealer = known_exact.get(f"{year}|{price}|{rounded_mi}")
    # Try price-only match
    if not dealer:
        dealer = known_price.get(f"{year}|{price}")
    
    if dealer:
        print(f"  MATCH: {year} ${price:,} {mileage:,}mi -> {dealer}")
        r["dealer"] = dealer
        # Update the source indicator -- append "(via AT)" to note it came from AutoTrader
        matched += 1
    else:
        lid = re.search(r"vehicle/(\d+)", r["url"])
        lid = lid.group(1) if lid else "?"
        print(f"  UNMATCHED: {year} ${price:,} {mileage:,}mi (listing {lid})")
        unmatched_rows.append(r)

print(f"\nMatched: {matched}, Unmatched: {len(unmatched_rows)}")

# Write updated CSV
with open(f"{OUT}/{prefix}_results_by_distance.csv", "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

print(f"Updated {OUT}/{prefix}_results_by_distance.csv")
