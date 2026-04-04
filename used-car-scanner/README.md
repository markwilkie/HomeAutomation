# Used Car Scanner

Automated used-vehicle finder for the Lake Forest Park, WA (98155) area. Scrapes 30+ dealer websites, Craigslist, Facebook Marketplace, AutoTrader, Cars.com, and CarGurus, then enriches, filters, and publishes interactive HTML reports to an Azure static site.

**Live site:** https://usedcarscannersite.z5.web.core.windows.net/

---

## Table of Contents

- [Active Searches](#active-searches)
- [Quick Start](#quick-start)
- [Scheduling](#scheduling)
- [Pipeline](#pipeline)
- [Supported Platforms & Scrapers](#supported-platforms--scrapers)
- [Configuration](#configuration)
- [Filtering & LLM Classification](#filtering--llm-classification)
- [HTML Dashboard Features](#html-dashboard-features)
- [File Reference](#file-reference)
- [Output Files](#output-files)
- [Logging](#logging)
- [Azure Infrastructure](#azure-infrastructure)
- [External APIs](#external-apis)
- [Architecture & Design Notes](#architecture--design-notes)
- [Troubleshooting](#troubleshooting)

---

## Active Searches

| Search   | Make/Model      | Years     | Price          | Max Miles |
|----------|-----------------|-----------|----------------|-----------|
| Ranger   | Ford Ranger     | ≤ 2020    | $3,500–$14,100 | 165k      |
| Frontier | Nissan Frontier | 2009–2020 | $5,000–$16,100 | 165k      |
| Tacoma   | Toyota Tacoma   | 2009–2020 | $7,000–$14,100 | 165k      |

Searches are defined in `searches.json`. Each includes pre-reject rules (e.g. reject 2WD, salvage titles) and an optional LLM classification prompt.

---

## Quick Start

### Prerequisites

| Requirement | Notes |
|-------------|-------|
| Python 3.10+ | `py -3` on Windows |
| PowerShell 7+ | `pwsh` (the orchestrator is a `.ps1` script) |
| Azure CLI (`az`) | For publishing HTML to Azure Static Sites |
| Playwright Chromium | `py -3 -m playwright install chromium` |

### Install dependencies

```powershell
py -3 -m pip install -r requirements.txt
```

Dependencies: `requests`, `beautifulsoup4`, `lxml`, `tabulate`, `playwright`.

### One-time setup

```powershell
# Log into Facebook — opens a visible browser, saves session to .browser-profile/
py -3 scan.py --fb-setup

# Solve CarGurus DataDome CAPTCHA — saves cookies to .cg_profile/
py -3 scan.py --cg-setup
```

These sessions are saved to persistent Chromium profile directories and reused on subsequent scans. Re-run if cookies expire.

### Run a scan

```powershell
# Scan all searches (Ranger, Frontier, Tacoma), open results in Chrome
.\scan.ps1

# Scan only one search
.\scan.ps1 -Only Frontier

# Scan specific platforms only (comma-separated)
.\scan.ps1 -Only Ranger -Platforms "craigslist,facebook"
```

When `-Platforms` is used, partial results are merged into the full CSV — rows from non-scanned platforms are preserved.

---

## Scheduling

```powershell
.\scan.ps1 -Schedule
```

Runs scans on a loop at the times listed in `schedule.json`. While waiting between scans, an interactive CLI is available:

| Command         | Description                                 |
|-----------------|---------------------------------------------|
| `scan`          | Run a full ad-hoc scan now                  |
| `scan Frontier` | Scan a single search                        |
| `status`        | Show schedule config, next run, last log    |
| `next`          | Skip wait, run next scheduled scan now      |
| `quit`          | Stop the scheduler                          |
| `help`          | Show available commands                     |

**Overlap handling:** If a scan takes longer than the gap between two scheduled times, the overlapping run is skipped and a message is logged.

**Live config:** `schedule.json` is re-read each cycle, so you can edit run times without restarting.

---

## Pipeline

Each scan runs the following steps per search. Every step exits non-zero on failure, halting the pipeline.

```
 ┌─────────────────────────────────────────────────────────────────┐
 │ scan.py               Scrape all dealer & platform listings     │
 │                       → output/{prefix}_results.csv             │
 ├─────────────────────────────────────────────────────────────────┤
 │ enrich_results.py     Add driving distance from 98155 via OSRM  │
 │                       → output/{prefix}_results_by_distance.csv │
 ├─────────────────────────────────────────────────────────────────┤
 │ enrich_dealers.py     Scrape CL listings for seller name/type,  │
 │                       assign dealer rating (1–10)               │
 ├─────────────────────────────────────────────────────────────────┤
 │ match_at_dealers.py   Cross-reference AutoTrader listings to     │
 │                       actual dealers by price+year+mileage       │
 ├─────────────────────────────────────────────────────────────────┤
 │ enrich_dealers.py     Re-run after AT matching to rate newly     │
 │                       identified dealers                         │
 ├─────────────────────────────────────────────────────────────────┤
 │ enrich_vin.py         Decode VINs via NHTSA vPIC API             │
 │                       (drivetrain, body, engine, transmission)    │
 ├─────────────────────────────────────────────────────────────────┤
 │ filter_llm.py         Regex pre-reject rules + optional GPT-4    │
 │                       classification via Azure OpenAI             │
 ├─────────────────────────────────────────────────────────────────┤
 │ rebuild_html.py       Generate interactive HTML dashboard         │
 │                       → output/{prefix}_results.html              │
 └─────────────────────────────────────────────────────────────────┘

After each search completes:
 ┌─────────────────────────────────────────────────────────────────┐
 │ rebuild_combined.py   Merge all searches → combined_results.html │
 │ az storage blob       Upload all *.html to Azure static site     │
 └─────────────────────────────────────────────────────────────────┘
```

---

## Supported Platforms & Scrapers

### Dealer Website Platforms

Each dealer in `dealers.json` declares a `platform` that determines which scraper function handles it.

| Platform | Scraper Function | Sites | Technique |
|----------|-----------------|-------|-----------|
| `dealer_eprocess` | `scrape_dealer_eprocess()` | Carter Subaru, Rodland Toyota, Younker Nissan, Burien Nissan, Bellevue Nissan | JSON-LD extraction from inventory pages; Playwright for paginated filter navigation |
| `dealeron` | `scrape_dealeron()` | Toyota of Lake City | DealerOn Cosmos SPA — intercepts XHR API responses via Playwright |
| `team_velocity` | `scrape_team_velocity()` | Honda of Seattle, Campbell Nissan | Apollo platform — JSON-LD with HTML card fallback |
| `dealer_com` | `scrape_dealer_com()` | Lee Johnson Chevrolet, Sound Ford, Nissan of Everett | Dealer.com / Trader Interactive — HTML card parsing |
| `dealer_inspire` | `scrape_dealer_inspire()` | Rairdon Auto Group, Advantage Nissan, Kendall Ford | Dealer Inspire — Algolia search API |
| `automanager` | `scrape_automanager()` | Seattle Lux Auto | AutoManager inventory pages |
| `foxdealer` | `scrape_foxdealer()` | Various | FoxDealer — Elasticsearch API with `foxdealer_id` |
| `carmax` | `scrape_carmax()` | CarMax | JSON REST API |
| `motive` | `scrape_motive()` | Lee Johnson Nissan of Kirkland | RideMotive SPA — API intercept |

### Aggregator / Marketplace Platforms

| Platform | Scraper Function | Technique |
|----------|-----------------|-----------|
| `craigslist` | `scrape_craigslist()` | Standard HTTP scraping; deduplicates by URL and by year+make+model+price+mileage (catches CL reposts) |
| `facebook` | `scrape_facebook()` | Persistent Playwright browser with saved login session; auto-scroll for pagination |
| `autotrader` | `scrape_autotrader()` | Playwright with bot-detection bypass (navigator.webdriver spoofing, fake plugins) |
| `cargurus` | `scrape_cargurus()` | Playwright with persistent DataDome cookies in `.cg_profile/` |
| `carscom` | `scrape_carscom()` | Standard HTTP with HTML parsing |

### Data Extraction Strategy

The scraper uses a layered extraction approach for each site:

1. **JSON-LD** (`<script type="application/ld+json">`) — preferred, gives structured schema.org/Vehicle data
2. **API interception** — for SPA sites, intercept XHR/Fetch responses via Playwright
3. **HTML card parsing** — CSS selector patterns for `.vehicle-card`, `.listing-row`, etc.
4. **Detail page enrichment** — after scraping the listing page, visit each vehicle's detail URL to extract:
   - Full specs (drivetrain, body type, engine, transmission, doors, fuel, title status)
   - VIN (regex for 17-character alphanumeric patterns)
   - High-res image URL

---

## Configuration

### searches.json

Defines each vehicle search. Key fields:

```json
{
  "Frontier": {
    "make": "Nissan",
    "model": "Frontier",
    "facebook_query": "nissan frontier 4x4",
    "min_year": 2009,
    "max_year": 2020,
    "min_price": 5000,
    "max_price": 16100,
    "max_miles": 165000,
    "title": "Nissan Frontier",
    "pre_reject": {
      "rwd": {
        "reject": "4x2|2WD|RWD",
        "override": "4x4|4WD|AWD"
      },
      "title": {
        "reject": "salvage|rebuilt|flood"
      }
    },
    "prompt": ""
  }
}
```

| Field | Description |
|-------|-------------|
| `make`, `model` | Vehicle make/model — used to build platform search URLs and filter results |
| `facebook_query` | Custom search terms for Facebook Marketplace (e.g. "nissan frontier 4x4") |
| `min_year`, `max_year` | Year range filter |
| `min_price`, `max_price` | Price range filter (passed to platforms that support it) |
| `max_miles` | Maximum mileage |
| `title` | Display name in HTML reports |
| `pre_reject` | Regex-based rejection rules (see [Filtering](#filtering--llm-classification)) |
| `prompt` | Natural-language prompt for GPT-4 classification (empty = skip LLM step) |

### schedule.json

```json
{
  "times": ["03:00", "09:00", "14:00", "18:00"]
}
```

Times are in 24-hour local time. Editable while the scheduler is running — re-read each cycle.

### dealers.json

Array of dealer configurations. Each entry describes one dealer or aggregator:

```json
{
  "name": "Carter Subaru Shoreline",
  "address": "17225 Aurora Ave N, Shoreline, WA 98133",
  "phone": "(206) 970-3537",
  "distance_miles": 2,
  "platform": "dealer_eprocess",
  "inventory_url": "https://www.cartersubarushoreline.com/search/used-seattle-wa/?cy=98133&tp=used",
  "base_url": "https://www.cartersubarushoreline.com",
  "needs_browser": true
}
```

| Field | Description |
|-------|-------------|
| `name` | Dealer display name |
| `address` | Physical address |
| `phone` | Contact number |
| `distance_miles` | Approximate distance from home (for sorting) |
| `platform` | Which scraper function to use (see [Supported Platforms](#supported-platforms--scrapers)) |
| `inventory_url` | URL to the used car search/inventory page |
| `base_url` | Root domain for resolving relative URLs |
| `needs_browser` | `true` = use Playwright; `false` = standard HTTP requests |
| `foxdealer_id` | *(optional)* ID for FoxDealer Elasticsearch API |

### Environment Variables

Set in `scan.ps1` before pipeline runs:

| Variable | Purpose |
|----------|---------|
| `AZURE_OPENAI_ENDPOINT` | Azure OpenAI service URL |
| `AZURE_OPENAI_API_KEY` | API key for GPT-4 classification |
| `AZURE_OPENAI_DEPLOYMENT` | Model deployment name (default: `gpt-4.1`) |

---

## Filtering & LLM Classification

`filter_llm.py` applies two phases of filtering:

### Phase 1: Regex Pre-Reject Rules

Defined in `searches.json` under `pre_reject`. Each rule group has:

- **`reject`** — regex pattern. If it matches any field (description, trim, vin_data), the listing is rejected.
- **`override`** — regex pattern. If it also matches, the rejection is cancelled (listing kept).

Example: reject all 2WD trucks unless the listing also mentions 4WD:
```json
"rwd": {
  "reject": "4x2|2WD|RWD|rear-wheel drive",
  "override": "4x4|4WD|AWD|four-wheel drive"
}
```

### Phase 2: LLM Classification (Optional)

If `prompt` is non-empty in `searches.json`, remaining listings are sent to Azure OpenAI GPT-4 as a JSON batch:

```json
[
  {"id": 0, "year": "2015", "make": "Nissan", "model": "Frontier", "trim": "SV",
   "description": "...", "vin_data": "Drive: 4WD | Body: Pickup | ...",
   "price": "$12,500", "mileage": "95,000 mi"}
]
```

GPT-4 returns keep/drop decisions with reasons:
```json
[{"id": 0, "keep": true, "reason": "4WD king cab, good condition"}]
```

If no `prompt` and no `pre_reject` rules are configured, the filter step is skipped entirely.

---

## HTML Dashboard Features

The generated HTML reports (`{prefix}_results.html` and `combined_results.html`) are fully interactive single-file dashboards.

### Sorting & Filtering

- Click any column header to sort (ascending/descending toggle)
- Client-side filter controls:
  - Max distance (miles)
  - Price range (min/max)
  - Minimum dealer rating
  - Source filter (Craigslist, AutoTrader, Cars.com, CarGurus, Facebook, Dealer)
  - Search filter (combined page only — dropdown per vehicle search)
  - Show only: new listings, followed listings
  - Hide rejected listings

### Listing Display

- **Thumbnail images** — click to enlarge
- **Source badges** — color-coded pills (CL, AT, Cars.com, CG, FB, Dealer)
- **NEW badge** — pulsing green badge for listings not in previous scan
- **GONE badge** — semi-transparent row for listings that disappeared since last scan
- **VIN data** — decoded specs (drivetrain, body, engine, cylinders, fuel, transmission)
- **Dealer rating** — color-coded 1–10 pill (7+: green, 6: cyan, 5: yellow, 4: red, 3: gray)
- **Distance** — driving miles from 98155

### Follow-Up Tracking

Each listing has a star (★) button that opens a follow-up panel:

- **Phone number** — free-text field
- **Email** — free-text field
- **Last contacted** — date field
- **Notes** — free-text notes
- **Reject (☒)** — hides the listing from default view

State is saved to browser `localStorage` and synced to Azure Blob Storage (`state/car_followups.json`), enabling cross-browser and cross-device access.

### Mobile Responsive

On small screens, the table switches to a card-based layout — each listing is a stacked card with all fields visible, no horizontal scrolling.

---

## File Reference

| File | Purpose |
|------|---------|
| **Orchestration** | |
| `scan.ps1` | Main orchestrator — runs the full pipeline, scheduling, interactive CLI, Azure publish |
| **Scraping** | |
| `scan.py` | Core scraper engine — queries all dealers and platforms, extracts vehicle listings |
| `_check_platforms.py` | Diagnostic tool — probes new dealer sites for scraping patterns (JSON-LD, JS vars, HTML classes) |
| **Enrichment** | |
| `enrich_results.py` | Adds driving distance from 98155 via OSRM routing + Nominatim geocoding |
| `enrich_dealers.py` | Scrapes Craigslist listings for seller name/type, assigns dealer ratings (1–10) |
| `enrich_vin.py` | Decodes VINs via NHTSA vPIC API (drivetrain, body, engine, transmission) |
| `match_at_dealers.py` | Maps AutoTrader listings to actual dealers by matching price+year+mileage against other sources |
| **Filtering** | |
| `filter_llm.py` | Two-phase filter: regex pre-reject rules + optional Azure OpenAI GPT-4 classification |
| **Reporting** | |
| `rebuild_html.py` | Generates per-search interactive HTML dashboard with follow-up tracking |
| `rebuild_combined.py` | Merges all searches into a single combined HTML dashboard |
| `show_results.py` | Simple CLI table printer for debugging (prints CSV to stdout) |
| **Configuration** | |
| `searches.json` | Vehicle search definitions (make, model, year/price/miles ranges, rejection rules) |
| `schedule.json` | Daily scan schedule times (24-hour format) |
| `dealers.json` | Dealer inventory URLs, platforms, and metadata (~30 entries) |
| **State & Cache** | |
| `distance_cache.json` | Cached geocode/distance results — auto-generated, avoids repeat API calls |
| `migrate_state.py` | One-time tool to migrate follow-up state from browser localStorage to Azure blob |

---

## Output Files

All results go to `output/`. Per-search files use the search name as prefix:

| File Pattern | Description |
|-------------|-------------|
| `{prefix}_results.csv` | Raw scan results from all platforms |
| `{prefix}_results_by_distance.csv` | Enriched: distance, location, dealer rating, VIN data, filtered |
| `{prefix}_results.html` | Interactive HTML dashboard for this search |
| `{prefix}_previous_results.csv` | Snapshot of last scan's enriched CSV (for detecting removed listings) |
| `{prefix}_previous_urls.txt` | All URLs seen in previous scans (for detecting new listings) |
| `combined_results.html` | All searches merged into one dashboard |
| `index.html` | Landing page for the Azure static site |

### CSV Columns

| Column | Source | Description |
|--------|--------|-------------|
| `dealer` | scan.py, enrich_dealers.py | Dealer or seller name |
| `year` | scan.py | Model year |
| `make` | scan.py | Vehicle make |
| `model` | scan.py | Vehicle model |
| `trim` | scan.py | Trim level |
| `price` | scan.py | Formatted price (e.g. "$12,500") |
| `mileage` | scan.py | Formatted mileage (e.g. "95,000 mi") |
| `url` | scan.py | Link to original listing |
| `distance_mi` | enrich_results.py | Driving distance from 98155 in miles |
| `description` | scan.py | Specs extracted from detail page |
| `vin` | scan.py, enrich_vin.py | 17-character VIN |
| `vin_data` | enrich_vin.py | Decoded VIN specs (drive, body, engine, fuel, etc.) |
| `image_url` | scan.py | Thumbnail image URL |
| `seller_type` | enrich_dealers.py | "dealer" or "owner" (Craigslist) |
| `dealer_rating` | enrich_dealers.py | 1–10 rating |

---

## Logging

Scan logs are written to `logs/scan_YYYY-MM-DD_HHmmss.log`. Each log captures:

- All Python script stdout/stderr
- Pipeline step start/stop markers
- Error messages and exit codes
- Publish confirmation

Logs older than 30 days are auto-pruned after each scan.

---

## Azure Infrastructure

| Resource | Details |
|----------|---------|
| **Storage account** | `usedcarscannersite` (westus2) |
| **Static site** | `$web` container → https://usedcarscannersite.z5.web.core.windows.net/ |
| **State blob** | `state/car_followups.json` — cross-browser follow-up data (stars, rejects, notes, contact info) |
| **CORS** | Configured for the static site origin |
| **Publishing** | `az storage blob upload` per HTML file (excludes `fb_*.html` debug files) |

Follow-up state sync works by:
1. On page load, the dashboard fetches `car_followups.json` from Azure Blob via SAS URL
2. Merges remote state with localStorage (remote + local union, local wins for richer entries)
3. On any edit (star, reject, note), saves to both localStorage and Azure Blob

---

## External APIs

| Service | Purpose | Auth | Rate Limit |
|---------|---------|------|------------|
| OSRM (router.project-osrm.org) | Driving distance calculation | None (free) | ~1 req/sec |
| Nominatim (nominatim.openstreetmap.org) | Geocoding addresses → lat/lon | None (free) | 1 req/sec (TOS) |
| NHTSA vPIC (vpic.nhtsa.dot.gov) | VIN decoding → drivetrain, body, engine | None (free) | 0.3s delay |
| Azure OpenAI (GPT-4.1) | Optional LLM vehicle classification | API key (env var) | Per-deployment quota |
| Facebook Marketplace | Listing scraper via Playwright | Browser login (persistent profile) | Auto-scroll pacing |
| CarGurus | Listing scraper via Playwright | DataDome cookies (persistent profile) | Standard browsing pace |

---

## Architecture & Design Notes

### Browser Automation

Three tiers of HTTP fetching, chosen per-dealer via `needs_browser` and platform type:

| Method | When Used | How |
|--------|-----------|-----|
| `fetch()` | Simple sites, APIs | `requests.Session` with spoofed User-Agent |
| `fetch_with_browser()` | Bot-protected sites | Headless Playwright Chromium |
| `fetch_with_headed_browser()` | Aggressive bot detection | Visible Chromium positioned off-screen, with `navigator.webdriver` spoofing and fake plugin injection |
| `fetch_with_persistent_browser()` | Facebook, CarGurus | Visible Chromium with saved profile directory for login/cookie persistence |

### Deduplication

Listings are deduplicated at two levels:
1. **By URL** — catches pagination repeats and same listing on multiple pages
2. **By composite key** (`year|make|model|price|mileage`) — catches Craigslist reposts and duplicate listings across platforms

### Caching

- **Distance cache** (`distance_cache.json`) — geocode and driving distance results, keyed by "City, ST". Prevents hammering Nominatim/OSRM on repeated scans.
- **VIN dedup** — within a single run, same VIN is only decoded once via NHTSA.
- **Previous URLs** (`{prefix}_previous_urls.txt`) — cumulative set of all URLs ever seen, used for new-listing detection.

### Partial Platform Scans

When `-Platforms` is passed to `scan.ps1`:
1. Scan runs only the specified platforms → writes `{prefix}_partial_results.csv`
2. Merge step reads the existing full CSV, keeps rows from non-scanned platforms
3. Combines kept rows + new partial results → writes merged full CSV
4. Rest of pipeline runs normally on the merged data

### Dealer Rating System

`enrich_dealers.py` assigns a 1–10 rating based on seller identity:

| Rating | Seller Type |
|--------|------------|
| 7–10 | Named franchise dealer (Toyota, Ford, Honda, etc.) |
| 6 | Large "Auto Group" or automotive dealer |
| 5 | Named Craigslist dealer |
| 4 | Anonymous dealer or unknown seller |
| 3 | Private seller (least accountability) |

Known dealers have hardcoded overrides in a `KNOWN_RATINGS` dict.

### AutoTrader Cross-Reference

AutoTrader listings don't always show the actual dealer name. `match_at_dealers.py` builds an index of all non-AutoTrader listings keyed by `(year, price, mileage)` and matches AutoTrader rows against it to resolve the real dealer. Falls back to `(year, price)` if exact match fails.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Facebook scrape returns 0 results | Re-run `py -3 scan.py --fb-setup` to refresh login |
| CarGurus blocked by CAPTCHA | Re-run `py -3 scan.py --cg-setup` to solve DataDome |
| "Playwright browsers not installed" | Run `py -3 -m playwright install chromium` |
| Distance shows as empty | Check `distance_cache.json` for stale entries; OSRM may be temporarily down |
| LLM filter not running | Verify `AZURE_OPENAI_ENDPOINT` and `AZURE_OPENAI_API_KEY` env vars are set |
| Pipeline fails mid-way | Check `logs/` for the latest `.log` file — each step logs stdout/stderr |
| HTML not updating on Azure | Verify `az login` session is valid; check `az storage blob upload` output in log |
| 3000+ files showing in Git | Browser profiles (`.browser-profile/`, `.cg_profile/`, `.fb_profile/`) should be in `.gitignore` |
