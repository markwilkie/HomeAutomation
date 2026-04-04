"""Classify and filter listings using Azure OpenAI with a freeform prompt."""
import csv
import json
import os
import re
import sys
import requests

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"
filter_prompt = sys.argv[2] if len(sys.argv) > 2 else ""

# Azure OpenAI config from environment
endpoint = os.environ.get("AZURE_OPENAI_ENDPOINT", "").rstrip("/")
api_key = os.environ.get("AZURE_OPENAI_API_KEY", "")
deployment = os.environ.get("AZURE_OPENAI_DEPLOYMENT", "gpt-4.1")
api_version = os.environ.get("AZURE_OPENAI_API_VERSION", "2025-01-01-preview")

if filter_prompt and (not endpoint or not api_key):
    print("Set AZURE_OPENAI_ENDPOINT and AZURE_OPENAI_API_KEY environment variables")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Programmatic pre-filter -- built from searches.json pre_reject groups
# ---------------------------------------------------------------------------
_reject_groups = []  # list of (reject_rx, override_rx | None, group_name)

with open("searches.json", encoding="utf-8") as _sf:
    _searches = json.load(_sf)
    for _name, _cfg in _searches.items():
        if _name.lower() == prefix.lower():
            _pr = _cfg.get("pre_reject", {})
            if isinstance(_pr, dict):
                for _grp, _spec in _pr.items():
                    _rej = _spec.get("reject", "")
                    _ov = _spec.get("override", "")
                    if _rej:
                        _reject_rx = re.compile(
                            r"\b(?:" + _rej + r")\b", re.IGNORECASE
                        )
                        _override_rx = (
                            re.compile(r"\b(?:" + _ov + r")\b", re.IGNORECASE)
                            if _ov else None
                        )
                        _reject_groups.append((_reject_rx, _override_rx, _grp))
            break

if not filter_prompt and not _reject_groups:
    print("No filter prompt or pre_reject rules -- skipping filtering")
    sys.exit(0)


def programmatic_reject(row: dict) -> str | None:
    """Return rejection reason if any field matches a reject pattern, else None."""
    text = " ".join([
        row.get("trim", ""),
        row.get("description", ""),
        row.get("vin_data", ""),
    ])
    for reject_rx, override_rx, group in _reject_groups:
        m = reject_rx.search(text)
        if m:
            if override_rx and override_rx.search(text):
                continue  # conflicting terms -- let LLM decide
            return f"{group}: {m.group()}"
    return None

# Read CSV
csv_path = f"{OUT}/{prefix}_results_by_distance.csv"
rows = []
with open(csv_path, encoding="utf-8") as f:
    reader = csv.DictReader(f)
    fieldnames = list(reader.fieldnames)
    for row in reader:
        rows.append(row)

if not rows:
    print("No rows to classify")
    sys.exit(0)

# --- Phase 1: Programmatic pre-filter ---
pre_kept = []
pre_removed = []
for row in rows:
    reason = programmatic_reject(row)
    if reason:
        pre_removed.append(row)
        print(f"  PRE-DROP: {row['year']} {(row['trim'] or '--')[:30]:30s} | {reason}")
    else:
        pre_kept.append(row)

if pre_removed:
    print(f"\nPre-filter: removed {len(pre_removed)}, {len(pre_kept)} remain for LLM\n")

if not pre_kept:
    print("All rows removed by pre-filter")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
    print(f"Wrote 0 rows to {csv_path}")
    sys.exit(0)

# --- Phase 2: LLM classification (skip if no prompt) ---
if not filter_prompt:
    print(f"No LLM prompt -- writing {len(pre_kept)} rows after pre-filter")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(pre_kept)
    print(f"Wrote {len(pre_kept)} rows to {csv_path}")
    sys.exit(0)

print(f"Filtering {len(pre_kept)} listings with Azure OpenAI ({deployment})...")
print(f"  Filter: {filter_prompt}")

# Build listing summaries for the prompt
listings = []
for i, row in enumerate(pre_kept):
    listings.append({
        "id": i,
        "year": row["year"],
        "make": row["make"],
        "model": row["model"],
        "trim": row["trim"],
        "description": row.get("description", ""),
        "vin_data": row.get("vin_data", ""),
        "price": row["price"],
        "mileage": row["mileage"],
        "dealer": row["dealer"],
    })

prompt = f"""Apply this filter to each vehicle listing's trim, description, and vin_data:

{filter_prompt}

For each listing, return keep=true or keep=false with a short reason.

Return ONLY a JSON array:
[{{"id": 0, "keep": true, "reason": "short reason"}}, ...]

Listings:
{json.dumps(listings, indent=2)}"""

url = f"{endpoint}/openai/deployments/{deployment}/chat/completions?api-version={api_version}"
headers = {
    "Content-Type": "application/json",
    "api-key": api_key,
}
body = {
    "messages": [
        {"role": "system", "content": "You are a vehicle classification expert. Return only valid JSON, no markdown."},
        {"role": "user", "content": prompt},
    ],
    "temperature": 0,
    "max_tokens": 8000,
}

resp = requests.post(url, headers=headers, json=body, timeout=120)
resp.raise_for_status()
result = resp.json()
content = result["choices"][0]["message"]["content"].strip()

# Strip markdown code fences if present
if content.startswith("```"):
    content = content.split("\n", 1)[1]
if content.endswith("```"):
    content = content.rsplit("```", 1)[0]
content = content.strip()

classifications = json.loads(content)

# Apply classifications and filter
class_map = {c["id"]: c for c in classifications}
kept = []
removed = []
for i, row in enumerate(pre_kept):
    c = class_map.get(i, {"keep": True, "reason": ""})
    reason = c.get("reason", "")
    if c.get("keep", True):
        kept.append(row)
        print(f"  KEEP: {row['year']} {(row['trim'] or '--')[:30]:30s} | {reason}")
    else:
        removed.append(row)
        print(f"  DROP: {row['year']} {(row['trim'] or '--')[:30]:30s} | {reason}")

print(f"\nKept: {len(kept)}, Removed: {len(removed)} (LLM) + {len(pre_removed)} (pre-filter)")

# Write back (only kept rows)
with open(csv_path, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(kept)

print(f"Wrote {len(kept)} rows to {csv_path}")
