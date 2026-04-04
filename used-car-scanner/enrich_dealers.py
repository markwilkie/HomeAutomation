"""
Enrich {prefix}_results_by_distance.csv by scraping each Craigslist listing
to extract the actual dealer/seller name and whether it's a dealer or private.
Then rate each seller individually.
"""
import csv
import re
import sys
import time
import requests
from bs4 import BeautifulSoup

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"

SESSION = requests.Session()
SESSION.headers.update({
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Safari/537.36"
    ),
    "Accept": "text/html,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9",
})

# Known dealer ratings (1-10)
KNOWN_RATINGS = {
    "Rairdon Auto Group": 7,
    "Advantage Nissan": 7,
    "Sound Ford": 7,
    "Seattle Lux Auto": 6,
    "Carter Subaru Shoreline": 7,
    "Toyota of Lake City": 7,
    "Honda of Seattle": 7,
    "Lee Johnson Chevrolet": 7,
    "Rodland Toyota of Everett": 7,
    "Younker Nissan": 7,
    "Campbell Nissan of Edmonds": 7,
    "Campbell Volkswagen of Edmonds": 7,
    "Burien Nissan": 7,
    "Bellevue Nissan": 7,
    "Nissan of Everett": 7,
    "Lee Johnson Nissan of Kirkland": 7,
    "Carter Subaru Ballard": 7,
    "Carter Volkswagen": 7,
    "Carter Acura of Lynnwood": 7,
    "Kendall Ford Marysville": 7,
    "Custom Trucks NW": 6,
    "Trucks Plus USA": 6,
    "Dave Smith Motors": 7,
    "Brotherton Buick GMC": 7,
    "Evergreen Ford": 7,
    "ZAG Motors": 6,
}


KNOWN_LOCATIONS = {
    "seattle", "kenmore", "bothell", "lynnwood", "edmonds", "kirkland",
    "woodinville", "redmond", "bellevue", "renton", "auburn", "kent",
    "federal way", "tacoma", "puyallup", "spanaway", "lakewood", "olympia",
    "shelton", "mckenna", "satsop", "carrolls", "mount vernon", "bellingham",
    "gladstone", "portland", "eugene", "spokane", "yakima", "burnaby",
    "vancouver", "everett", "shoreline", "mountlake terrace", "montesano",
    "edmunds", "marysville", "issaquah", "kellogg",
}


def _clean_name(name: str) -> str:
    """Clean up extracted dealer name artifacts."""
    if not name:
        return name
    # Strip leading +, *, -, bullets
    name = re.sub(r"^[+*\-•►▶\s]+", "", name).strip()
    # Strip underscores used for emphasis
    name = name.replace("_", "").strip()
    # Remove "QR Code Link to This Post" prefix
    name = re.sub(r"^QR Code Link to This Post\s*", "", name, flags=re.IGNORECASE).strip()
    return name


def _is_location_name(name: str) -> bool:
    """Check if a name is actually a city/location, not a business."""
    cleaned = re.sub(r",?\s*(WA|OR|BC|CA)\s*$", "", name, flags=re.IGNORECASE).strip()
    return cleaned.lower() in KNOWN_LOCATIONS


def _is_vehicle_name(name: str) -> bool:
    """Check if a name is actually a vehicle description, not a business."""
    makes = ["nissan", "toyota", "honda", "ford", "chevrolet", "subaru"]
    models = ["frontier", "tacoma", "ranger", "colorado", "ridgeline"]
    lower = name.lower()
    # If it starts with a year or make+model, it's a vehicle name
    if re.match(r"^\d{4}\s", name):
        return True
    for make in makes:
        for model in models:
            if make in lower and model in lower:
                return True
    # Just the make name alone (< 10 chars)
    if lower in makes and len(lower) < 10:
        return True
    # All-caps taglines/slogans (e.g. "OVER 200 TRUCKS IN STOCK")
    if name == name.upper() and len(name.split()) > 3:
        return True
    return False


def scrape_cl_listing(url: str) -> dict:
    """Scrape a single CL listing page to extract dealer/seller info."""
    result = {"seller_name": "", "seller_type": "unknown"}
    try:
        resp = SESSION.get(url, timeout=20, allow_redirects=True)
        resp.raise_for_status()
    except Exception as e:
        print(f"    FAIL {url}: {e}")
        return result

    soup = BeautifulSoup(resp.text, "lxml")

    # 1) Determine seller type from "for sale by ..." link on the page
    for a in soup.select("a"):
        link_text = a.get_text(strip=True).lower()
        if "for sale by dealer" in link_text:
            result["seller_type"] = "dealer"
            break
        elif "for sale by owner" in link_text:
            result["seller_type"] = "private"
            break
    else:
        # Fallback to URL pattern: /ctd/ = dealer, /cto/ = owner
        if "/ctd/" in url:
            result["seller_type"] = "dealer"
        elif "/cto/" in url:
            result["seller_type"] = "private"

    # 2) Extract dealer name from the posting body (first line is often the biz name)
    body = soup.select_one("#postingbody")
    if body:
        # Remove QR code / print elements
        for el in body.select(".print-information, .print-qrcode-container, .print-qrcode-label"):
            el.decompose()
        body_text = body.get_text(" ", strip=True).strip()

        if result["seller_type"] == "dealer" or result["seller_type"] == "unknown":
            dealer_patterns = [
                # Name ending with dealer-like suffix, before STOCK/CALL/phone
                r"^([A-Z][A-Za-z\s&'.\-]{2,40}(?:Auto\s*Sales|Motor\s*Sales|Auto\s*LLC|Motors?\s*LLC|Auto\s*Group|Car\s*Sales|Car\s*Corner|Auto\s*Brokers?))\b",
                # Name with Auto/Motors/Cars/Sales suffix followed by STOCK/CALL/phone/address
                r"^([A-Z][A-Za-z\s&'.\-]{2,40}(?:Auto|Motors?|Cars?|Sales|LLC|Inc|Dealer|NW))\s*(?:STOCK|CALL|[,(]|\d{3,})",
                r"^([A-Z][A-Za-z\s&'.]+?(?:Auto|Motors?|Cars?|Sales|LLC|Inc))\s",
            ]
            for pat in dealer_patterns:
                m = re.search(pat, body_text)
                if m:
                    name = _clean_name(m.group(1))
                    if 3 < len(name) < 50 and not _is_location_name(name) and not _is_vehicle_name(name):
                        result["seller_name"] = name
                        break

    # 3) Check title for dealer name in parentheses: "2013 Nissan...(DealerName)"
    title_el = soup.select_one(".postingtitletext")
    if title_el and not result["seller_name"]:
        title_text = title_el.get_text(strip=True)
        m = re.search(r"\(([^)]{3,50})\)\s*$", title_text)
        if m:
            candidate = _clean_name(m.group(1))
            # Filter out prices, locations, and vehicle names
            if (candidate
                    and not re.match(r"^\$|^\d+$", candidate)
                    and not _is_location_name(candidate)
                    and not _is_vehicle_name(candidate)):
                result["seller_name"] = candidate

    # Final cleanup
    result["seller_name"] = _clean_name(result["seller_name"])

    return result


def rate_seller(seller_name: str, seller_type: str) -> int:
    """Rate a seller 1-10 based on name and type."""
    name_lower = seller_name.lower().strip() if seller_name else ""

    # Check known ratings first (only if we have a name to check)
    if name_lower:
        for known, rating in KNOWN_RATINGS.items():
            if known.lower() in name_lower or name_lower in known.lower():
                return rating

    if seller_type == "private":
        return 3  # Private seller -- least accountability

    if seller_type == "dealer":
        if not name_lower:
            return 4  # Anonymous dealer post on CL

        # Named dealer with franchise keywords
        franchise_keywords = ["nissan", "toyota", "honda", "ford", "chevrolet",
                              "subaru", "hyundai", "kia", "mazda", "bmw", "audi"]
        for kw in franchise_keywords:
            if kw in name_lower:
                return 7

        big_dealer_keywords = ["auto group", "automotive", "motor company"]
        for kw in big_dealer_keywords:
            if kw in name_lower:
                return 6

        # Named small CL dealer -- some accountability
        return 5

    return 4  # Unknown


def main():
    rows = []
    with open(f"{OUT}/{prefix}_results_by_distance.csv", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)

    print(f"Processing {len(rows)} listings...\n")

    for i, row in enumerate(rows):
        url = row["url"]
        dealer = row["dealer"]

        if "craigslist.org" in url:
            print(f"  [{i+1}/{len(rows)}] Scraping: {url.split('/d/')[1][:50] if '/d/' in url else url[:60]}...")
            info = scrape_cl_listing(url)

            if info["seller_name"]:
                row["dealer"] = info["seller_name"]
            elif info["seller_type"] == "private":
                row["dealer"] = "Private Seller"
            elif info["seller_type"] == "dealer":
                row["dealer"] = "CL Dealer (unnamed)"
            else:
                row["dealer"] = "Craigslist (unknown)"

            row["seller_type"] = info["seller_type"]
            row["dealer_rating"] = rate_seller(info["seller_name"], info["seller_type"])

            print(f"           -> {row['dealer']} ({info['seller_type']}) -- rating: {row['dealer_rating']}/10")
            time.sleep(0.5)  # polite delay
        elif "autotrader.com" in url:
            # AutoTrader listings -- dealer name may be generic "AutoTrader"
            row["seller_type"] = "dealer"
            row["dealer_rating"] = rate_seller(dealer, "dealer")
            print(f"  [{i+1}/{len(rows)}] AutoTrader: {dealer} -- rating: {row['dealer_rating']}/10")
        elif "cars.com" in url:
            # Cars.com listings -- dealer name is the real dealer from the site
            row["seller_type"] = "dealer"
            row["dealer_rating"] = rate_seller(dealer, "dealer")
            print(f"  [{i+1}/{len(rows)}] Cars.com: {dealer} -- rating: {row['dealer_rating']}/10")
        elif "facebook.com" in url:
            # Facebook Marketplace -- detect dealer vs private from description
            desc = (row.get("description", "") + " " + (row.get("trim", ""))).lower()
            dealer_lower = dealer.lower()
            # Heuristic: if dealer name is still generic or description has dealer signals
            is_dealer = (
                dealer_lower not in ("facebook marketplace", "") and
                not _is_vehicle_name(dealer) and
                not _is_location_name(dealer)
            )
            if not is_dealer:
                # Check description for dealer-like patterns
                dealer_patterns = [
                    r"\b(?:auto|motor|car|truck|vehicle)s?\s*(?:sale|group|center|inc|llc|ltd)",
                    r"\b(?:inc|llc|ltd|corp|co)\b\.?",
                    r"\bdealership\b",
                    r"\bwe finance\b",
                    r"\b(?:call|visit)\s+us\b",
                ]
                for pat in dealer_patterns:
                    if re.search(pat, desc, re.IGNORECASE):
                        is_dealer = True
                        break
            seller_type = "dealer" if is_dealer else "private"
            if dealer == "Facebook Marketplace":
                row["dealer"] = "FB Dealer" if is_dealer else "FB Private Seller"
            row["seller_type"] = seller_type
            row["dealer_rating"] = rate_seller(row["dealer"], seller_type)
            print(f"  [{i+1}/{len(rows)}] Facebook: {row['dealer']} ({seller_type}) -- rating: {row['dealer_rating']}/10")
        else:
            # Keep existing dealer info
            row["seller_type"] = "dealer"
            row["dealer_rating"] = KNOWN_RATINGS.get(dealer, rate_seller(dealer, "dealer"))
            print(f"  [{i+1}/{len(rows)}] Known dealer: {dealer} -- rating: {row['dealer_rating']}/10")

    # Write enriched CSV
    fieldnames = [
        "dealer", "seller_type", "year", "make", "model", "trim", "price",
        "mileage", "distance_mi", "location", "dealer_rating", "url", "description",
        "vin", "vin_data", "image_url",
    ]
    with open(f"{OUT}/{prefix}_results_by_distance.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"\nDone -- wrote {len(rows)} rows to {OUT}/{prefix}_results_by_distance.csv")


if __name__ == "__main__":
    main()
