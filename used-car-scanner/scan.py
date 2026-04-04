"""
Used Car Scanner -- Scrapes local dealer websites near 98155 (Shoreline, WA)
one by one and consolidates used car inventory into a searchable list.

Usage:
    python scan.py                          # scan all dealers, show all results
    python scan.py --make Toyota            # filter by make
    python scan.py --max-price 25000        # filter by max price
    python scan.py --max-miles 60000        # filter by max mileage
    python scan.py --make Honda --max-price 20000 --csv results.csv
"""

import argparse
import csv
import json
import re
import sys
import time
from dataclasses import dataclass, fields, asdict
from pathlib import Path
from urllib.parse import urljoin

import requests
from bs4 import BeautifulSoup
from tabulate import tabulate

# Optional: Playwright for sites with bot protection
try:
    from playwright.sync_api import sync_playwright
    HAS_PLAYWRIGHT = True
except ImportError:
    HAS_PLAYWRIGHT = False

# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass
class Vehicle:
    dealer: str
    year: str
    make: str
    model: str
    trim: str
    price: str
    mileage: str
    url: str
    distance_mi: str = ""
    description: str = ""
    vin: str = ""
    vin_data: str = ""
    image_url: str = ""

    def price_num(self) -> float:
        """Return numeric price for filtering, or inf if unknown."""
        cleaned = re.sub(r"[^\d.]", "", self.price)
        try:
            return float(cleaned)
        except ValueError:
            return float("inf")

    def mileage_num(self) -> float:
        """Return numeric mileage for filtering, or inf if unknown."""
        cleaned = re.sub(r"[^\d]", "", self.mileage)
        try:
            return float(cleaned)
        except ValueError:
            return float("inf")


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

SESSION = requests.Session()
SESSION.headers.update({
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Safari/537.36"
    ),
    "Accept": "text/html,application/xhtml+xml,application/json,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9",
})


def fetch(url: str, timeout: int = 30) -> requests.Response | None:
    """GET a URL, returning the response or None on failure."""
    try:
        resp = SESSION.get(url, timeout=timeout, allow_redirects=True)
        resp.raise_for_status()
        return resp
    except requests.RequestException as exc:
        print(f"    [!]  Request failed: {exc}")
        return None


# Path for persistent browser profile (Facebook login state)
_BROWSER_PROFILE_DIR = Path(__file__).parent / ".browser-profile"


def fetch_with_browser(url: str, wait_ms: int = 5000) -> str | None:
    """Use Playwright headless Chrome for sites that block requests."""
    if not HAS_PLAYWRIGHT:
        print("    [!]  Playwright not installed -- run: pip install playwright && playwright install chromium")
        return None
    print("    [~]  Using headless browser...")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            ctx = browser.new_context(
                user_agent=SESSION.headers["User-Agent"],
                viewport={"width": 1920, "height": 1080},
            )
            page = ctx.new_page()
            page.goto(url, wait_until="domcontentloaded", timeout=45000)
            # Wait for content to render
            page.wait_for_timeout(wait_ms)
            # Scroll down to trigger lazy loading
            page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
            page.wait_for_timeout(2000)
            html = page.content()
            browser.close()
            return html
    except Exception as exc:
        print(f"    [!]  Browser fetch failed: {exc}")
        return None


def fetch_with_headed_browser(url: str, wait_ms: int = 8000, wait_until: str = "domcontentloaded") -> str | None:
    """Use Playwright in headed mode (off-screen) for sites that block headless."""
    if not HAS_PLAYWRIGHT:
        print("    [!]  Playwright not installed")
        return None
    print("    [~]  Using headed browser (off-screen)...")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(
                headless=False,
                args=[
                    "--window-position=-10000,-10000",
                    "--disable-blink-features=AutomationControlled",
                ],
            )
            ctx = browser.new_context(
                user_agent=SESSION.headers["User-Agent"],
                viewport={"width": 1920, "height": 1080},
                locale="en-US",
            )
            ctx.add_init_script("""
                delete Object.getPrototypeOf(navigator).webdriver;
                Object.defineProperty(navigator, 'webdriver', {get: () => undefined});
                Object.defineProperty(navigator, 'plugins', {get: () => [1, 2, 3, 4, 5]});
                Object.defineProperty(navigator, 'languages', {get: () => ['en-US', 'en']});
                window.chrome = {runtime: {}, loadTimes: function(){}, csi: function(){}};
            """)
            page = ctx.new_page()
            page.goto(url, wait_until=wait_until, timeout=60000)
            page.wait_for_timeout(wait_ms)
            for i in range(3):
                page.evaluate(f"window.scrollTo(0, {(i+1) * 800})")
                page.wait_for_timeout(1000)
            html = page.content()
            browser.close()
            return html
    except Exception as exc:
        print(f"    [!]  Headed browser fetch failed: {exc}")
        return None


def fetch_with_persistent_browser(url: str, wait_ms: int = 8000, scroll: bool = True) -> str | None:
    """Use Playwright with a persistent profile (keeps Facebook login state)."""
    if not HAS_PLAYWRIGHT:
        print("    [!]  Playwright not installed")
        return None
    profile = str(_BROWSER_PROFILE_DIR)
    if not _BROWSER_PROFILE_DIR.exists():
        print("    [!]  No browser profile found. Run: py -3 scan.py --fb-setup")
        return None
    print("    [~]  Using browser with saved profile...")
    try:
        with sync_playwright() as p:
            ctx = p.chromium.launch_persistent_context(
                profile,
                headless=True,
                viewport={"width": 1920, "height": 1080},
            )
            page = ctx.new_page()
            page.goto(url, wait_until="domcontentloaded", timeout=45000)
            page.wait_for_timeout(wait_ms)
            if scroll:
                import re as _re
                prev_count = 0
                stall_count = 0
                for i in range(30):
                    page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
                    page.wait_for_timeout(3000)
                    content = page.content()
                    # Count both <a> link items and JSON-embedded items
                    link_ids = set(_re.findall(r'/marketplace/item/(\d+)', content))
                    json_ids = set(_re.findall(r'GroupCommerceProductItem","id":"(\d+)"', content))
                    cur_count = len(link_ids | json_ids)
                    if cur_count == prev_count:
                        stall_count += 1
                        if stall_count >= 3 and i >= 5:
                            break  # no new items after 3 consecutive stalls
                    else:
                        stall_count = 0
                    prev_count = cur_count
            html = page.content()
            ctx.close()
            return html
    except Exception as exc:
        print(f"    [!]  Browser fetch failed: {exc}")
        return None


def fb_setup():
    """Open a visible browser with persistent profile for the user to log into Facebook."""
    if not HAS_PLAYWRIGHT:
        print("Playwright not installed -- run: pip install playwright && playwright install chromium")
        sys.exit(1)
    profile = str(_BROWSER_PROFILE_DIR)
    _BROWSER_PROFILE_DIR.mkdir(exist_ok=True)
    print("Opening browser -- please log into Facebook, then close the browser window.")
    with sync_playwright() as p:
        ctx = p.chromium.launch_persistent_context(
            profile,
            headless=False,
            viewport={"width": 1280, "height": 900},
        )
        page = ctx.new_page()
        page.goto("https://www.facebook.com/login", wait_until="domcontentloaded", timeout=60000)
        # Wait until the user closes the browser
        try:
            page.wait_for_event("close", timeout=300000)
        except Exception:
            pass
        ctx.close()
    print("Done -- Facebook login saved. You can now run the scanner.")


def cg_setup():
    """Open a visible browser to solve CarGurus CAPTCHA and save DataDome cookies."""
    if not HAS_PLAYWRIGHT:
        print("Playwright not installed -- run: pip install playwright && playwright install chromium")
        sys.exit(1)
    cg_profile = Path(__file__).parent / ".cg_profile"
    cg_profile.mkdir(exist_ok=True)
    print("Opening browser -- please solve the CAPTCHA on CarGurus, then close the browser window.")
    with sync_playwright() as pw:
        ctx = pw.chromium.launch_persistent_context(
            str(cg_profile),
            headless=False,
            args=["--disable-blink-features=AutomationControlled"],
            viewport={"width": 1366, "height": 768},
        )
        ctx.add_init_script("""
            delete Object.getPrototypeOf(navigator).webdriver;
            Object.defineProperty(navigator, 'webdriver', {get: () => undefined});
        """)
        page = ctx.new_page()
        page.goto(
            "https://www.cargurus.com/Cars/inventorylisting/viewDetailsFilterViewInventoryListing.action"
            "?sourceContext=carGurusHomePageModel&entitySelectingHelper.selectedEntity=d240&zip=98155",
            wait_until="domcontentloaded",
            timeout=60000,
        )
        try:
            page.wait_for_event("close", timeout=300000)
        except Exception:
            pass
        ctx.close()
    print("Done -- CarGurus cookies saved. You can now run the scanner.")


def soup_from(resp: requests.Response) -> BeautifulSoup:
    return BeautifulSoup(resp.text, "lxml")


# ---------------------------------------------------------------------------
# JSON-LD extractor (works on many dealer platforms)
# ---------------------------------------------------------------------------

def extract_jsonld_vehicles(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Extract vehicles from JSON-LD <script type='application/ld+json'> blocks."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    for script in soup.find_all("script", type="application/ld+json"):
        try:
            data = json.loads(script.string or "")
        except (json.JSONDecodeError, TypeError):
            continue

        items = data if isinstance(data, list) else [data]
        for item in items:
            if not isinstance(item, dict):
                continue
            # Check @type for Vehicle / Car / Product
            item_type = item.get("@type", "")
            if item_type not in ("Vehicle", "Car", "Product", "Automobile"):
                # Check inside @graph
                for g in item.get("@graph", []):
                    if isinstance(g, dict) and g.get("@type") in ("Vehicle", "Car", "Product", "Automobile"):
                        v = _vehicle_from_jsonld(g, dealer_name, base_url)
                        if v:
                            vehicles.append(v)
                continue
            v = _vehicle_from_jsonld(item, dealer_name, base_url)
            if v:
                vehicles.append(v)
    return vehicles


def _vehicle_from_jsonld(item: dict, dealer_name: str, base_url: str) -> Vehicle | None:
    name = item.get("name", "")
    # Try to parse "2022 Toyota Camry SE" style names
    year, make, model, trim = _parse_vehicle_name(name)

    # Fallback: use structured JSON-LD fields when name parsing is incomplete
    if not year:
        vmd = item.get("vehicleModelDate", "")
        if vmd:
            year = str(vmd)
    if not make:
        brand = item.get("brand", item.get("manufacturer", ""))
        if isinstance(brand, dict):
            make = brand.get("name", "")
        elif isinstance(brand, str):
            make = brand
    # Always prefer JSON-LD model field -- it's structured and more reliable
    json_model = item.get("model", "")
    if isinstance(json_model, dict):
        json_model = json_model.get("name", "")
    if json_model:
        model = json_model

    # Price
    price = ""
    offers = item.get("offers", item.get("offer", {}))
    if isinstance(offers, list):
        offers = offers[0] if offers else {}
    if isinstance(offers, dict):
        price = str(offers.get("price", offers.get("lowPrice", "")))
    if not price:
        price = str(item.get("price", ""))

    # Mileage
    mileage = ""
    mi = item.get("mileageFromOdometer", item.get("mileage", {}))
    if isinstance(mi, dict):
        mileage = str(mi.get("value", ""))
    elif mi:
        mileage = str(mi)

    # URL
    url = item.get("url", "")
    if url and not url.startswith("http"):
        url = urljoin(base_url, url)

    if not (year or make or model):
        return None
    # Build rich description from all available JSON-LD fields
    parts = [item.get("description", "") or name]
    for key, label in [
        ("bodyType", "Body"), ("driveWheelConfiguration", "Drive"),
        ("vehicleEngine", "Engine"), ("vehicleTransmission", "Trans"),
        ("numberOfDoors", "Doors"), ("fuelType", "Fuel"),
        ("color", "Ext"), ("vehicleInteriorColor", "Int"),
        ("vehicleConfiguration", "Config"), ("itemCondition", "Condition"),
    ]:
        val = item.get(key, "")
        if isinstance(val, dict):
            val = val.get("value", val.get("name", ""))
        if val:
            parts.append(f"{label}: {val}")
    desc = " | ".join(parts)
    # Extract image URL from JSON-LD
    img = item.get("image", "")
    if isinstance(img, list):
        img = img[0] if img else ""
    if isinstance(img, dict):
        img = img.get("url", img.get("contentUrl", ""))
    return Vehicle(
        dealer=dealer_name, year=year, make=make, model=model,
        trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
        description=desc, image_url=str(img) if img else "",
    )


# ---------------------------------------------------------------------------
# Detail-page enrichment -- visit each vehicle URL for full specs
# ---------------------------------------------------------------------------

# Keys in visible HTML that should override JSON-LD "Drive" value
_DRIVE_LABELS = frozenset({"drivetrain", "drive type", "drive train", "drivetype",
                           "drive wheel", "drivewheel"})
_BODY_LABELS = frozenset({"body style", "bodystyle", "body type", "bodytype",
                           "cab style", "cab size", "cab type"})

def _extract_specs_from_html(html: str) -> dict[str, str]:
    """Extract key vehicle specs from a detail page's HTML."""
    specs: dict[str, str] = {}
    soup = BeautifulSoup(html, "lxml")

    # 1. JSON-LD structured data
    for script in soup.find_all("script", type="application/ld+json"):
        try:
            data = json.loads(script.string or "")
        except (json.JSONDecodeError, TypeError):
            continue
        items = data if isinstance(data, list) else [data]
        for item in items:
            if not isinstance(item, dict):
                continue
            if item.get("@type") not in ("Vehicle", "Car", "Product", "Automobile"):
                for g in item.get("@graph", []):
                    if isinstance(g, dict) and g.get("@type") in ("Vehicle", "Car", "Product", "Automobile"):
                        item = g
                        break
                else:
                    continue
            for key, label in [
                ("driveWheelConfiguration", "Drive"),
                ("bodyType", "Body"), ("numberOfDoors", "Doors"),
                ("vehicleEngine", "Engine"), ("vehicleTransmission", "Trans"),
                ("fuelType", "Fuel"), ("vehicleConfiguration", "Config"),
                ("vehicleIdentificationNumber", "VIN"),
            ]:
                val = item.get(key, "")
                if isinstance(val, dict):
                    val = val.get("value", val.get("name", ""))
                if val and label not in specs:
                    val = str(val)
                    # Map schema.org URLs to readable values
                    if label == "Drive" and "schema.org" in val:
                        for suffix, readable in {
                            "RearWheelDrive": "Rear-Wheel Drive",
                            "FrontWheelDrive": "Front-Wheel Drive",
                            "FourWheelDrive": "Four-Wheel Drive",
                            "AllWheelDrive": "All-Wheel Drive",
                        }.items():
                            if suffix in val:
                                val = readable
                                break
                    specs[label] = val

            # Extract price from JSON-LD offers
            if "Price" not in specs:
                offers = item.get("offers", {})
                if isinstance(offers, list):
                    offers = offers[0] if offers else {}
                if isinstance(offers, dict):
                    price_val = offers.get("price", "")
                    if price_val:
                        cleaned = re.sub(r"[^\d.]", "", str(price_val))
                        try:
                            if float(cleaned) > 0:
                                specs["Price"] = cleaned
                        except ValueError:
                            pass

    # Helper: store spec, overriding JSON-LD "Drive"/"Body" when HTML has a more specific key
    def _set_spec(label: str, value: str) -> None:
        key = label.lower().strip()
        if key in _DRIVE_LABELS:
            specs["Drive"] = value
        elif key in _BODY_LABELS:
            specs["Body"] = value
        else:
            specs.setdefault(label, value)

    # 2. Common spec table/list patterns (dt/dd, th/td, label/value)
    for dt in soup.select("dt"):
        dd = dt.find_next_sibling("dd")
        if dd:
            label = dt.get_text(strip=True).rstrip(":").strip()
            value = dd.get_text(strip=True)
            if label and value:
                _set_spec(label, value)
    for tr in soup.select("tr"):
        cells = tr.find_all(["th", "td"])
        if len(cells) == 2:
            label = cells[0].get_text(strip=True).rstrip(":").strip()
            value = cells[1].get_text(strip=True)
            if label and value:
                _set_spec(label, value)
    # li with "Label: Value" pattern
    for li in soup.select("ul.vehicle-features li, ul.specs li, .vehicle-info li, .vdp-details li"):
        text = li.get_text(strip=True)
        if ":" in text:
            label, _, value = text.partition(":")
            label = label.strip()
            value = value.strip()
            if label and value:
                _set_spec(label, value)

    # 3. Cars.com / generic: div-based label+value pairs
    for item in soup.select(".fancy-description-list dt, [class*='description-list'] dt, .sds-definition-list dt"):
        dd = item.find_next_sibling("dd")
        if dd:
            label = item.get_text(strip=True).rstrip(":").strip()
            value = dd.get_text(strip=True)
            if label and value:
                _set_spec(label, value)

    # 3b. Cars.com basics entries: <li data-qa="basics-entry">
    for li in soup.select('[data-qa="basics-entry"]'):
        text = li.get_text(" ", strip=True)
        # Pattern: "Rear-wheel Drive drivetrain", "Gasoline fuel", "6 Cylinders", "Automatic transmission"
        dm = re.match(r"((?:Rear|Front|All|Four)[\w -]*(?:Wheel[\s-]*)?Drive)\s+drivetrain", text, re.IGNORECASE)
        if dm:
            specs.setdefault("Drive", dm.group(1).strip())
            continue
        tm = re.match(r"(Automatic|Manual|CVT|[\w -]+)\s+transmission", text, re.IGNORECASE)
        if tm:
            specs.setdefault("Trans", tm.group(1).strip())
            continue
        fm = re.match(r"(Gasoline|Diesel|Electric|Hybrid|Flex\s*Fuel|E85|Plug-In[\w -]*)\s+fuel", text, re.IGNORECASE)
        if fm:
            specs.setdefault("Fuel", fm.group(1).strip())
            continue
        em = re.match(r"((?:\d[\d.]*\s*L?\s+)?(?:Inline-?\d+|V\d+|I\d+|Flat-?\d+|\d+\s*[Cc]ylinder)[\w -]*)", text, re.IGNORECASE)
        if em:
            specs.setdefault("Engine", em.group(1).strip())
            continue
        bm = re.match(r"(King\s*Cab|Crew\s*Cab|Extended\s*Cab|Regular\s*Cab|Access\s*Cab|Double\s*Cab|"
                       r"SuperCab|SuperCrew|Quad\s*Cab|Mega\s*Cab|Sedan|Coupe|SUV|Hatchback|Wagon|"
                       r"Convertible|Van|Minivan|Pickup|Truck)", text, re.IGNORECASE)
        if bm:
            specs.setdefault("Body", bm.group(1).strip())
            continue

    # 4. Text scan fallback -- look for drivetrain keywords in page text
    page_text = soup.get_text(" ", strip=True)
    if "Drive" not in specs:
        drive_match = re.search(
            r"(?:Drivetrain|Drive\s*Type|Drive\s*Train)\s*[:.]?\s*"
            r"((?:Rear|Front|All|Four)[\s-]*Wheel[\s-]*Drive|[24]WD|[24]x[24]|AWD|RWD|FWD|4MATIC)",
            page_text, re.IGNORECASE,
        )
        if not drive_match:
            # Reverse pattern: "Rear-wheel Drive drivetrain"
            drive_match = re.search(
                r"((?:Rear|Front|All|Four)[\s-]*Wheel[\s-]*Drive|[24]WD|AWD|RWD|FWD|4MATIC)"
                r"\s+(?:drivetrain|drive\s*type|drive\s*train)",
                page_text, re.IGNORECASE,
            )
        if not drive_match:
            # Abbreviation pattern: "RWD: Rear Wheel Drive" or standalone "RWD"
            drive_match = re.search(
                r"\b(RWD|FWD|AWD|[24]WD|4x4|4x2)\b\s*(?::\s*"
                r"(?:Rear|Front|All|Four)[\s-]*Wheel[\s-]*Drive)?",
                page_text,
            )
        if drive_match:
            specs["Drive"] = drive_match.group(1).strip()

    # 5. Text scan -- look for body/cab keywords (overrides JSON-LD which can be wrong)
    body_match = re.search(
        r"(?:Body\s*(?:Style|Type)|Cab\s*(?:Style|Size))\s*[:.]?\s*"
        r"(King\s*Cab|Crew\s*Cab|Extended\s*Cab|Regular\s*Cab|Access\s*Cab|Double\s*Cab|"
        r"SuperCab|SuperCrew|Quad\s*Cab|Mega\s*Cab)",
        page_text, re.IGNORECASE,
    )
    if body_match:
        specs["Body"] = body_match.group(1).strip()

    # 6. Title status -- salvage, rebuilt, branded, etc.
    if "Title" not in specs:
        title_match = re.search(
            r"(salvage|rebuilt|branded|junk|flood|lemon)\s+title",
            page_text, re.IGNORECASE,
        )
        if not title_match:
            # "has a branded title" or "title: salvage"
            title_match = re.search(
                r"title\s*[:.]?\s*(salvage|rebuilt|branded|junk|flood|lemon)",
                page_text, re.IGNORECASE,
            )
        if title_match:
            specs["Title"] = title_match.group(1).strip().title()

    # 7. VIN text scan fallback
    if "VIN" not in specs:
        vin_match = re.search(
            r"(?:VIN|Vehicle\s*Identification)\s*(?:#|Number)?\s*[:.]?\s*"
            r"([A-HJ-NPR-Z0-9]{17})\b",
            page_text, re.IGNORECASE,
        )
        if not vin_match:
            # Standalone 17-char VIN pattern (common in structured data)
            vin_match = re.search(r"\b([A-HJ-NPR-Z0-9]{17})\b", page_text)
        if vin_match:
            specs["VIN"] = vin_match.group(1)

    # 8. Price from HTML text (selling price, internet price, sale price)
    if "Price" not in specs:
        price_match = re.search(
            r"(?:Selling\s*Price|Internet\s*Price|Sale\s*Price|Our\s*Price|Price)\s*[:.]?\s*\$?([\d,]+)",
            page_text, re.IGNORECASE,
        )
        if price_match:
            cleaned = price_match.group(1).replace(",", "")
            try:
                if float(cleaned) > 0:
                    specs["Price"] = cleaned
            except ValueError:
                pass

    return specs


def _specs_to_description(specs: dict[str, str]) -> str:
    """Build a description string from extracted specs, focusing on filter-relevant fields."""
    # Normalize spec keys to find drivetrain / body / doors info
    KEY_MAP = {
        "drive": "Drive", "drivetrain": "Drive", "drive type": "Drive",
        "drivetype": "Drive", "drive train": "Drive",
        "drivewheel": "Drive", "drive wheel": "Drive",
        "body": "Body", "body style": "Body", "bodystyle": "Body",
        "body type": "Body", "cab": "Body", "cab style": "Body",
        "doors": "Doors", "number of doors": "Doors",
        "engine": "Engine", "transmission": "Trans", "trans": "Trans",
        "fuel": "Fuel", "fuel type": "Fuel",
        "title": "Title", "title status": "Title", "title type": "Title",
    }
    normalized: dict[str, str] = {}
    for key, value in specs.items():
        mapped = KEY_MAP.get(key.lower().strip())
        if mapped and mapped not in normalized:
            normalized[mapped] = value

    parts = []
    for label in ["Drive", "Body", "Doors", "Engine", "Trans", "Fuel", "Title"]:
        if label in normalized:
            parts.append(f"{label}: {normalized[label]}")
    return " | ".join(parts)


def enrich_vehicle_details(vehicles: list[Vehicle], platform: str, needs_browser: bool = False) -> list[Vehicle]:
    """Visit each vehicle's detail page to fetch full specs (drivetrain, body, etc.)."""
    if not vehicles:
        return vehicles

    # AutoTrader detail pages block all access (bot detection) -- skip enrichment
    # Facebook detail pages require login -- the listing page already provides key data
    if platform in ("autotrader", "facebook"):
        return vehicles

    # Only enrich vehicles that have a URL and sparse descriptions
    to_enrich = [v for v in vehicles if v.url and v.url.startswith("http")]
    if not to_enrich:
        return vehicles

    print(f"    [i] Enriching {len(to_enrich)} vehicle detail pages...")

    # Determine fetch method based on platform
    use_headed = platform in ("autotrader", "carscom", "cargurus", "motive") or needs_browser
    use_requests = not needs_browser and platform not in ("autotrader", "carscom", "cargurus", "motive", "facebook")

    # Try requests first for non-bot-protected sites
    if use_requests:
        enriched = 0
        for v in to_enrich:
            resp = fetch(v.url)
            if resp:
                specs = _extract_specs_from_html(resp.text)
                if specs:
                    extra = _specs_to_description(specs)
                    if extra:
                        v.description = f"{v.description} | {extra}" if v.description else extra
                        enriched += 1
                    if not v.vin and specs.get("VIN"):
                        v.vin = specs["VIN"]
                    if specs.get("Price"):
                        page_price = _fmt_price(specs["Price"])
                        if page_price != v.price and page_price != "--":
                            v.price = page_price
            time.sleep(0.5)
        if enriched > 0:
            print(f"    [i] Enriched {enriched}/{len(to_enrich)} via HTTP")
            return vehicles
        else:
            print(f"    [!]  HTTP enrichment found no specs for {len(to_enrich)} vehicles")

    # Use browser for bot-protected sites or if requests failed
    if HAS_PLAYWRIGHT:
        enriched = 0
        try:
            with sync_playwright() as p:
                if use_headed:
                    browser = p.chromium.launch(
                        headless=False,
                        args=[
                            "--window-position=-10000,-10000",
                            "--disable-blink-features=AutomationControlled",
                        ],
                    )
                else:
                    browser = p.chromium.launch(headless=True)
                ctx = browser.new_context(
                    user_agent=SESSION.headers["User-Agent"],
                    viewport={"width": 1920, "height": 1080},
                    locale="en-US",
                )
                if use_headed:
                    ctx.add_init_script("""
                        delete Object.getPrototypeOf(navigator).webdriver;
                        Object.defineProperty(navigator, 'webdriver', {get: () => undefined});
                    """)
                page = ctx.new_page()
                for v in to_enrich:
                    try:
                        page.goto(v.url, wait_until="domcontentloaded", timeout=30000)
                        wait_ms = 5000 if platform in ("carscom", "autotrader", "cargurus") else 3000
                        page.wait_for_timeout(wait_ms)
                        html = page.content()
                        specs = _extract_specs_from_html(html)
                        if specs:
                            extra = _specs_to_description(specs)
                            if extra:
                                v.description = f"{v.description} | {extra}" if v.description else extra
                                enriched += 1
                            if not v.vin and specs.get("VIN"):
                                v.vin = specs["VIN"]
                            if specs.get("Price"):
                                page_price = _fmt_price(specs["Price"])
                                if page_price != v.price and page_price != "--":
                                    v.price = page_price
                    except Exception as exc:
                        print(f"    [!]  Enrich error for {v.url[:60]}...: {exc}")
                    time.sleep(0.5)
                browser.close()
            if enriched > 0:
                print(f"    [i] Enriched {enriched}/{len(to_enrich)} via browser")
            else:
                print(f"    [!]  Browser enrichment found no specs for {len(to_enrich)} vehicles")
        except Exception as exc:
            print(f"    [!]  Detail enrichment failed: {exc}")

    return vehicles


# ---------------------------------------------------------------------------
# Platform-specific scrapers
# ---------------------------------------------------------------------------

def scrape_dealer_eprocess(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Dealer eProcess platform (Carter Subaru, Rodland Toyota, etc.)."""
    vehicles = extract_jsonld_vehicles(html, dealer_name, base_url)
    if vehicles:
        return vehicles

    # Fallback: parse HTML vehicle cards
    soup = BeautifulSoup(html, "lxml")
    for card in soup.select(".vehicle-card, .srpVehicle, [data-vdp-url], .vehicle-card-details"):
        v = _parse_html_card(card, dealer_name, base_url)
        if v:
            vehicles.append(v)
    return vehicles


def scrape_dealeron(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """DealerOn platform (Toyota of Lake City, etc.)."""
    vehicles = extract_jsonld_vehicles(html, dealer_name, base_url)
    if vehicles:
        return vehicles

    soup = BeautifulSoup(html, "lxml")
    # DealerOn wraps vehicles in .hproduct or data attribute cards
    for card in soup.select(".hproduct, .vehicle-card, .srpVehicle, [data-vdpurl], .item"):
        v = _parse_html_card(card, dealer_name, base_url)
        if v:
            vehicles.append(v)

    # Fallback: look for any links matching used vehicle detail patterns
    if not vehicles:
        for link in soup.select('a[href*="/used-"], a[href*="/preowned"], a[href*="UsedVehicleDetails"]'):
            name = link.get_text(strip=True)
            if name and re.search(r"\d{4}", name):
                year, make, model, trim = _parse_vehicle_name(name)
                if year:
                    href = link.get("href", "")
                    url = href if href.startswith("http") else urljoin(base_url, href)
                    vehicles.append(Vehicle(
                        dealer=dealer_name, year=year, make=make, model=model,
                        trim=trim, price="--", mileage="--", url=url,
                        description=name,
                    ))
    return vehicles


def scrape_team_velocity(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Team Velocity / Apollo platform (Honda of Seattle, etc.)."""
    vehicles = extract_jsonld_vehicles(html, dealer_name, base_url)
    if vehicles:
        return vehicles

    soup = BeautifulSoup(html, "lxml")
    for card in soup.select(".vehicle-card, .inventory-vehicle, [data-vehicle-id]"):
        v = _parse_html_card(card, dealer_name, base_url)
        if v:
            vehicles.append(v)
    return vehicles


def scrape_dealer_com(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Dealer.com / Trader Interactive platform (Lee Johnson Chevrolet, etc.)."""
    vehicles = extract_jsonld_vehicles(html, dealer_name, base_url)
    if vehicles:
        return vehicles

    soup = BeautifulSoup(html, "lxml")
    for card in soup.select(".vehicle-card, .inventory-listing, [data-vin], .vehicle-card-details"):
        v = _parse_html_card(card, dealer_name, base_url)
        if v:
            vehicles.append(v)

    # Fallback: find vehicle title links
    if not vehicles:
        for link in soup.select('a[href*="VehicleDetails"], a[href*="/used-"], a[href*="/used/"], a[href*="inventory/"]'):
            name = link.get_text(strip=True)
            if name and re.search(r"\d{4}", name):
                year, make, model, trim = _parse_vehicle_name(name)
                if year:
                    href = link.get("href", "")
                    url = href if href.startswith("http") else urljoin(base_url, href)
                    vehicles.append(Vehicle(
                        dealer=dealer_name, year=year, make=make, model=model,
                        trim=trim, price="--", mileage="--", url=url,
                        description=name,
                    ))
    return vehicles


def scrape_dealer_inspire(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Dealer Inspire platform (Rairdon, Advantage Nissan, etc.)."""
    vehicles = extract_jsonld_vehicles(html, dealer_name, base_url)
    if vehicles:
        return vehicles

    soup = BeautifulSoup(html, "lxml")

    # Method 1: Parse data-vehicle JSON attributes (Rairdon uses these)
    seen_vins = set()
    for el in soup.select("[data-vehicle]"):
        raw = el.get("data-vehicle", "")
        try:
            data = json.loads(raw)
        except (json.JSONDecodeError, TypeError):
            continue
        if not isinstance(data, dict) or "year" not in data:
            continue
        vin = data.get("vin", "")
        if vin in seen_vins:
            continue
        seen_vins.add(vin)
        year = str(data.get("year", ""))
        make = data.get("make", "")
        model = data.get("model", "")
        trim = data.get("trim", "")
        price = _fmt_price(str(data.get("price", "")))
        mileage = _fmt_mileage(str(data.get("mileage", data.get("odometer", ""))))
        stock = data.get("stock", "")
        # Build URL from VIN/stock if available
        url_el = el.find_parent("a", href=True) or el.find("a", href=True)
        url = ""
        if url_el:
            href = url_el.get("href", "")
            url = href if href.startswith("http") else urljoin(base_url, href)
        desc = data.get("description", "") or f"{year} {make} {model} {trim}".strip()
        # Enrich with any extra fields from data-vehicle JSON
        extras = []
        for key, label in [
            ("body", "Body"), ("drivetrain", "Drive"), ("engine", "Engine"),
            ("transmission", "Trans"), ("doors", "Doors"), ("fuelType", "Fuel"),
            ("exteriorColor", "Ext"), ("interiorColor", "Int"),
            ("cab", "Cab"), ("bodyStyle", "Body"),
        ]:
            val = data.get(key, "")
            if val:
                extras.append(f"{label}: {val}")
        if extras:
            desc = desc + " | " + " | ".join(extras)
        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=price, mileage=mileage, url=url,
            description=desc,
        ))

    # Method 2: Also parse links like "Pre-Owned YEAR MAKE MODEL TRIM"
    # with "Selling Price: $XX,XXX" and "Mileage: XX,XXX" nearby
    # This catches vehicles not in data-vehicle attributes
    seen_urls = set(v.url for v in vehicles)
    for link in soup.select('a[href*="/inventory/"]'):
        name = link.get_text(strip=True)
        if not name or not re.search(r"\d{4}", name):
            continue
        href = link.get("href", "")
        url = href if href.startswith("http") else urljoin(base_url, href)
        if url in seen_urls:
            continue
        seen_urls.add(url)

        year, make, model, trim = _parse_vehicle_name(name)
        if not year:
            continue

        # Find price and mileage from surrounding context
        parent = link.find_parent()
        # Walk up to find a container with price/mileage
        for _ in range(8):
            if parent is None or parent.name == "body":
                break
            text = parent.get_text(" ", strip=True)
            if "Selling Price" in text or "Mileage" in text:
                break
            parent = parent.parent

        price = "--"
        mileage = "--"
        if parent:
            text = parent.get_text(" ", strip=True)
            pm = re.search(r"Selling Price[:\s]*\$([\d,]+)", text)
            if pm:
                price = f"${pm.group(1)}"
            mm = re.search(r"Mileage[:\s]*([\d,]+)", text)
            if mm:
                mileage = f"{mm.group(1)} mi"

        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=price, mileage=mileage, url=url,
            description=name,
        ))

    return vehicles


def scrape_automanager(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """AutoManager platform (Seattle Lux Auto, etc.)."""
    vehicles = extract_jsonld_vehicles(html, dealer_name, base_url)
    if vehicles:
        return vehicles

    soup = BeautifulSoup(html, "lxml")
    seen_urls = set()

    # AutoManager uses div.vehicle containers with vehicle-details links
    for link in soup.select('a[href*="vehicle-details/"]'):
        name = link.get_text(strip=True)
        if not name or not re.search(r"\d{4}", name):
            continue
        href = link.get("href", "")
        url = href if href.startswith("http") else urljoin(base_url, href)
        if url in seen_urls:
            continue
        seen_urls.add(url)

        year, make, model, trim = _parse_vehicle_name(name)
        if not year:
            continue

        # Walk up to find the vehicle panel container
        price = "--"
        mileage = "--"
        parent = link
        for _ in range(12):
            parent = parent.parent
            if parent is None or parent.name == "body":
                break
            # Look for price element
            price_el = parent.select_one(".pricevalue1, .inventory-price-container, .price")
            if price_el:
                pm = re.search(r"\$([\d,]+)", price_el.get_text())
                if pm:
                    price = f"${pm.group(1)}"
            # Look for mileage element
            mi_el = parent.select_one(".mileage")
            if mi_el:
                mileage = mi_el.get_text(strip=True)
            if price != "--" and mileage != "--":
                break

        am_card_text = parent.get_text(" ", strip=True) if parent and parent.name != "body" else name
        desc = am_card_text[:500] if len(am_card_text) > 500 else am_card_text
        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=price, mileage=_fmt_mileage(mileage), url=url,
            description=desc,
        ))

    return vehicles


def scrape_foxdealer(dealer: dict, search_make: str = "", search_model: str = "") -> list[Vehicle]:
    """FoxDealer platform -- uses Elasticsearch API at search.api.foxdealer.com."""
    dealer_name = dealer["name"]
    base_url = dealer["base_url"]
    foxdealer_id = dealer.get("foxdealer_id")
    if not foxdealer_id:
        print(f"    [!]  No foxdealer_id configured for {dealer_name}")
        return []

    api_url = f"https://search.api.foxdealer.com/?id={foxdealer_id}&filter_path=hits.hits._source.fd"
    headers = {
        "Content-Type": "application/json",
        "ciq-client-app": "CIQ-Platform-FE/1.0",
        "Origin": base_url.rstrip("/"),
        "Referer": base_url.rstrip("/") + "/",
    }

    # Build Elasticsearch query
    clauses = [f"fd.condition:Used", f"fd.blog_id:{foxdealer_id}"]
    if search_make:
        clauses.append(f"fd.make:{search_make}")
    if search_model:
        clauses.append(f"fd.model:{search_model}")
    query_str = " AND ".join(clauses)

    body = {
        "query": {"query_string": {"query": query_str, "default_field": "post_name"}},
        "size": 200,
    }

    try:
        resp = SESSION.post(api_url, json=body, headers=headers, timeout=30)
        resp.raise_for_status()
        data = resp.json()
    except Exception as exc:
        print(f"    [!]  FoxDealer API error: {exc}")
        return []

    hits = data.get("hits", {}).get("hits", [])
    vehicles: list[Vehicle] = []
    for hit in hits:
        fd = hit.get("_source", {}).get("fd", {})
        year = str(fd.get("year", ""))
        make = fd.get("make", "")
        model = fd.get("model", "")
        trim = fd.get("trim", "")
        # Use the lowest non-zero price across all price fields — the API
        # sometimes returns MSRP in display_price instead of the selling price.
        # Also check price group ranges (e.g. "15000,20000") for the lower bound.
        price_candidates = []
        for pk in ("display_price", "sellingprice", "conditional_price",
                    "internetprice", "specialprice"):
            try:
                pv = float(fd.get(pk, 0) or 0)
                if pv > 0:
                    price_candidates.append(pv)
            except (ValueError, TypeError):
                pass
        for gk in ("sellingprice_group", "display_price_group"):
            gv = fd.get(gk, "")
            if isinstance(gv, str) and "," in gv:
                try:
                    lo = float(gv.split(",")[0])
                    if lo > 0:
                        price_candidates.append(lo)
                except (ValueError, TypeError):
                    pass
        price = _fmt_price(str(int(min(price_candidates)))) if price_candidates else "--"
        mileage = _fmt_mileage(str(fd.get("miles", "")))
        vin = fd.get("vin", "")
        permalink = fd.get("permalink", "")
        url = base_url.rstrip("/") + permalink if permalink else base_url

        # Image: first item in imagelist
        image_url = ""
        imagelist = fd.get("imagelist", [])
        if imagelist and isinstance(imagelist[0], dict):
            image_url = imagelist[0].get("url", "")

        # Build description with extras
        desc = f"{year} {make} {model} {trim}".strip()
        extras = []
        for key, label in [
            ("body", "Body"), ("drivetrain", "Drive"), ("engdescription", "Engine"),
            ("trans", "Trans"), ("fueltype", "Fuel"), ("extcolor", "Ext"),
            ("intcolor", "Int"),
        ]:
            val = fd.get(key, "")
            if val:
                extras.append(f"{label}: {val}")
        if extras:
            desc = desc + " | " + " | ".join(extras)

        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=price, mileage=mileage, url=url,
            description=desc, vin=vin, image_url=image_url,
        ))

    return vehicles


def scrape_carmax(dealer_name: str, base_url: str) -> list[Vehicle]:
    """CarMax uses a JSON API."""
    api_url = (
        "https://www.carmax.com/cars/api/search/run"
        "?uri=%2Fcars&radius=25&latitude=47.7615&longitude=-122.3435"
        "&pageSize=50&pageNumber=1&sortKey=BestMatch"
    )
    resp = fetch(api_url)
    if not resp:
        return []
    try:
        data = resp.json()
    except (json.JSONDecodeError, ValueError):
        return []

    vehicles: list[Vehicle] = []
    items = data.get("items", data.get("Results", []))
    for item in items:
        year = str(item.get("year", item.get("Year", "")))
        make = item.get("make", item.get("Make", ""))
        model = item.get("model", item.get("Model", ""))
        trim = item.get("trim", item.get("Trim", ""))
        price = str(item.get("price", item.get("Price", item.get("basePrice", ""))))
        mileage = str(item.get("mileage", item.get("Miles", item.get("Mileage", ""))))
        stock = item.get("stockNumber", item.get("StockNumber", ""))
        url = f"https://www.carmax.com/car/{stock}" if stock else base_url
        desc = item.get("description", "") or f"{year} {make} {model} {trim}".strip()
        extras = []
        for key, label in [
            ("bodyStyle", "Body"), ("driveTrain", "Drive"), ("drivetrain", "Drive"),
            ("engine", "Engine"), ("engineSize", "Engine"), ("transmission", "Trans"),
            ("doors", "Doors"), ("fuelType", "Fuel"), ("mpgCity", "MPG City"),
            ("mpgHighway", "MPG Hwy"), ("exteriorColor", "Ext"), ("interiorColor", "Int"),
        ]:
            val = item.get(key, "")
            if val and str(val) not in desc:
                extras.append(f"{label}: {val}")
        if extras:
            desc = desc + " | " + " | ".join(extras)
        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
            description=desc,
        ))
    return vehicles


# ---------------------------------------------------------------------------
# Generic HTML card parser
# ---------------------------------------------------------------------------

def _parse_html_card(card, dealer_name: str, base_url: str) -> Vehicle | None:
    """Best-effort extraction from a generic vehicle card element."""
    # Title/name
    title_el = card.select_one(
        ".vehicle-title, .vehicleTitle, .title, h2, h3, "
        "[data-vehicle-title], .vehicle-name, .listing-title"
    )
    name = title_el.get_text(" ", strip=True) if title_el else card.get("data-title", "")
    if not name:
        name = card.get("data-vehicle-title", "")
    year, make, model, trim = _parse_vehicle_name(name)

    # Price -- try several selectors, fall back to last $XX,XXX in card text
    price_el = card.select_one(
        ".price, .vehicle-price, .internetPrice, .final-price, "
        "[data-price], .sale-price, .asking-price"
    )
    price = price_el.get_text(strip=True) if price_el else card.get("data-price", "")
    # If price text doesn't contain a digit (e.g. "Sale Price**"), extract from card text
    if not re.search(r"\d", price):
        card_text = card.get_text(" ", strip=True)
        # Find all $XX,XXX amounts; take the last one (usually the sale/final price)
        all_prices = re.findall(r"\$[\d,]+", card_text)
        if all_prices:
            price = all_prices[-1]

    # Mileage -- try selectors, fall back to "XX,XXX miles" in card text
    mi_el = card.select_one(
        ".mileage, .odometer, .miles, [data-mileage], .vehicle-mileage"
    )
    mileage = mi_el.get_text(strip=True) if mi_el else card.get("data-mileage", "")
    if not re.search(r"\d", mileage):
        card_text = card.get_text(" ", strip=True)
        mi_match = re.search(r"([\d,]+)\s*miles?", card_text, re.IGNORECASE)
        if mi_match:
            mileage = mi_match.group(1) + " mi"

    # URL
    link = card.select_one("a[href]")
    url = ""
    if link:
        href = link.get("href", "")
        url = href if href.startswith("http") else urljoin(base_url, href)
    if not url:
        vdp = card.get("data-vdp-url") or card.get("data-vdpurl", "")
        if vdp:
            url = vdp if vdp.startswith("http") else urljoin(base_url, vdp)

    if not (year or make or model):
        return None
    # Capture full card text as description for better LLM filtering
    card_text = card.get_text(" ", strip=True)
    desc = card_text[:500] if len(card_text) > 500 else card_text
    return Vehicle(
        dealer=dealer_name, year=year, make=make, model=model,
        trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
        description=desc,
    )


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

# Known car makes for smarter parsing
_KNOWN_MAKES = {
    "acura", "alfa", "aston", "audi", "bentley", "bmw", "buick", "cadillac",
    "chevrolet", "chevy", "chrysler", "dodge", "ferrari", "fiat", "ford", "genesis",
    "gmc", "honda", "hyundai", "infiniti", "jaguar", "jeep", "kia", "lamborghini",
    "land", "lexus", "lincoln", "maserati", "mazda", "mclaren", "mercedes", "mercedes-benz",
    "mini", "mitsubishi", "nissan", "porsche", "ram", "rivian", "rolls-royce", "subaru",
    "tesla", "toyota", "volkswagen", "volvo",
}

# Regex that handles both spaced and concatenated year+make (e.g. "2025Toyota")
_YMM_RE = re.compile(
    r"(?:(?:Used|Pre-?Owned|Certified|Gold\s+Certified)\s*)"
    r"?(\d{4})\s*"           # year (space optional after)
    r"([A-Za-z][A-Za-z -]+?)"  # make
    r"\s+(.+)",              # model + trim
    re.IGNORECASE,
)


def _parse_vehicle_name(name: str) -> tuple[str, str, str, str]:
    """Parse '2022 Toyota Camry SE' or '2022Toyota Camry SE' into (year, make, model, trim)."""
    name = name.strip()
    # Normalize: insert space between year and make if missing (e.g. "2025Toyota" -> "2025 Toyota")
    name = re.sub(r"(\d{4})([A-Za-z])", r"\1 \2", name)
    # Insert space before uppercase letters that follow lowercase (e.g. "CamryLE" -> "Camry LE")
    name = re.sub(r"([a-z])([A-Z])", r"\1 \2", name)
    # Clean up multiple spaces
    name = re.sub(r"\s+", " ", name).strip()
    # Remove leading "Used", "Pre-Owned", "Certified Pre-Owned", "Gold Certified"
    name = re.sub(r"^(?:Certified\s+Pre-?Owned|Gold\s+Certified|Pre-?Owned|Used)\s*", "", name, flags=re.IGNORECASE).strip()

    m = _YMM_RE.match(name)
    if not m:
        # Try simpler: just year + everything else
        m2 = re.match(r"(\d{4})\s+(.+)", name)
        if m2:
            year = m2.group(1)
            rest = m2.group(2).strip()
            # Try to split make from rest using known makes
            for known in _KNOWN_MAKES:
                if rest.lower().startswith(known):
                    make = rest[:len(known)]
                    remainder = rest[len(known):].strip()
                    parts = remainder.split(None, 1)
                    model = parts[0] if parts else remainder
                    trim = parts[1] if len(parts) > 1 else ""
                    return (year, make, model, trim)
            # Unknown make: first word is make
            parts = rest.split(None, 2)
            make = parts[0] if parts else ""
            model = parts[1] if len(parts) > 1 else ""
            trim = parts[2] if len(parts) > 2 else ""
            return (year, make, model, trim)
        return ("", "", name, "")
    year = m.group(1)
    make = m.group(2).strip()
    rest = m.group(3).strip()
    parts = rest.split(None, 1)
    model = parts[0] if parts else rest
    trim = parts[1] if len(parts) > 1 else ""
    return (year, make, model, trim)


def _fmt_price(raw: str) -> str:
    cleaned = re.sub(r"[^\d.]", "", str(raw))
    try:
        val = float(cleaned)
        if val > 0:
            return f"${val:,.0f}"
    except ValueError:
        pass
    return raw.strip() if raw else "--"


def _fmt_mileage(raw: str) -> str:
    cleaned = re.sub(r"[^\d]", "", str(raw))
    try:
        val = int(cleaned)
        if val > 0:
            return f"{val:,} mi"
    except ValueError:
        pass
    return raw.strip() if raw else "--"


# ---------------------------------------------------------------------------
# Dispatcher
# ---------------------------------------------------------------------------

def scrape_craigslist(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Craigslist search results."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    seen_pids = set()

    for card in soup.select(".cl-search-result"):
        pid = card.get("data-pid", "")
        if pid in seen_pids:
            continue
        seen_pids.add(pid)

        title = card.get("title", "")
        if not title:
            continue
        year, make, model, trim = _parse_vehicle_name(title)
        if not year:
            continue

        # URL
        link = card.select_one("a[href*='craigslist.org']")
        url = link.get("href", "") if link else ""

        # Price from .priceinfo
        price_el = card.select_one(".priceinfo")
        price = price_el.get_text(strip=True) if price_el else ""

        # Mileage from .meta -- format like "104k mi"
        mileage = ""
        meta_el = card.select_one(".meta")
        if meta_el:
            meta_text = meta_el.get_text(" ", strip=True)
            mi_match = re.search(r"(\d+)k\s*mi", meta_text, re.IGNORECASE)
            if mi_match:
                mileage = str(int(mi_match.group(1)) * 1000)

        # Image thumbnail
        img_url = ""
        img_el = card.select_one("img")
        if img_el:
            img_url = img_el.get("src", "") or img_el.get("data-src", "")

        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
            description=title, image_url=img_url,
        ))

    return vehicles


def scrape_facebook(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Facebook Marketplace vehicle listings."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    seen_ids = set()

    for link in soup.select('a[href*="/marketplace/item/"]'):
        href = link.get("href", "")
        # Extract item ID from URL
        item_match = re.search(r"/marketplace/item/(\d+)", href)
        if not item_match:
            continue
        item_id = item_match.group(1)
        if item_id in seen_ids:
            continue
        seen_ids.add(item_id)

        # The link text contains: $price YEAR Make Model ... Location ... Miles
        text = link.get_text(" ", strip=True)
        if not text:
            continue

        # Extract location from span (pattern: "City, ST")
        location = ""
        for span in link.select("span"):
            st = span.get_text(strip=True)
            if re.match(r"^[A-Z][a-z].+,\s*[A-Z]{2}$", st):
                location = st
                break

        # Extract price: first $XX,XXX pattern
        price_match = re.search(r"\$([\d,]+)", text)
        price = f"${price_match.group(1)}" if price_match else ""

        # Strip price from text to parse vehicle name
        name_text = re.sub(r"\$[\d,]+", "", text).strip()

        # Extract mileage: "XXK miles" pattern
        mileage = ""
        mi_match = re.search(r"([\d,.]+)\s*[Kk]\s*miles?", name_text)
        if mi_match:
            val = mi_match.group(1).replace(",", "")
            try:
                mileage = str(int(float(val) * 1000))
            except ValueError:
                pass

        # Parse vehicle name (the YEAR Make Model part)
        year, make, model, trim = _parse_vehicle_name(name_text)
        if not year:
            continue

        url = href if href.startswith("http") else f"https://www.facebook.com{href}"
        # Clean URL to remove tracking params
        url = url.split("?")[0]

        # Extract thumbnail image
        img_el = link.select_one("img")
        img_url = img_el.get("src", "") if img_el else ""

        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
            description=f"fb_location:{location} {name_text}" if location else name_text,
            image_url=img_url,
        ))

    # Also extract listings from embedded JSON data (FB loads some via React/Relay)
    for item_id in re.findall(r'GroupCommerceProductItem","id":"(\d+)"', html):
        if item_id in seen_ids:
            continue
        # Extract title
        tm = re.search(item_id + r'".*?marketplace_listing_title":"((?:[^"\\]|\\.)*)"', html)
        if not tm:
            continue
        raw_title = tm.group(1).replace("\\u00b7", "·").replace("\\/", "/")
        year, make, model, trim = _parse_vehicle_name(raw_title)
        if not year:
            continue
        seen_ids.add(item_id)
        # Extract price
        pm = re.search(item_id + r'".*?formatted_amount":"(\$[^"]*)"', html)
        price = pm.group(1) if pm else ""
        # Extract mileage from subtitle
        mileage = ""
        mm = re.search(item_id + r'".*?subtitle":"([^"]*miles[^"]*)"', html)
        if mm:
            mi_match = re.search(r"([\d,.]+)\s*[Kk]\s*miles?", mm.group(1))
            if mi_match:
                val = mi_match.group(1).replace(",", "")
                try:
                    mileage = str(int(float(val) * 1000))
                except ValueError:
                    pass
        # Extract location
        loc = ""
        lm = re.search(item_id + r'".*?"city":"([^"]*)".*?"state":"([^"]*)"', html)
        if lm:
            loc = f"{lm.group(1)}, {lm.group(2)}"

        url = f"https://www.facebook.com/marketplace/item/{item_id}"
        desc = f"fb_location:{loc} {raw_title} {mm.group(1) if mm else ''}".strip() if loc else f"{raw_title} {mm.group(1) if mm else ''}".strip()
        # Extract image from JSON
        img_url = ""
        img_m = re.search(item_id + r'".*?primary_listing_photo":\{[^}]*"image":\{"uri":"([^"]+)"', html)
        if img_m:
            img_url = img_m.group(1).replace("\\/", "/")
        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model,
            trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
            description=desc, image_url=img_url,
        ))

    return vehicles


def scrape_carscom(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Cars.com search results."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    seen_ids = set()

    for card in soup.select("[data-listing-id]"):
        listing_id = card.get("data-listing-id", "")
        if listing_id in seen_ids:
            continue
        seen_ids.add(listing_id)

        # Title: h2 > span or just h2 text
        title_el = card.select_one("h2")
        if not title_el:
            continue
        title = title_el.get_text(strip=True)
        year, make, model, trim = _parse_vehicle_name(title)
        if not year:
            continue

        # Price: span with class containing "spark-body-larger" or data-test="price"
        price = ""
        price_el = card.select_one("span.spark-body-larger, [data-test='price']")
        if price_el:
            price = price_el.get_text(strip=True)

        # Mileage: div with mileage class or data
        mileage = ""
        mi_el = card.select_one("div.datum-icon.mileage, [data-test='mileage']")
        if mi_el:
            mi_text = mi_el.get_text(strip=True)
            mi_match = re.search(r"([\d,]+)\s*mi", mi_text, re.IGNORECASE)
            if mi_match:
                mileage = mi_match.group(1).replace(",", "")

        # Dealer name from card (override the generic "Cars.com" dealer name)
        dealer_el = card.select_one("span.spark-body-small, [data-test='dealer-name']")
        card_dealer = dealer_el.get_text(strip=True) if dealer_el else dealer_name

        # Distance from card text: "City, ST (XX mi)"
        card_dist = ""
        card_text = card.get_text("\n", strip=True)
        dist_m = re.search(r"\((\d+)\s*mi\)", card_text)
        if dist_m:
            card_dist = dist_m.group(1)

        # URL
        url = ""
        link = card.select_one("a[href*='/vehicledetail/'], a[href*='/vehicle/']")
        if link:
            href = link.get("href", "")
            url = href if href.startswith("http") else f"https://www.cars.com{href}"

        cc_card_text = card.get_text(" ", strip=True)
        desc = cc_card_text[:500] if len(cc_card_text) > 500 else cc_card_text
        # Image thumbnail
        img_url = ""
        img_el = card.select_one("img")
        if img_el:
            img_url = img_el.get("src", "") or img_el.get("data-src", "")
        vehicles.append(Vehicle(
            dealer=card_dealer, year=year, make=make, model=model,
            trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
            distance_mi=card_dist, description=desc, image_url=img_url,
        ))

    return vehicles


def scrape_cargurus(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """CarGurus search results -- card-based layout."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    seen_ids = set()

    for card in soup.find_all(attrs={"class": re.compile(r"Card")}):
        text = card.get_text("\n", strip=True)

        # Must contain a Nissan Frontier (or any year + make + model)
        ym = re.search(r"(\d{4})\s+(\w+)\s+(\w+)", text)
        if not ym:
            continue

        year = ym.group(1)
        make = ym.group(2)
        model = ym.group(3)

        # Trim: line after "Learn more about this YYYY Make Model"
        trim = ""
        tm = re.search(r"Learn more about this.*?\n\n?(.+?)(?:\n|$)", text)
        if tm:
            t = tm.group(1).strip()
            if not t.startswith("Learn") and not re.match(r"[\d,]+\s*mi", t):
                trim = t

        # Detail link
        link_tag = card.find("a", href=re.compile(r"/details/"))
        if not link_tag:
            continue
        href = link_tag.get("href", "")
        # Extract listing ID for dedup
        lid = re.search(r"/details/(\d+)", href)
        if lid:
            lid_str = lid.group(1)
            if lid_str in seen_ids:
                continue
            seen_ids.add(lid_str)

        url = href if href.startswith("http") else f"https://www.cargurus.com{href}"

        # Price: the h4 inside the card
        price = "--"
        h4 = card.find("h4")
        if h4:
            pm = re.search(r"\$([\d,]+)", h4.get_text())
            if pm:
                price = f"${pm.group(1)}"

        # Mileage
        mileage = "--"
        mi_m = re.search(r"([\d,]+)\s*mi\b", text)
        if mi_m:
            mileage = f"{mi_m.group(1)} mi"

        # Location
        loc_m = re.search(r"\n([A-Z][a-z]+(?:\s[A-Z][a-z]+)*,\s*[A-Z]{2})\n", text)
        location = loc_m.group(1) if loc_m else ""

        # Dealer name from "Sponsored by ..." or "Sold by ..."
        card_dealer = dealer_name
        dm = re.search(r"(?:Sponsored by|Sold by)\s+(.+?)(?:\n|$)", text)
        if dm:
            card_dealer = dm.group(1).strip()

        desc = text[:500] if len(text) > 500 else text
        vehicles.append(Vehicle(
            dealer=card_dealer, year=year, make=make, model=model,
            trim=trim, price=price, mileage=_fmt_mileage(mileage), url=url,
            description=desc,
        ))

    return vehicles


def scrape_motive(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """Motive / RideMotive platform (Lee Johnson Nissan of Kirkland, etc.)."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    seen_vins = set()

    for link in soup.select('a[href*="/inventory/Used-"]'):
        href = link.get("href", "")
        # URL pattern: /inventory/Used-{Year}-{Make}-{Model}-{Trim}-{VIN}
        m = re.search(r"/inventory/Used-(\d{4})-([\w-]+?)-(.*?)-(\w{17})$", href)
        if not m:
            continue
        vin = m.group(4)
        if vin in seen_vins:
            continue
        seen_vins.add(vin)

        year = m.group(1)
        make = m.group(2).replace("_", " ")
        rest = m.group(3).replace("_", " ")
        # Split rest into model and trim: last segment is trim if there are multiple
        parts = rest.rsplit("-", 1)
        if len(parts) == 2:
            model_part, trim = parts
        else:
            model_part, trim = parts[0], ""
        model_part = model_part.replace("-", " ")
        trim = trim.replace("-", " ").replace("_", " ")

        # Walk up the DOM to find price and mileage text
        price = "\u2014"
        mileage = "\u2014"
        card = link
        for _ in range(15):
            card = card.parent
            if card is None or card.name == "body":
                break
            text = card.get_text(" ", strip=True)
            if "Our Price" in text or "Retail Price" in text:
                pm = re.search(r"Our Price\s*\$([\d,]+)", text)
                if not pm:
                    pm = re.search(r"Selling Price\s*\$([\d,]+)", text)
                if pm:
                    price = f"${pm.group(1)}"
                mi = re.search(r"([\d.]+)k\s*mi", text)
                if mi:
                    mi_val = float(mi.group(1)) * 1000
                    mileage = f"{int(mi_val):,} mi"
                break

        url = href if href.startswith("http") else urljoin(base_url, href)
        card_text = card.get_text(" ", strip=True) if card and card.name != "body" else ""
        desc = card_text[:500] if card_text else f"{year} {make} {model_part} {trim}".strip()
        vehicles.append(Vehicle(
            dealer=dealer_name, year=year, make=make, model=model_part,
            trim=trim, price=price, mileage=mileage, url=url,
            description=desc,
        ))

    return vehicles


def scrape_autotrader(html: str, dealer_name: str, base_url: str) -> list[Vehicle]:
    """AutoTrader search results."""
    soup = BeautifulSoup(html, "lxml")
    vehicles: list[Vehicle] = []
    seen = set()

    for card in soup.select('[data-cmp="inventoryListing"]'):
        # Title: look for data-cmp="link" inside heading or just h2/h3
        title = ""
        title_el = card.select_one(
            '[data-cmp="link"], h2, h3, '
            'a[data-cmp="inventoryListingTitle"]'
        )
        if title_el:
            title = title_el.get_text(strip=True)
        if not title:
            # Fallback: get all text from card and parse
            card_text = card.get_text(" ", strip=True)
            ym = re.search(r"((?:Used|New|Certified)?\s*\d{4}\s+\w+\s+\w+)", card_text)
            if ym:
                title = ym.group(1)
        if not title:
            continue

        year, make, model, trim = _parse_vehicle_name(title)
        if not year:
            continue

        dedup_key = f"{year}|{make}|{model}|{trim}"

        # Price
        price = ""
        price_el = card.select_one(
            '[data-cmp="firstPrice"], [data-cmp="pricing"], '
            'span[class*="first-price"], div[class*="price-section"]'
        )
        if price_el:
            price_text = price_el.get_text(strip=True)
            pm = re.search(r"\$?([\d,]+)", price_text)
            if pm:
                price = pm.group(1).replace(",", "")
        if not price:
            # Fallback: find $ in card text
            card_text = card.get_text(" ", strip=True)
            pm = re.search(r"\$([\d,]+)", card_text)
            if pm:
                price = pm.group(1).replace(",", "")

        # Mileage
        mileage = ""
        mi_el = card.select_one('[data-cmp="mileage"]')
        if mi_el:
            mi_text = mi_el.get_text(strip=True)
            mm = re.search(r"([\d,]+)\s*mi", mi_text, re.IGNORECASE)
            if mm:
                mileage = mm.group(1).replace(",", "")
        if not mileage:
            card_text = card.get_text(" ", strip=True)
            mm = re.search(r"([\d,]+)\s*[Kk]\s*mi", card_text)
            if mm:
                val = mm.group(1).replace(",", "")
                try:
                    mileage = str(int(float(val) * 1000))
                except ValueError:
                    pass
            if not mileage:
                mm = re.search(r"([\d,]+)\s*mi", card_text, re.IGNORECASE)
                if mm:
                    mileage = mm.group(1).replace(",", "")

        dedup_key += f"|{price}|{mileage}"
        if dedup_key in seen:
            continue
        seen.add(dedup_key)

        # Dealer name and distance from card text
        card_dealer = dealer_name
        card_dist = ""
        card_text = card.get_text("\n", strip=True)
        lines = [l.strip() for l in card_text.split("\n") if l.strip()]
        for j, line in enumerate(lines):
            dist_m = re.match(r"([\d.]+)\s*mi\.?\s*away", line)
            if dist_m and j > 0:
                card_dist = str(round(float(dist_m.group(1))))
                card_dealer = lines[j - 1]
                # Skip if the "dealer" is actually a phone number or badge
                if re.match(r"^[\d()\s\-]{7,}$", card_dealer) or len(card_dealer) < 3:
                    card_dealer = dealer_name
                break
        # Fallback: try CSS selector
        if card_dealer == dealer_name:
            dealer_el = card.select_one('[data-cmp="dealerName"], [data-cmp="sellerName"]')
            if dealer_el:
                card_dealer = dealer_el.get_text(strip=True)

        # URL
        url = ""
        link = card.select_one('a[href*="/cars-for-sale/vehicledetails"]')
        if link:
            href = link.get("href", "")
            url = href if href.startswith("http") else f"https://www.autotrader.com{href}"
        if not url:
            # Any link in card
            link = card.select_one("a[href]")
            if link:
                href = link.get("href", "")
                if "/cars-for-sale/" in href:
                    url = href if href.startswith("http") else f"https://www.autotrader.com{href}"

        at_card_text = card.get_text(" ", strip=True)
        desc = at_card_text[:500] if len(at_card_text) > 500 else at_card_text

        # If trim is empty, try to re-parse from the full card text
        # e.g. "Used 2018 Nissan Frontier S 126K mi..." gives trim "S"
        if not trim:
            full_title_m = re.search(
                r"(?:Used|New|Certified(?:\s+Pre-?Owned)?)?\s*"
                + re.escape(f"{year} {make} {model}") + r"\s+(.+?)(?:\s+\d[\d,]*\s*[Kk]?\s*mi|$)",
                at_card_text, re.IGNORECASE,
            )
            if full_title_m:
                trim = full_title_m.group(1).strip()

        vehicles.append(Vehicle(
            dealer=card_dealer, year=year, make=make, model=model,
            trim=trim, price=_fmt_price(price), mileage=_fmt_mileage(mileage), url=url,
            distance_mi=card_dist, description=desc,
        ))

    return vehicles


SCRAPERS = {
    "dealer_eprocess": scrape_dealer_eprocess,
    "dealeron": scrape_dealeron,
    "team_velocity": scrape_team_velocity,
    "dealer_com": scrape_dealer_com,
    "dealer_inspire": scrape_dealer_inspire,
    "automanager": scrape_automanager,
    "motive": scrape_motive,
    "craigslist": scrape_craigslist,
    "facebook": scrape_facebook,
    "carscom": scrape_carscom,
    "autotrader": scrape_autotrader,
    "cargurus": scrape_cargurus,
    "foxdealer": None,  # handled via early return in scan_dealer (API-based)
}


# ---------------------------------------------------------------------------
# Search URL builders -- inject make/model into dealer-specific search URLs
# ---------------------------------------------------------------------------

def _build_search_url(dealer: dict, make: str = "", model: str = "",
                      min_year: int = 0, max_year: int = 0,
                      min_price: float = 0, max_price: float = 0,
                      max_miles: float = 0, facebook_query: str = "") -> str:
    """Build a search-specific URL for the dealer, filtering by make/model if given."""
    platform = dealer.get("platform", "generic")
    base_url = dealer["base_url"]
    inv_url = dealer["inventory_url"]

    if not make:
        return inv_url

    mk = make.lower().strip()
    md = model.lower().strip() if model else ""

    if platform == "dealer_eprocess":
        # eProcess make/model slugs are cosmetic -- server ignores them.
        # Worse, they break arrow-based pagination.  Keep the original
        # inventory_url path (which has city-state) and just add ct=48.
        # Post-scrape _filter_by_make_model() handles make/model filtering.
        from urllib.parse import urlparse, parse_qs, urlencode, urlunparse
        parsed = urlparse(inv_url)
        params = parse_qs(parsed.query)
        params["tp"] = ["used"]
        params["ct"] = ["48"]  # max results per page (default 12 misses vehicles)
        new_query = urlencode(params, doseq=True)
        return urlunparse(parsed._replace(query=new_query))

    elif platform == "dealeron":
        # Pattern: /searchused.aspx?Makes=Nissan&Models=Frontier
        url = f"{base_url}/searchused.aspx?Makes={make}"
        if md:
            url += f"&Models={model}"
        return url

    elif platform == "team_velocity":
        # Pattern: /inventory/used/make/model
        url = f"{base_url}/inventory/used/{mk}"
        if md:
            url += f"/{md}"
        return url

    elif platform == "dealer_com":
        # Use the dealer's own inventory URL and append make/model params
        from urllib.parse import urlparse, parse_qs, urlencode, urlunparse
        parsed = urlparse(inv_url)
        params = parse_qs(parsed.query)
        if mk:
            params["make"] = [make]
        if md:
            params["model"] = [model]
        # Some dealer.com sites use "search" param for text search
        if "search" not in params and not any(k in params for k in ["make", "model"]):
            if md:
                params["search"] = [f"{make} {model}"]
        new_query = urlencode(params, doseq=True)
        return urlunparse(parsed._replace(query=new_query))

    elif platform == "carmax":
        # Pattern: /cars/nissan/frontier?zipcode=98155&radius=25
        slug = mk
        if md:
            slug += f"/{md}"
        return f"{base_url}/cars/{slug}?zipcode=98155&radius=50"

    elif platform == "dealer_inspire":
        # All DI sites use Algolia InstantSearch -- standard query params are ignored
        from urllib.parse import urlparse, urlencode
        parsed = urlparse(inv_url)
        params = {}
        if mk:
            params["_dFR[make][0]"] = make.strip()
        if md:
            params["_dFR[model][0]"] = model.strip()
        return f"{base_url}{parsed.path}?{urlencode(params)}"

    elif platform == "automanager":
        # Pattern: /view-inventory?make=Nissan
        from urllib.parse import urlparse, parse_qs, urlencode, urlunparse
        parsed = urlparse(inv_url)
        params = parse_qs(parsed.query)
        if mk:
            params["make"] = [make]  # AutoManager uses capitalized make
        new_query = urlencode(params, doseq=True)
        return urlunparse(parsed._replace(query=new_query))

    elif platform == "craigslist":
        # Pattern: /search/cta?auto_make_model=make+model&min_auto_year=X&max_auto_year=Y
        from urllib.parse import urlencode
        params = {}
        if mk or md:
            params["auto_make_model"] = f"{mk} {md}".strip()
        if min_year:
            params["min_auto_year"] = str(min_year)
        if max_year:
            params["max_auto_year"] = str(max_year)
        if min_price:
            params["min_price"] = str(int(min_price))
        if max_price:
            params["max_price"] = str(int(max_price))
        if max_miles:
            params["max_auto_miles"] = str(int(max_miles))
        return f"{base_url}/search/cta?{urlencode(params)}"

    elif platform == "facebook":
        # FB marketplace search: /marketplace/<city>/search/?query=<terms>
        # Price/sort params break search results, so only pass query.
        # Price filtering happens post-scrape.
        from urllib.parse import urlencode
        params = {}
        if facebook_query:
            params["query"] = facebook_query
        elif md:
            params["query"] = md
        elif mk:
            params["query"] = mk
        return f"{inv_url}?{urlencode(params)}"

    elif platform == "carscom":
        from urllib.parse import urlencode
        params = {
            "stock_type": "used",
            "maximum_distance": "75",
            "zip": "98155",
        }
        if mk:
            params["makes[]"] = mk
        if md:
            params["models[]"] = f"{mk}-{md}"
        if min_year:
            params["year_min"] = str(min_year)
        if max_year:
            params["year_max"] = str(max_year)
        if min_price:
            params["list_price_min"] = str(int(min_price))
        if max_price:
            params["list_price_max"] = str(int(max_price))
        if max_miles:
            params["mileage_max"] = str(int(max_miles))
        return f"https://www.cars.com/shopping/results/?{urlencode(params)}"

    elif platform == "motive":
        # Motive SPA uses JSON-encoded filters in the URL
        import json as _json
        from urllib.parse import quote
        filters = {
            "sortBy": {"option": {"label": "Price", "value": "price", "type": "value"}, "direction": "asc"},
            "appliedFilters": {"car_condition": {"value": {"Used": True}}},
        }
        if mk:
            filters["appliedFilters"]["make"] = {"value": {make: True}}
        if md:
            filters["appliedFilters"]["model"] = {"value": {model: True}}
        return f"{base_url}/inventory?filters={quote(_json.dumps(filters))}"

    elif platform == "autotrader":
        from urllib.parse import urlencode
        params = {
            "zip": "98155",
            "searchRadius": "75",
        }
        if min_year:
            params["startYear"] = str(min_year)
        if max_year:
            params["endYear"] = str(max_year)
        if min_price:
            params["minPrice"] = str(int(min_price))
        if max_price:
            params["maxPrice"] = str(int(max_price))
        if max_miles:
            params["maxMileage"] = str(int(max_miles))
        slug_make = mk if mk else ""
        slug_model = md if md else ""
        path = "/cars-for-sale/used/"
        if slug_make:
            path += f"{slug_make}/"
        if slug_model:
            path += f"{slug_model}/"
        return f"https://www.autotrader.com{path}?{urlencode(params)}"

    elif platform == "cargurus":
        from urllib.parse import urlencode
        # Entity d240 = Nissan Frontier
        params = {
            "sourceContext": "carGurusHomePageModel",
            "entitySelectingHelper.selectedEntity": inv_url.split("selectedEntity=")[-1].split("&")[0] if "selectedEntity=" in inv_url else "d240",
            "zip": "98155",
            "searchDistance": "75",
        }
        if min_year:
            params["startYear"] = str(min_year)
        if max_year:
            params["endYear"] = str(max_year)
        if min_price:
            params["minPrice"] = str(int(min_price))
        if max_price:
            params["maxPrice"] = str(int(max_price))
        if max_miles:
            params["maxMileage"] = str(int(max_miles))
        return f"https://www.cargurus.com/Cars/inventorylisting/viewDetailsFilterViewInventoryListing.action?{urlencode(params)}"

    return inv_url


def _get_page_url(url: str, page: int, platform: str) -> str:
    """Return URL for a specific page number."""
    from urllib.parse import urlparse, parse_qs, urlencode, urlunparse
    if page <= 1:
        return url

    parsed = urlparse(url)
    params = parse_qs(parsed.query)

    if platform == "dealer_eprocess":
        params["pg"] = [str(page)]
    elif platform == "dealeron":
        params["page"] = [str(page)]
    elif platform == "team_velocity":
        params["start"] = [str((page - 1) * 25)]
    elif platform == "dealer_com":
        params["page"] = [str(page)]
    elif platform == "dealer_inspire":
        # All DI sites use Algolia with _p (0-based)
        params["_p"] = [str(page - 1)]
    else:
        params["page"] = [str(page)]

    new_query = urlencode(params, doseq=True)
    return urlunparse(parsed._replace(query=new_query))


def _detect_page_count(html: str, platform: str) -> int:
    """Try to detect total page count from HTML."""
    soup = BeautifulSoup(html, "lxml")
    # Look for "Page X of Y" patterns
    text = soup.get_text()
    m = re.search(r"Page\s*\d+\s*of\s*(\d+)", text, re.IGNORECASE)
    if m:
        return int(m.group(1))
    # Look for pagination links
    pages = set()
    for a in soup.select("a[href]"):
        href = a.get("href", "")
        for param in ["pg=", "page="]:
            pm = re.search(rf"{param}(\d+)", href)
            if pm:
                pages.add(int(pm.group(1)))
    return max(pages) if pages else 1


def _fetch_html(url: str, needs_browser: bool) -> str | None:
    """Fetch HTML from a URL, using browser if needed."""
    html = None
    if not needs_browser:
        resp = fetch(url)
        if resp:
            html = resp.text
    if html is None:
        html = fetch_with_browser(url)
    return html


def _fetch_dealeron(dealer: dict, inv_url: str, name: str, base_url: str) -> list[Vehicle]:
    """Fetch all vehicles from a DealerOn Cosmos SPA dealer.

    Navigates to the search page with pn=96 (max page size), intercepts the
    internal REST API response to extract vehicle data from JSON, then clicks
    through any remaining pagination pages.
    """
    if not HAS_PLAYWRIGHT:
        print("    [!]  Playwright not installed")
        return []

    print("    [~]  DealerOn: headed browser (API intercept)...")
    import json as _json

    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(
                headless=False,
                args=["--window-position=-10000,-10000",
                      "--disable-blink-features=AutomationControlled"],
            )
            ctx = browser.new_context(
                user_agent=SESSION.headers["User-Agent"],
                viewport={"width": 1920, "height": 1080},
                locale="en-US",
            )
            page = ctx.new_page()

            # Collect API responses containing vehicle data
            api_bodies: list[str] = []

            def _on_response(resp):
                if "/api/vhcliaa/" in resp.url and "vehicles" in resp.url:
                    try:
                        api_bodies.append(resp.text())
                    except Exception:
                        pass

            page.on("response", _on_response)

            # Navigate with pn=96 (max supported page size)
            url = f"{base_url}/searchused.aspx?pn=96"
            page.goto(url, wait_until="domcontentloaded", timeout=60000)
            page.wait_for_timeout(8000)

            # Parse vehicles from API JSON responses
            def _parse_api_body(body: str) -> tuple[dict, dict[str, dict]]:
                data = _json.loads(body)
                paging = data.get("Paging", {}).get("PaginationDataModel", {})
                vehicles = {}
                for card in data.get("DisplayCards", []):
                    vc = card.get("VehicleCard")
                    if not vc:
                        continue
                    vin = vc.get("VehicleVin", "")
                    if not vin:
                        continue
                    # Determine price: TaggingPrice, then VehicleInternetPrice,
                    # then decode VehiclePriceLibrary (base64 "Internet Price:XXXX;...")
                    raw_price = str(vc.get("TaggingPrice", "0"))
                    if raw_price in ("0", "", "0.0"):
                        inet = vc.get("VehicleInternetPrice", 0)
                        if inet and float(inet) > 0:
                            raw_price = str(inet)
                        else:
                            lib = vc.get("VehiclePriceLibrary", "")
                            if lib:
                                try:
                                    import base64
                                    decoded = base64.b64decode(lib).decode()
                                    pm = re.search(r"(?:Internet|Selling)\s*Price[:\s]*([\d.]+)", decoded)
                                    if pm and float(pm.group(1)) > 0:
                                        raw_price = pm.group(1)
                                except Exception:
                                    pass
                    vehicles[vin] = {
                        "year": str(vc.get("VehicleYear", "")),
                        "make": vc.get("VehicleMake", ""),
                        "model": vc.get("VehicleModel", ""),
                        "trim": vc.get("VehicleTrim") or vc.get("VehicleRuleAdjustedTrim", ""),
                        "vin": vin,
                        "price": raw_price,
                        "mileage": vc.get("Mileage", "--"),
                        "url": vc.get("VehicleDetailUrl", ""),
                    }
                return paging, vehicles

            all_vehicles: dict[str, dict] = {}
            total_pages = 1

            if api_bodies:
                paging, vehicles = _parse_api_body(api_bodies[-1])
                total_pages = paging.get("TotalPages", 1)
                total_count = paging.get("TotalCount", 0)
                all_vehicles.update(vehicles)
                print(f"    [ok] API page 1/{total_pages}: {len(vehicles)} vehicles (total: {total_count})")

            # Click through remaining pages
            for pg in range(2, total_pages + 1):
                before = len(api_bodies)
                # Dismiss overlays (cookie consent, chat widgets) that block clicks
                page.evaluate("""() => {
                    for (const sel of ['#ca-consent-root', '#podium-website-widget',
                                       '.gg-app', '.gubagoo-wrapper', '[class*="chat-widget"]']) {
                        const el = document.querySelector(sel);
                        if (el) el.remove();
                    }
                    // Click any "Allow cookies" button
                    for (const btn of document.querySelectorAll('button')) {
                        if (/allow|accept|agree/i.test(btn.textContent)) { btn.click(); break; }
                    }
                    window.scrollTo(0, document.body.scrollHeight);
                }""")
                page.wait_for_timeout(500)
                pg_link = page.locator(
                    f'li.pagination__item:not(.disabled):not(.active) a.pagination__link:has-text("{pg}")'
                )
                if pg_link.count() == 0:
                    pg_link = page.locator("li.pagination__item--next a.pagination__link")
                if pg_link.count() > 0:
                    try:
                        pg_link.first.click(timeout=10000)
                    except Exception:
                        # Force click via JS if overlays still block
                        pg_link.first.dispatch_event("click")
                    page.wait_for_timeout(5000)
                    if len(api_bodies) > before:
                        paging, vehicles = _parse_api_body(api_bodies[-1])
                        all_vehicles.update(vehicles)
                        print(f"    [ok] API page {pg}/{total_pages}: +{len(vehicles)} vehicles")
                    else:
                        print(f"    [!] Page {pg}: no API response")
                else:
                    print(f"    [!] Page {pg}: no link found")

            browser.close()

            # Build Vehicle objects
            result = []
            for v in all_vehicles.values():
                if not v.get("year") or not v.get("make"):
                    continue
                result.append(Vehicle(
                    dealer=name, year=v["year"], make=v["make"], model=v["model"],
                    trim=v.get("trim", ""), price=_fmt_price(v.get("price", "")),
                    mileage=v.get("mileage", "--"), url=v.get("url", ""),
                    vin=v.get("vin", ""),
                ))

            if not result:
                print(f"    [!] API returned 0 vehicles, trying HTML fallback...")
            return result
    except Exception as exc:
        print(f"    [!]  DealerOn fetch failed: {exc}")
        return []


def _fetch_eprocess(dealer: dict, inv_url: str, name: str, base_url: str,
                    search_make: str, search_model: str) -> list[Vehicle]:
    """Fetch all vehicles from an eProcess dealer.

    Tries three strategies in order:
    1. JS data cache (__textSearchDataCache) -- gets full inventory instantly.
    2. Pagination arrows -- clicks through pages to collect all vehicles.
    3. Filter navigation -- finds make filter link (mk= param) for server-side
       filtering, then collects JSON-LD from filtered pages.
    Falls back to standard JSON-LD scraping from whatever is on the page.
    """
    if not HAS_PLAYWRIGHT:
        print("    [!]  Playwright not installed")
        return []

    print("    [~]  eProcess: headed browser (interactive)...")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(
                headless=False,
                args=[
                    "--window-position=-10000,-10000",
                    "--disable-blink-features=AutomationControlled",
                ],
            )
            ctx = browser.new_context(
                user_agent=SESSION.headers["User-Agent"],
                viewport={"width": 1920, "height": 1080},
                locale="en-US",
            )
            ctx.add_init_script("""
                delete Object.getPrototypeOf(navigator).webdriver;
                Object.defineProperty(navigator, 'webdriver', {get: () => undefined});
            """)
            page = ctx.new_page()
            page.goto(inv_url, wait_until="domcontentloaded", timeout=60000)
            page.wait_for_timeout(6000)

            # --- Strategy 1: JS data cache ---
            vehicles = _eprocess_extract_cache(page, name, base_url)
            if vehicles:
                browser.close()
                return vehicles

            # --- Strategy 2: arrow pagination ---
            vehicles = _eprocess_paginate_arrows(page, name, base_url)
            if vehicles is not None:
                browser.close()
                return vehicles

            # --- Strategy 3: filter navigation (mk=/md= params) ---
            if search_make:
                vehicles = _eprocess_filter_navigate(
                    page, name, base_url, search_make, search_model)
                if vehicles:
                    browser.close()
                    return vehicles

            # --- Fallback: scrape whatever JSON-LD is on the page ---
            html = page.content()
            vehicles = scrape_dealer_eprocess(html, name, base_url)
            browser.close()
            return vehicles
    except Exception as exc:
        print(f"    [!]  eProcess fetch failed: {exc}")
        return []


def _eprocess_extract_cache(page, name: str, base_url: str) -> list[Vehicle]:
    """Extract all used vehicles from eProcess __textSearchDataCache."""
    data = page.evaluate("""() => {
        const cache = window.__textSearchDataCache;
        if (!cache) return null;
        const key = Object.keys(cache)[0];
        if (!key) return null;
        const ds = cache[key];
        if (!ds || !ds.vehicle_facts || !ds.canonical_lexicon) return null;

        // Build ID -> name lookup from lexicon
        const lookup = {};
        for (const entry of ds.canonical_lexicon) {
            if (entry.type && entry.id !== undefined)
                lookup[entry.type + ':' + entry.id] = entry.canonical;
        }

        // Extract used vehicles
        const vehicles = [];
        for (const [vid, v] of Object.entries(ds.vehicle_facts)) {
            if (v.flag_new === 1) continue;  // skip new vehicles
            vehicles.push({
                id: vid,
                year: String(v.year || ''),
                make: lookup['make:' + v.make] || '',
                model: lookup['model:' + v.model] || '',
                trim: lookup['trim:' + v.trim] || '',
                price: String(v.price || ''),
                vin: v.vin || '',
            });
        }
        return vehicles.length > 0 ? vehicles : null;
    }""")

    if not data:
        return []

    print(f"    [ok] JS cache: {len(data)} used vehicles")
    vehicles = []
    for v in data:
        if not v.get("year") or not v.get("make"):
            continue
        url = f"{base_url}/vehicle-details/{v['vin']}/" if v.get("vin") else ""
        vehicles.append(Vehicle(
            dealer=name, year=v["year"], make=v["make"], model=v["model"],
            trim=v.get("trim", ""), price=_fmt_price(v.get("price", "")),
            mileage="--", url=url, vin=v.get("vin", ""),
        ))
    return vehicles


def _eprocess_paginate_arrows(page, name: str, base_url: str) -> list[Vehicle] | None:
    """Paginate through eProcess pages using the arrow buttons.

    Returns None if no pagination controls exist (signal to try other strategies).
    Returns list (possibly empty) if this strategy handled the page.
    """
    right_arrow = page.query_selector("div.pagination_settings__arrow--right")
    page_count_el = page.query_selector(".pagination_settings__page_count")
    if not right_arrow and not page_count_el:
        return None  # no pagination controls -- try other strategies

    total_pages = int(page_count_el.inner_text()) if page_count_el else 1

    # Scrape page 1
    page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
    page.wait_for_timeout(1500)
    html = page.content()
    vehicles = scrape_dealer_eprocess(html, name, base_url)
    print(f"    [ok] Arrow pagination: page 1/{total_pages} -- {len(vehicles)} vehicles")

    for pg in range(2, total_pages + 1):
        right_arrow = page.query_selector("div.pagination_settings__arrow--right")
        if not right_arrow:
            print(f"    [!] No right arrow on page {pg-1}")
            break
        right_arrow.click()
        page.wait_for_timeout(5000)
        page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
        page.wait_for_timeout(2000)

        page_html = page.content()
        page_vehicles = scrape_dealer_eprocess(page_html, name, base_url)
        if not page_vehicles:
            print(f"    [!] No vehicles on page {pg}, stopping")
            break
        existing = {v.url for v in vehicles}
        new_vs = [v for v in page_vehicles if v.url not in existing]
        vehicles.extend(new_vs)
        print(f"    Page {pg}/{total_pages} -- +{len(new_vs)} vehicles")

    return vehicles


def _eprocess_filter_navigate(page, name: str, base_url: str,
                              search_make: str, search_model: str) -> list[Vehicle]:
    """Use eProcess make/model filter links (mk=/md= params) to get filtered results."""
    # Find the make filter link matching search_make
    filter_links = page.query_selector_all("a.srp_filter_option")
    make_href = None
    mk_lower = search_make.lower()
    for fl in filter_links:
        txt = fl.inner_text().strip().split("\n")[0].strip().lower()
        href = fl.get_attribute("href") or ""
        if txt == mk_lower and "mk=" in href:
            make_href = href
            break
        # Partial match (e.g. "Mercedes-Benz" vs "mercedes")
        if mk_lower in txt and "mk=" in href:
            make_href = href
            break

    if not make_href:
        return []

    # Navigate to make-filtered URL
    make_url = make_href if make_href.startswith("http") else urljoin(base_url, make_href)
    print(f"    [ok] Filter navigate: make={search_make}")
    page.goto(make_url, wait_until="domcontentloaded", timeout=60000)
    page.wait_for_timeout(5000)

    # If we also have a model, find the model filter link
    if search_model:
        md_lower = search_model.lower()
        model_links = page.query_selector_all("a.srp_filter_option")
        for fl in model_links:
            txt = fl.inner_text().strip().split("\n")[0].strip().lower()
            href = fl.get_attribute("href") or ""
            if txt == md_lower and "md=" in href:
                model_url = href if href.startswith("http") else urljoin(base_url, href)
                print(f"    [ok] Filter navigate: model={search_model}")
                page.goto(model_url, wait_until="domcontentloaded", timeout=60000)
                page.wait_for_timeout(5000)
                break

    # Scrape JSON-LD from filtered page
    page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
    page.wait_for_timeout(1500)
    html = page.content()
    vehicles = scrape_dealer_eprocess(html, name, base_url)

    # If still multiple pages, try arrow pagination on filtered results
    right_arrow = page.query_selector("div.pagination_settings__arrow--right")
    page_count_el = page.query_selector(".pagination_settings__page_count")
    total_pages = int(page_count_el.inner_text()) if page_count_el else 1

    if total_pages > 1 and right_arrow:
        print(f"    Filtered results: page 1/{total_pages}")
        for pg in range(2, total_pages + 1):
            right_arrow = page.query_selector("div.pagination_settings__arrow--right")
            if not right_arrow:
                break
            right_arrow.click()
            page.wait_for_timeout(4000)
            page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
            page.wait_for_timeout(1500)
            page_html = page.content()
            page_vehicles = scrape_dealer_eprocess(page_html, name, base_url)
            if not page_vehicles:
                break
            existing = {v.url for v in vehicles}
            new_vs = [v for v in page_vehicles if v.url not in existing]
            vehicles.extend(new_vs)
            print(f"    Page {pg}/{total_pages} -- +{len(new_vs)} vehicles")

    return vehicles


def _fetch_cargurus(url: str, name: str, base_url: str) -> list[Vehicle]:
    """Fetch CarGurus results with persistent browser profile to keep DataDome cookies."""
    if not HAS_PLAYWRIGHT:
        print("    [!]  Playwright not installed")
        return []
    print("    [~]  Using headed browser with persistent profile...")
    cg_profile = Path(__file__).parent / ".cg_profile"
    cg_profile.mkdir(exist_ok=True)
    try:
        with sync_playwright() as p:
            ctx = p.chromium.launch_persistent_context(
                str(cg_profile),
                headless=False,
                args=["--disable-blink-features=AutomationControlled"],
                viewport={"width": 1366, "height": 768},
                locale="en-US",
            )
            ctx.add_init_script("""
                delete Object.getPrototypeOf(navigator).webdriver;
                Object.defineProperty(navigator, 'webdriver', {get: () => undefined});
                Object.defineProperty(navigator, 'plugins', {get: () => [1, 2, 3, 4, 5]});
                Object.defineProperty(navigator, 'languages', {get: () => ['en-US', 'en']});
                window.chrome = {runtime: {}, loadTimes: function(){}, csi: function(){}};
            """)
            page = ctx.new_page()
            # Warm up with homepage to establish DataDome cookies
            page.goto("https://www.cargurus.com/", wait_until="domcontentloaded", timeout=30000)
            page.wait_for_timeout(3000)
            page.goto(url, wait_until="domcontentloaded", timeout=60000)
            page.wait_for_timeout(8000)
            # Scroll to load lazy content
            for i in range(3):
                page.evaluate(f"window.scrollTo(0, {(i + 1) * 800})")
                page.wait_for_timeout(1000)

            html = page.content()
            if "captcha-delivery" in html:
                print("    [!]  CarGurus blocked with CAPTCHA -- run: py -3 scan.py --cg-setup")
                ctx.close()
                return []

            vehicles = scrape_cargurus(html, name, base_url)

            # Check for pagination ("Page X of Y")
            pag_text = ""
            pag_el = page.query_selector('[class*="aginat"]')
            if pag_el:
                pag_text = pag_el.inner_text()
            pm = re.search(r"Page\s*\d+\s*of\s*(\d+)", pag_text)
            total_pages = int(pm.group(1)) if pm else 1

            for pg in range(2, total_pages + 1):
                next_btn = page.query_selector('[aria-label*="ext"]')
                if not next_btn:
                    break
                next_btn.scroll_into_view_if_needed()
                page.wait_for_timeout(500)
                next_btn.click()
                page.wait_for_timeout(5000)
                # Scroll again
                for i in range(3):
                    page.evaluate(f"window.scrollTo(0, {(i + 1) * 800})")
                    page.wait_for_timeout(800)
                page_html = page.content()
                page_vehicles = scrape_cargurus(page_html, name, base_url)
                if not page_vehicles:
                    break
                # Deduplicate against existing
                existing_urls = {v.url for v in vehicles}
                new_vehicles = [v for v in page_vehicles if v.url not in existing_urls]
                vehicles.extend(new_vehicles)
                print(f"    Page {pg}/{total_pages} -- +{len(new_vehicles)} vehicles")

            ctx.close()
            return vehicles
    except Exception as exc:
        print(f"    [!]  CarGurus fetch failed: {exc}")
        return []


def scan_dealer(dealer: dict, search_make: str = "", search_model: str = "",
                min_year: int = 0, max_year: int = 0,
                min_price: float = 0, max_price: float = 0,
                max_miles: float = 0, facebook_query: str = "") -> list[Vehicle]:
    """Fetch and parse one dealer's used inventory."""
    name = dealer["name"]
    platform = dealer.get("platform", "generic")
    base_url = dealer["base_url"]
    needs_browser = dealer.get("needs_browser", False)

    # Build search-specific URL
    inv_url = _build_search_url(dealer, search_make, search_model,
                                min_year=min_year, max_year=max_year,
                                min_price=min_price, max_price=max_price,
                                max_miles=max_miles,
                                facebook_query=facebook_query)

    print(f"\n{'='*60}")
    print(f"  Scanning: {name}")
    print(f"  URL:      {inv_url}")
    print(f"  Platform: {platform}")
    print(f"{'='*60}")

    def _filter_by_make_model(vlist):
        """Filter vehicles by search_make / search_model (handles sites where URL filtering is cosmetic)."""
        result = vlist
        if search_make and result:
            before = len(result)
            result = [v for v in result if v.make.lower() == search_make.lower()]
            if len(result) < before:
                print(f"    Filtered by make ({search_make}): {before} -> {len(result)}")
        if search_model and result:
            before = len(result)
            sm = search_model.lower()
            # Match exact or model prefix (handles "Sportage Hybrid", "Frontier S", etc.)
            result = [v for v in result
                      if v.model.lower() == sm or v.model.lower().startswith(sm + " ")]
            if len(result) < before:
                print(f"    Filtered by model ({search_model}): {before} -> {len(result)}")
        return result

    # Facebook Marketplace needs a persistent browser profile with saved login
    if platform == "facebook":
        html = fetch_with_persistent_browser(inv_url, wait_ms=8000, scroll=True)
        if html is None:
            return []
        vehicles = scrape_facebook(html, name, base_url)
        vehicles = _filter_by_make_model(vehicles)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # CarGurus uses DataDome captcha -- needs headed browser with anti-detect;
    # pagination is click-based (no URL param), so we fetch+parse in one session.
    if platform == "cargurus":
        vehicles = _fetch_cargurus(inv_url, name, base_url)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # CarMax uses a JSON API, not HTML scraping
    if platform == "carmax":
        vehicles = scrape_carmax(name, base_url)
        if not vehicles:
            html = fetch_with_browser(inv_url, wait_ms=8000)
            if html:
                vehicles = extract_jsonld_vehicles(html, name, base_url)
        print(f"    Found {len(vehicles)} vehicles")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # FoxDealer uses Elasticsearch API, not HTML scraping
    if platform == "foxdealer":
        vehicles = scrape_foxdealer(dealer, search_make, search_model)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # Motive SPA needs headed browser with extra scrolling
    if platform == "motive":
        html = fetch_with_headed_browser(inv_url, wait_ms=8000)
        if html is None:
            return []
        vehicles = scrape_motive(html, name, base_url)
        vehicles = _filter_by_make_model(vehicles)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # AutoTrader blocks headless browsers; use headed mode (off-screen)
    if platform == "autotrader":
        html = fetch_with_headed_browser(inv_url, wait_ms=10000)
        if html is None:
            return []
        vehicles = scrape_autotrader(html, name, base_url)
        # If no vehicles found, AutoTrader may have shown a bot-check page
        if not vehicles:
            print("    [!]  AutoTrader may have blocked this request (bot detection)")
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # Cars.com blocks headless browsers; use headed mode
    if platform == "carscom":
        html = fetch_with_headed_browser(inv_url, wait_ms=6000)
        if html is None:
            return []
        vehicles = scrape_carscom(html, name, base_url)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # DealerOn Cosmos SPA: use API intercept to get all vehicles
    if platform == "dealeron":
        vehicles = _fetch_dealeron(dealer, inv_url, name, base_url)
        vehicles = _filter_by_make_model(vehicles)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # eProcess sites: use interactive browser (JS cache, arrow pagination, or filter nav)
    if platform == "dealer_eprocess":
        vehicles = _fetch_eprocess(dealer, inv_url, name, base_url,
                                   search_make, search_model)
        vehicles = _filter_by_make_model(vehicles)
        print(f"    Found {len(vehicles)} vehicles total")
        enrich_vehicle_details(vehicles, platform, needs_browser)
        return vehicles

    # Fetch page 1
    html = _fetch_html(inv_url, needs_browser)
    if html is None:
        return []

    scraper = SCRAPERS.get(platform)
    if scraper:
        vehicles = scraper(html, name, base_url)
    else:
        vehicles = extract_jsonld_vehicles(html, name, base_url)
        if not vehicles:
            soup = BeautifulSoup(html, "lxml")
            for card in soup.select(".vehicle-card, [data-vin], .hproduct, .inventory-listing"):
                v = _parse_html_card(card, name, base_url)
                if v:
                    vehicles.append(v)

    # If headless browser returned 0 vehicles, retry with headed browser
    if not vehicles and needs_browser:
        print("    [!]  Headless blocked -- retrying with headed browser...")
        html = fetch_with_headed_browser(inv_url, wait_ms=8000)
        if html:
            if scraper:
                vehicles = scraper(html, name, base_url)
            else:
                vehicles = extract_jsonld_vehicles(html, name, base_url)

    # Pagination: check for additional pages
    total_pages = _detect_page_count(html, platform)
    if total_pages > 1:
        print(f"    Page 1/{total_pages} -- fetching remaining pages...")
        for pg in range(2, total_pages + 1):
            page_url = _get_page_url(inv_url, pg, platform)
            time.sleep(1)
            page_html = _fetch_html(page_url, needs_browser)
            if page_html is None:
                break
            if scraper:
                page_vehicles = scraper(page_html, name, base_url)
            else:
                page_vehicles = extract_jsonld_vehicles(page_html, name, base_url)
            if not page_vehicles:
                break
            vehicles.extend(page_vehicles)
            print(f"    Page {pg}/{total_pages} -- +{len(page_vehicles)} vehicles")

    vehicles = _filter_by_make_model(vehicles)

    print(f"    Found {len(vehicles)} vehicles total")
    enrich_vehicle_details(vehicles, platform, needs_browser)
    return vehicles


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def print_results(vehicles: list[Vehicle]) -> None:
    if not vehicles:
        print("\nNo vehicles found matching your criteria.")
        return
    rows = [
        [v.dealer, v.year, v.make, v.model, v.trim, v.price, v.mileage]
        for v in vehicles
    ]
    headers = ["Dealer", "Year", "Make", "Model", "Trim", "Price", "Mileage"]
    print(f"\n{'='*80}")
    print(f"  RESULTS: {len(vehicles)} used vehicles found")
    print(f"{'='*80}\n")
    print(tabulate(rows, headers=headers, tablefmt="simple_outline"))


def _propagate_enriched_descriptions(vehicles: list[Vehicle]) -> None:
    """Copy enriched specs (Drive, Body, Title, etc.) to duplicate vehicles lacking them.

    Vehicles are matched by year + make + model + trim (case-insensitive).
    If one copy has enriched info (e.g. 'Drive: RWD') and another doesn't,
    the enriched specs are appended to the un-enriched vehicle's description.
    """
    # Build groups by (year, make, model, trim)
    groups: dict[tuple, list[Vehicle]] = {}
    for v in vehicles:
        key = (v.year, v.make.lower(), v.model.lower(), v.trim.lower().split()[0] if v.trim else "")
        groups.setdefault(key, []).append(v)

    propagated = 0
    for group in groups.values():
        if len(group) < 2:
            continue
        # Find the best enriched description (contains "Drive:" or "Title:")
        enriched_specs = ""
        for v in group:
            if v.description and re.search(r"\b(?:Drive|Title|Body):", v.description):
                # Extract just the enriched part (after the last " | " that starts a spec)
                m = re.search(r"((?:Drive|Body|Doors|Engine|Trans|Fuel|Title):.+)$", v.description)
                if m:
                    enriched_specs = m.group(1)
                    break
        if not enriched_specs:
            continue
        # Apply to vehicles in the group that lack enriched specs
        for v in group:
            if not re.search(r"\b(?:Drive|Title|Body):", v.description or ""):
                v.description = f"{v.description} | {enriched_specs}" if v.description else enriched_specs
                propagated += 1

    if propagated > 0:
        print(f"  [i] Propagated enriched specs to {propagated} duplicate listings")


def write_csv(vehicles: list[Vehicle], path: str) -> None:
    fieldnames = [f.name for f in fields(Vehicle)]
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for v in vehicles:
            writer.writerow(asdict(v))
    print(f"\n  Results saved to {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Scan local dealer websites near 98155 for used cars."
    )
    parser.add_argument("--make", help="Filter by make (e.g. Toyota, Honda)")
    parser.add_argument("--model", help="Filter by model (e.g. Camry, CR-V)")
    parser.add_argument("--min-price", type=float, help="Minimum price")
    parser.add_argument("--max-price", type=float, help="Maximum price")
    parser.add_argument("--min-year", type=int, help="Minimum model year")
    parser.add_argument("--max-year", type=int, help="Maximum model year")
    parser.add_argument("--max-miles", type=float, help="Maximum mileage")
    parser.add_argument("--facebook-query", help="Custom Facebook Marketplace search query (overrides model)")
    parser.add_argument("--csv", metavar="FILE", help="Save results to CSV file")
    parser.add_argument(
        "--dealers", default="dealers.json",
        help="Path to dealers config file (default: dealers.json)"
    )
    parser.add_argument(
        "--fb-setup", action="store_true",
        help="Open a browser to log into Facebook (run once before scanning FB Marketplace)"
    )
    parser.add_argument(
        "--cg-setup", action="store_true",
        help="Open a browser to solve CarGurus CAPTCHA (run once to save cookies)"
    )
    parser.add_argument(
        "--platforms",
        help="Comma-separated list of platforms to scan (e.g. craigslist,facebook)"
    )
    args = parser.parse_args()

    if args.fb_setup:
        fb_setup()
        return

    if args.cg_setup:
        cg_setup()
        return

    # Load dealer list
    dealers_path = Path(args.dealers)
    if not dealers_path.exists():
        print(f"Error: {dealers_path} not found. Create it or specify --dealers path.")
        sys.exit(1)

    with open(dealers_path, encoding="utf-8") as f:
        dealers = json.load(f)

    if args.platforms:
        allowed = {p.strip().lower() for p in args.platforms.split(",")}
        dealers = [d for d in dealers if d.get("platform", "").lower() in allowed]

    print(f"\nUsed Car Scanner -- Scanning {len(dealers)} dealers near 98155\n")

    # Scan each dealer
    all_vehicles: list[Vehicle] = []
    for dealer in dealers:
        try:
            vehicles = scan_dealer(
                dealer, search_make=args.make or "", search_model=args.model or "",
                min_year=args.min_year or 0, max_year=args.max_year or 0,
                min_price=args.min_price or 0, max_price=args.max_price or 0,
                max_miles=args.max_miles or 0,
                facebook_query=args.facebook_query or "",
            )
            all_vehicles.extend(vehicles)
        except Exception as exc:
            print(f"    [X] Error scanning {dealer['name']}: {exc}")
        time.sleep(1)  # polite delay between dealers

    # Apply filters
    filtered = all_vehicles

    # Deduplicate by URL, then by year+make+model+price+mileage (catches reposts)
    seen_keys = set()
    deduped = []
    for v in filtered:
        # Primary key: URL (catches pagination repeats)
        url_key = v.url if v.url and v.url != "--" else None
        if url_key and url_key in seen_keys:
            continue
        # Secondary key: same vehicle at same price/mileage (catches CL reposts)
        content_key = f"{v.year}|{v.make}|{v.model}|{v.price}|{v.mileage}"
        if content_key in seen_keys:
            continue
        if url_key:
            seen_keys.add(url_key)
        seen_keys.add(content_key)
        deduped.append(v)
    filtered = deduped

    if args.make:
        filtered = [v for v in filtered if args.make.lower() in v.make.lower()]
    if args.model:
        filtered = [v for v in filtered if args.model.lower() in v.model.lower()]
    if args.min_price:
        filtered = [v for v in filtered if v.price_num() == 0 or v.price_num() >= args.min_price]
    if args.max_price:
        filtered = [v for v in filtered if v.price_num() == 0 or v.price_num() <= args.max_price]
    if args.min_year:
        filtered = [
            v for v in filtered
            if v.year.isdigit() and int(v.year) >= args.min_year
        ]
    if args.max_year:
        filtered = [
            v for v in filtered
            if v.year.isdigit() and int(v.year) <= args.max_year
        ]
    if args.max_miles:
        filtered = [v for v in filtered if v.mileage_num() <= args.max_miles or v.mileage_num() == float("inf")]

    # Propagate enriched specs across duplicate vehicles (same car on different sites)
    _propagate_enriched_descriptions(filtered)

    # Sort by price
    filtered.sort(key=lambda v: v.price_num())

    # Output
    print_results(filtered)

    if args.csv:
        write_csv(filtered, args.csv)

    print(f"\nTotal: {len(filtered)} vehicles")
    if len(filtered) < len(all_vehicles):
        print(f"({len(all_vehicles)} before filters)")


if __name__ == "__main__":
    main()
