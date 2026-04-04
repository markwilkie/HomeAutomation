import csv
import sys

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"

with open(f"{OUT}/{prefix}_results.csv") as f:
    for r in csv.DictReader(f):
        print(f"{r['year']} {r['make']} {r['model']} {r['trim']}")
        print(f"  Price: {r['price']}  Mileage: {r['mileage']}")
        print(f"  Dealer: {r['dealer']}")
        print(f"  URL: {r['url']}")
        print()
