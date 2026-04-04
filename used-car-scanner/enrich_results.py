"""Enrich {prefix}_results.csv with distance from Lake Forest Park and dealer ratings."""
import csv
import json
import re
import sys
import time
import urllib.request
import urllib.parse

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"

# ── Driving-distance API (OSRM + Nominatim, free, no key) ──────────────
_HOME_LAT, _HOME_LON = 45.5152, -122.6784   # Portland, OR
_CACHE_FILE = "distance_cache.json"
_NOMINATIM_URL = "https://nominatim.openstreetmap.org/search"
_OSRM_URL = "https://router.project-osrm.org/route/v1/driving"

def _load_cache():
    try:
        with open(_CACHE_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}

def _save_cache(cache):
    with open(_CACHE_FILE, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=2)

def _geocode(city, state):
    """Geocode 'City, ST' via Nominatim -> (lat, lon) or None."""
    params = urllib.parse.urlencode({
        "q": f"{city}, {state}, USA",
        "format": "json",
        "limit": "1",
    })
    url = f"{_NOMINATIM_URL}?{params}"
    req = urllib.request.Request(url, headers={"User-Agent": "used-car-scanner/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read())
        if data:
            return float(data[0]["lat"]), float(data[0]["lon"])
    except Exception:
        pass
    return None

def _osrm_distance(lat, lon):
    """Driving distance in miles from Lake Forest Park to (lat, lon) via OSRM."""
    url = f"{_OSRM_URL}/{_HOME_LON},{_HOME_LAT};{lon},{lat}?overview=false"
    req = urllib.request.Request(url, headers={"User-Agent": "used-car-scanner/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read())
        if data.get("code") == "Ok" and data["routes"]:
            metres = data["routes"][0]["distance"]
            return round(metres / 1609.34)
    except Exception:
        pass
    return None

_dist_cache = _load_cache()
_cache_dirty = False

def lookup_driving_distance(city, state):
    """Return driving-distance miles from Lake Forest Park to City, ST.
    Uses a local JSON cache; calls Nominatim+OSRM only on miss."""
    global _cache_dirty
    key = f"{city}, {state}".lower()
    if key in _dist_cache:
        return _dist_cache[key]

    coords = _geocode(city, state)
    if coords is None:
        return None
    time.sleep(1)                       # Nominatim rate-limit: 1 req/s
    dist = _osrm_distance(*coords)
    if dist is not None:
        _dist_cache[key] = dist
        _cache_dirty = True
    return dist

def flush_cache():
    if _cache_dirty:
        _save_cache(_dist_cache)

# Fallback hardcoded distances (used only when API is unreachable)
CITY_DISTANCES = {
    "kenmore": 5, "bothell": 8, "mountlake-terrace": 6, "lynnwood": 8,
    "edmonds": 10, "kirkland": 7, "woodinville": 10, "redmond": 15,
    "seattle": 10, "shoreline": 5, "everett": 20, "bellevue": 15,
    "renton": 20, "auburn": 30, "kent": 25, "federal-way": 35,
    "tacoma": 40, "puyallup": 45, "spanaway": 50, "lakewood": 55,
    "olympia": 70, "shelton": 80, "mckenna": 80, "centralia": 90,
    "satsop": 100, "carrolls": 100, "mount-vernon": 60, "bellingham": 90,
    "gladstone": 180, "portland": 175, "eugene": 280, "spokane": 280,
    "yakima": 145, "burnaby": 150, "vancouver": 150,
    "monroe": 30, "marysville": 35, "snohomish": 25, "issaquah": 20,
    "sammamish": 20, "burien": 15, "tukwila": 15, "seatac": 18,
    "des-moines": 25, "bremerton": 60, "silverdale": 65, "poulsbo": 60,
    "gig-harbor": 55, "bonney-lake": 45, "enumclaw": 50, "maple-valley": 30,
    "covington": 25, "ellensburg": 110, "wenatchee": 130, "moses-lake": 180,
    "the-dalles": 190, "gresham": 175, "beaverton": 175, "reno": 700,
    "eastlake": 10, "fremont": 10, "ballard": 10, "greenwood": 5,
    "lake-city": 3, "northgate": 5, "university-district": 10, "capitol-hill": 12,
    "columbia-city": 15, "rainier": 15, "beacon-hill": 15, "georgetown": 15,
    "parker": 145,
    "salem": 280, "clackamas": 180, "battle-ground": 180, "fairfield": 300,
    "north-bend": 17, "woodburn": 260, "longview": 130, "chehalis": 95,
    "kellogg": 300,
    "arlington": 50, "fife": 35, "sumner": 40, "lake-stevens": 30,
    "graham": 55, "steilacoom": 55, "seabeck": 70, "eatonville": 65,
    "port-orchard": 65, "camano-island": 60,
    "university-place": 45, "newcastle": 15,
    "black-diamond": 40, "stanwood": 55, "burlington": 60,
    "sultan": 40, "gold-bar": 50, "lake-forest-park": 0, "kennewick": 220,
    "nampa": 450, "meridian": 450, "twin-falls": 550, "caldwell": 460,
    "boise": 450, "mountain-home": 500,
}

# Dealer distances from Lake Forest Park
DEALER_DISTANCES = {
    "Rairdon Auto Group": 25,
    "Advantage Nissan": 60,
    "Sound Ford": 20,
    "Seattle Lux Auto": 10,
    "Carter Subaru Shoreline": 2,
    "Toyota of Lake City": 3,
    "Honda of Seattle": 10,
    "Lee Johnson Chevrolet": 10,
    "Rodland Toyota of Everett": 15,
    "Younker Nissan": 15,
    "Campbell Nissan of Edmonds": 15,
    "Campbell Volkswagen of Edmonds": 15,
    "Burien Nissan": 15,
    "Bellevue Nissan": 12,
    "Nissan of Everett": 25,
    "Lee Johnson Nissan of Kirkland": 10,
    "Carter Subaru Ballard": 8,
    "Carter Volkswagen": 8,
    "Carter Acura of Lynnwood": 15,
    "Kendall Ford Marysville": 35,
    "Custom Trucks NW": 45,
    "Trucks Plus USA": 140,
    "Dave Smith Motors": 300,
    "Brotherton Buick GMC": 15,
    "Evergreen Ford": 20,
    "ZAG Motors": 12,
}

# Dealer ratings (1-10 scale based on reputation/reviews)
# 10 = excellent franchise dealer, 1 = very risky/unknown
DEALER_RATINGS = {
    "Rairdon Auto Group": 7,
    "Advantage Nissan": 7,
    "Sound Ford": 7,
    "Seattle Lux Auto": 6,
    "Craigslist Seattle": 4,
    "Facebook Marketplace": 3,
    "Carter Subaru Shoreline": 7,
    "Toyota of Lake City": 7,
    "Honda of Seattle": 7,
    "Lee Johnson Chevrolet": 7,
    "Rodland Toyota of Everett": 7,
    "Younker Nissan": 7,
}


def _city_distance(city, state="WA"):
    """Get driving distance for a city.  API first, hardcoded fallback."""
    city_slug = city.lower().replace(" ", "-")
    # Try API (returns cached result instantly if already looked up)
    api_dist = lookup_driving_distance(city, state)
    if api_dist is not None:
        return api_dist
    # Fallback to hardcoded table
    if city_slug in CITY_DISTANCES:
        return CITY_DISTANCES[city_slug]
    for key, dist in CITY_DISTANCES.items():
        if key in city_slug or city_slug in key:
            return dist
    return None


def get_cl_distance(url):
    """Extract city from Craigslist URL path and return (distance, city_name)."""
    m = re.search(r"craigslist\.org/.+?/d/([a-z][a-z-]+?)-\d", url)
    if m:
        city_slug = m.group(1)
        city_name = city_slug.replace("-", " ").title()
        dist = _city_distance(city_name)
        if dist is not None:
            return dist, city_name
    # Also check the subdomain for region
    region = re.search(r"https?://(\w+)\.craigslist", url)
    if region:
        subdomain = region.group(1)
        dist = _city_distance(subdomain.title())
        if dist is not None:
            return dist, subdomain.title() + " area"
    return None, None


def get_dealer_distance(dealer_name):
    """Estimate distance from dealer name by looking for city keywords."""
    name_lower = dealer_name.lower()
    # Check for city names embedded in dealer name
    city_keywords = {
        "bellingham": 90, "everett": 20, "monroe": 30, "lynnwood": 8,
        "kirkland": 7, "redmond": 15, "bellevue": 15, "renton": 20,
        "kent": 25, "auburn": 30, "tacoma": 40, "puyallup": 45,
        "olympia": 70, "bremerton": 60, "lakewood": 55,
        "seattle": 10, "shoreline": 5, "bothell": 8, "woodinville": 10,
        "lake city": 3, "eastlake": 10, "northgate": 5,
        "the dalles": 190, "gresham": 175, "beaverton": 175,
        "portland": 175, "eugene": 280, "spokane": 280, "yakima": 145,
        "reno": 700, "vancouver": 150, "marysville": 35,
    }
    for city, dist in city_keywords.items():
        if city in name_lower:
            return dist, city.title()
    return None, None


def main():
    rows = []
    with open(f"{OUT}/{prefix}_results.csv", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            dealer = row["dealer"]
            url = row["url"]

            # Use pre-extracted distance from scraper if available
            pre_dist = row.get("distance_mi", "").strip()
            if pre_dist and pre_dist.isdigit():
                dist = int(pre_dist)
                loc = dealer
            elif "facebook.com" in url:
                # Extract location from description (fb_location:City, ST ...)
                desc = row.get("description", "")
                fb_loc_m = re.match(r"fb_location:([^,]+),\s*(\w{2})\b", desc)
                if fb_loc_m:
                    city = fb_loc_m.group(1).strip()
                    state = fb_loc_m.group(2)
                    dist = _city_distance(city, state)
                    if dist is None:
                        dist = 40
                    loc = f"{city}, {state}"
                else:
                    # Try to find "City, ST" pattern anywhere in description
                    loc_m = re.search(r"([A-Z][a-z][a-z ]+),\s*([A-Z]{2})\b", desc)
                    if loc_m:
                        city = loc_m.group(1).strip()
                        state = loc_m.group(2)
                        dist = _city_distance(city, state)
                        if dist is None:
                            dist = 40
                        loc = f"{city}, {state}"
                    else:
                        dist = 40
                        loc = "FB Marketplace"
            elif dealer in DEALER_DISTANCES:
                dist = DEALER_DISTANCES[dealer]
                loc = dealer
            elif "craigslist" in url:
                dist, loc = get_cl_distance(url)
                if dist is None:
                    dist = 10
                    loc = "Seattle area"
            elif "autotrader.com" in url:
                # AutoTrader URLs have city/state params
                city_m = re.search(r"[?&]city=([^&]+)", url)
                state_m = re.search(r"[?&]state=([^&]+)", url)
                if city_m:
                    city_name = city_m.group(1).replace("+", " ")
                    state = state_m.group(1) if state_m else "WA"
                    dist = _city_distance(city_name, state)
                    if dist is None:
                        dist = 40
                    loc = city_name
                else:
                    dist = 40
                    loc = "Within 75 mi"
            else:
                # Cars.com and other aggregators -- try to get distance from dealer name
                dist, loc = get_dealer_distance(dealer)
                if dist is None:
                    dist = 40
                    loc = "Within 75 mi"

            rating = DEALER_RATINGS.get(dealer, 5)

            rows.append({
                **row,
                "distance_mi": dist,
                "location": loc,
                "dealer_rating": rating,
            })

    # Sort by distance, then by price
    rows.sort(key=lambda r: (r["distance_mi"], r.get("price", "")))

    with open(f"{OUT}/{prefix}_results_by_distance.csv", "w", newline="", encoding="utf-8") as f:
        fieldnames = [
            "dealer", "year", "make", "model", "trim", "price", "mileage",
            "distance_mi", "location", "dealer_rating", "url", "description",
            "vin", "vin_data", "image_url",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {OUT}/{prefix}_results_by_distance.csv\n")
    fmt = "{:>4} mi | {:<22} | {:<20} | {} {} {} {:<30} | {:<9} | {:<12} | {}/10"
    for r in rows:
        trim = (r["trim"] or "")[:30]
        print(fmt.format(
            r["distance_mi"], r["location"], r["dealer"],
            r["year"], r["make"], r["model"], trim,
            r["price"], r["mileage"], r["dealer_rating"],
        ))

    flush_cache()


if __name__ == "__main__":
    main()
