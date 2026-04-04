"""
Rebuild {prefix}_results.html from CSV, marking new listings with isNew flag.
Compares current URLs against {prefix}_previous_urls.txt baseline.
"""
import csv
import datetime
import json
import os
import re
import sys

prefix = sys.argv[1] if len(sys.argv) > 1 else "frontier"
OUT = "output"
now = datetime.datetime.now()
today = now.strftime("%b %#d, %Y %#I:%M %p") if os.name == "nt" else now.strftime("%b %-d, %Y %-I:%M %p")

# Load search config for title
search_title = None
search_info_line = None
try:
    with open("searches.json", encoding="utf-8") as f:
        searches = json.load(f)
    if prefix in searches:
        s = searches[prefix]
        search_title = s.get("title", f"{s['make']} {s['model']}")
        min_yr = s.get("min_year", 0)
        max_yr = s.get("max_year", "")
        if min_yr and min_yr > 1900:
            year_range = f"{min_yr}\u2013{max_yr}"
        else:
            year_range = f"\u2264{max_yr}"
        price_max = f"${s.get('max_price',0):,}"
        miles_max = s.get("max_miles", 0)
        miles_str = f"{miles_max // 1000}k" if miles_max >= 1000 else str(miles_max)
        search_subtitle = f"{search_title} \u2014 Used ({year_range}, under {price_max})"
        search_info_line = f"Scanned {today} &nbsp;|&nbsp; Lake Forest Park, WA 98155 &nbsp;|&nbsp; &le;{price_max} &nbsp;|&nbsp; &le;{miles_str} mi"
except Exception:
    pass

# Load previous URLs
prev_urls = set()
try:
    with open(f"{OUT}/{prefix}_previous_urls.txt", encoding="utf-8") as f:
        for line in f:
            prev_urls.add(line.strip())
except FileNotFoundError:
    pass
print(f"Previous URLs: {len(prev_urls)}")

# Load current data
rows = []
with open(f"{OUT}/{prefix}_results_by_distance.csv", encoding="utf-8") as f:
    for r in csv.DictReader(f):
        url = r["url"]
        if "craigslist.org" in url:
            source = "craigslist"
        elif "autotrader.com" in url:
            source = "autotrader"
        elif "cars.com" in url:
            source = "cars.com"
        elif "cargurus.com" in url:
            source = "cargurus"
        elif "facebook.com" in url:
            source = "facebook"
        else:
            source = "dealer"

        price_str = r["price"].replace("$", "").replace(",", "")
        price = int(price_str) if price_str.isdigit() else 0

        mi_str = r["mileage"].replace(",", "").replace(" mi", "").replace("\u2014", "0")
        mi = int(mi_str) if mi_str.isdigit() else 0

        dist = int(r.get("distance_mi", "0") or "0")
        rating = int(r.get("dealer_rating", "0") or "0")

        is_new = url not in prev_urls

        rows.append({
            "dealer": r["dealer"],
            "seller_type": r["seller_type"],
            "year": int(r["year"]),
            "make": r["make"],
            "model": r["model"],
            "trim": r["trim"],
            "price": price,
            "mileage": mi,
            "distance": dist,
            "location": r.get("location", ""),
            "rating": rating,
            "url": url,
            "source": source,
            "isNew": is_new,
            "vin_data": r.get("vin_data", ""),
            "image_url": r.get("image_url", ""),
        })

new_count = sum(1 for r in rows if r["isNew"])
gone_urls = prev_urls - {r["url"] for r in rows}
print(f"Total: {len(rows)}, New: {new_count}, Gone: {len(gone_urls)}")

for r in rows:
    if r["isNew"]:
        print(f'  NEW: {r["year"]} {r["trim"] or "(no trim)"} ${r["price"]:,} - {r["dealer"]}')

# Load details for gone listings from previous results backup
prev_csv_path = f"{OUT}/{prefix}_previous_results.csv"
gone_rows = []
if gone_urls and os.path.exists(prev_csv_path):
    with open(prev_csv_path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["url"] in gone_urls:
                price_str = r["price"].replace("$", "").replace(",", "")
                price = int(price_str) if price_str.isdigit() else 0
                mi_str = r["mileage"].replace(",", "").replace(" mi", "").replace("\u2014", "0")
                mi = int(mi_str) if mi_str.isdigit() else 0
                dist = int(r.get("distance_mi", "0") or "0")
                rating = int(r.get("dealer_rating", "0") or "0")
                url = r["url"]
                if "craigslist.org" in url:
                    source = "craigslist"
                elif "autotrader.com" in url:
                    source = "autotrader"
                elif "cars.com" in url:
                    source = "cars.com"
                elif "cargurus.com" in url:
                    source = "cargurus"
                elif "facebook.com" in url:
                    source = "facebook"
                else:
                    source = "dealer"
                gone_rows.append({
                    "dealer": r["dealer"],
                    "seller_type": r["seller_type"],
                    "year": int(r["year"]),
                    "make": r["make"],
                    "model": r["model"],
                    "trim": r["trim"],
                    "price": price,
                    "mileage": mi,
                    "distance": dist,
                    "location": r.get("location", ""),
                    "rating": rating,
                    "url": r["url"],
                    "source": source,
                    "isNew": False,
                    "isRemoved": True,
                    "vin_data": r.get("vin_data", ""),
                    "image_url": r.get("image_url", ""),
                })

for g in gone_rows:
    print(f'  GONE: {g["year"]} {g["trim"] or "(no trim)"} ${g["price"]:,} - {g["dealer"]}')
for u in sorted(gone_urls - {g["url"] for g in gone_rows}):
    print(f"  GONE: {u}  (no previous data)")

# Append gone rows so they appear in the HTML (one-time notification)
rows.extend(gone_rows)

# Read existing HTML (seed from any existing results HTML if this is a new search)
html_path = f"{OUT}/{prefix}_results.html"
if not os.path.exists(html_path):
    import shutil
    import glob
    seeds = glob.glob(f"{OUT}/*_results.html")
    if not seeds:
        print(f"ERROR: No existing *_results.html to seed from. Run a search that has an HTML first.")
        sys.exit(1)
    seed = seeds[0]
    shutil.copy(seed, html_path)
    print(f"Seeded {html_path} from {seed}")
with open(html_path, encoding="utf-8") as f:
    html = f.read()

# Build new DATA block
lines = []
for r in rows:
    lines.append(json.dumps(r, ensure_ascii=False))
new_data = "const DATA = [\n" + ",\n".join(lines) + "\n];"

# Replace DATA block
html = re.sub(r"const DATA = \[.*?\];", new_data, html, flags=re.DOTALL)

# Add NEW badge CSS if not already present
if ".new-tag" not in html:
    html = html.replace(
        ".src-dealer { background: #d4edda; color: #155724; }",
        ".src-dealer { background: #d4edda; color: #155724; }\n"
        "  .new-tag { display: inline-block; font-size: .65rem; padding: 1px 5px; "
        "border-radius: 4px; font-weight: 700; background: #ff4444; color: #fff; "
        "margin-left: 4px; vertical-align: middle; animation: pulse 2s ease-in-out 3; }\n"
        "  @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: .5; } }\n"
        "  tr.new-row td { background: #fffde7; }\n"
        "  tr.new-row:hover td { background: #fff9c4; }"
    )

# Add NEW filter checkbox if not present
if "fNew" not in html:
    html = html.replace(
        '<label>Max price:',
        '<label><input type="checkbox" id="fNew"> New only</label>\n'
        '  <label>Max price:'
    )

# Replace entire subtitle with correct dynamic content
if search_info_line:
    html = re.sub(
        r'<p class="subtitle">.*?</p>',
        f'<p class="subtitle">{search_info_line}</p>',
        html,
        count=1,
    )

# Update the render function to handle isNew and new filter
old_source_tag = "function sourceTag(s) {"
new_source_tag = """function newTag() { return '<span class="new-tag">NEW</span>'; }
function sourceTag(s) {"""
if "function newTag" not in html:
    html = html.replace(old_source_tag, new_source_tag)

# --- Upgrade: add Facebook source to sourceTag map, dropdown, and CSS ---
if "facebook:" not in html:
    # Add FB to sourceTag JS map
    html = html.replace(
        "cargurus: ['CG','src-cg'], dealer:",
        "cargurus: ['CG','src-cg'], facebook: ['FB','src-fb'], dealer:",
    )
    # Add FB option to source dropdown
    html = html.replace(
        '<option value="dealer">Dealer site</option>',
        '<option value="facebook">Facebook</option>\n    <option value="dealer">Dealer site</option>',
    )
    # Add FB CSS rule
    html = html.replace(
        ".src-dealer",
        ".src-fb { background: #d4e0f5; color: #1a3d82; }\n  .src-dealer",
    )

# Update row rendering to include NEW tag and row class
old_row_render = "const cls = r.distance > 100 ? ' class=\"far\"' : '';"
new_row_render = """const cls = r.isNew ? ' class="new-row"' : '';"""
html = html.replace(old_row_render, new_row_render)

# Add NEW tag next to source tag in the row
old_source_cell = "'<td>' + sourceTag(r.source) + '</td>'"
new_source_cell = "'<td>' + sourceTag(r.source) + (r.isNew ? newTag() : '') + '</td>'"
html = html.replace(old_source_cell, new_source_cell)

# Add the new-only filter to the render function
old_filter = """(!src || r.source === src)"""
new_filter = """(!src || r.source === src) &&
    (!document.getElementById('fNew').checked || r.isNew)"""
if "fNew" not in html.split("function render")[1].split("function")[0] if "function render" in html else True:
    html = html.replace(old_filter, new_filter)

# Make sure the new checkbox triggers render
if "fNew" not in html.split("addEventListener")[0] if "addEventListener" in html else True:
    html = html.replace(
        "document.querySelectorAll('.controls select, .controls input').forEach(el => {",
        "document.querySelectorAll('.controls select, .controls input, .controls input[type=checkbox]').forEach(el => {\n"
        "  // includes the New Only checkbox"
    )
    # Actually the existing selector already matches inputs, so the checkbox will be picked up

# Update the shown/total count to include new count
old_count = '<p class="count"><span id="shown">0</span> of <span id="total">0</span> listings shown</p>'
new_count_html = '<p class="count"><span id="shown">0</span> of <span id="total">0</span> listings shown (<span id="newCount">0</span> new)</p>'
html = html.replace(old_count, new_count_html)

# Update render to set new count (only if not already present)
if "const newShown" not in html:
    old_total_set = "document.getElementById('total').textContent = DATA.length;"
    new_total_set = """document.getElementById('total').textContent = DATA.length;
  const newShown = rows.filter(r => r.isNew).length;
  const newTotal = DATA.filter(r => r.isNew).length;
  document.getElementById('newCount').textContent = newShown + ' of ' + newTotal;"""
    html = html.replace(old_total_set, new_total_set)

# Update page title and heading from searches.json
if search_title:
    html = re.sub(r"<title>.*?</title>", f"<title>{search_title} Search Results</title>", html)
    html = re.sub(r"<h1>.*?</h1>", f"<h1>{search_subtitle}</h1>", html)

# Add VIN Data column if not already present
if "VIN Data" not in html:
    # Add column header before Source
    html = html.replace(
        '<th>Source</th>',
        '<th>VIN Data <span class="arrow">▲</span></th>\n  <th>Source</th>',
    )
    # Add cell before source cell in render
    html = html.replace(
        "'<td>' + sourceTag(r.source)",
        "'<td class=\"vin-data\">' + (r.vin_data || '') + '</td>' +\n"
        "      '<td>' + sourceTag(r.source)",
    )
    # Add CSS for vin-data cell (small, wrapping text)
    html = html.replace(
        ".src-dealer { background: #d4edda; color: #155724; }",
        ".src-dealer { background: #d4edda; color: #155724; }\n"
        "  .vin-data { font-size: .75rem; max-width: 180px; word-wrap: break-word; color: #555; }",
    )

# Add photo thumbnail column
if "car-thumb" not in html:
    # CSS
    html = html.replace(
        ".src-dealer { background: #d4edda; color: #155724; }",
        ".src-dealer { background: #d4edda; color: #155724; }\n"
        "  .car-thumb { width: 80px; height: 60px; object-fit: cover; border-radius: 4px; cursor: pointer; }\n"
        "  .car-thumb:hover { transform: scale(2.5); position: relative; z-index: 10; box-shadow: 0 4px 12px rgba(0,0,0,.3); }\n"
        "  td.thumb-cell { padding: 2px 4px; width: 88px; text-align: center; }",
    )
    # Column header -- add after the star column (☆), before Year
    html = html.replace(
        '<th data-col="year"',
        '<th>Photo</th>\n  <th data-col="year"',
    )
    # Cell -- add after star cell, before year cell in row render
    html = html.replace(
        "      '<td>' + r.year + '</td>'",
        "      '<td class=\"thumb-cell\">' + (r.image_url ? '<img class=\"car-thumb\" src=\"' + r.image_url + '\" loading=\"lazy\" onerror=\"this.style.display=\\'none\\'\">' : '') + '</td>' +\n"
        "      '<td>' + r.year + '</td>'",
    )

# Add REMOVED badge CSS if not already present
if ".removed-tag" not in html:
    html = html.replace(
        ".new-tag {",
        ".removed-tag { display: inline-block; font-size: .65rem; padding: 1px 5px; "
        "border-radius: 4px; font-weight: 700; background: #888; color: #fff; "
        "margin-left: 4px; vertical-align: middle; }\n"
        "  tr.removed-row td { background: #f5f5f5; color: #999; text-decoration: line-through; }\n"
        "  tr.removed-row td a { color: #999; }\n"
        "  tr.removed-row td .source-tag, tr.removed-row td .rating-pill { opacity: .5; }\n"
        "  .new-tag {",
    )

# Update row class logic to handle isRemoved
old_cls = """const cls = r.isNew ? ' class="new-row"' : '';"""
new_cls = """const cls = r.isRemoved ? ' class="removed-row"' : r.isNew ? ' class="new-row"' : '';"""
html = html.replace(old_cls, new_cls)

# Add REMOVED tag next to source tag (alongside existing NEW tag)
old_tags = "(r.isNew ? newTag() : '')"
new_tags = "(r.isRemoved ? removedTag() : r.isNew ? newTag() : '')"
html = html.replace(old_tags, new_tags)

# Add removedTag function if not present
if "function removedTag" not in html:
    html = html.replace(
        "function newTag() {",
        "function removedTag() { return '<span class=\"removed-tag\">REMOVED</span>'; }\n"
        "function newTag() {",
    )

# Update counts to include removed
gone_count = len(gone_rows)
if "removedCount" not in html:
    html = html.replace(
        '(<span id="newCount">0</span> new)</p>',
        '(<span id="newCount">0</span> new, <span id="removedCount">0</span> removed)</p>',
    )
    old_new_count_js = "document.getElementById('newCount').textContent = newShown + ' of ' + newTotal;"
    new_count_js = (
        "document.getElementById('newCount').textContent = newShown + ' of ' + newTotal;\n"
        "  const removedCount = DATA.filter(r => r.isRemoved).length;\n"
        "  document.getElementById('removedCount').textContent = removedCount;"
    )
    html = html.replace(old_new_count_js, new_count_js)

# ── Follow-up tracking (star, contact info, notes via localStorage) ──

# CSS for follow-up panel
if ".follow-star" not in html:
    followup_css = (
        "  .follow-star { cursor: pointer; font-size: 1.1rem; user-select: none; text-align: center; width: 30px; }\n"
        "  .follow-star:hover { transform: scale(1.2); }\n"
        "  tr.followed > td:first-child { background: #fffde7; }\n"
        "  .followup-row td { padding: 0 !important; border-bottom: 2px solid #1a3a5c; background: #fafbfc; }\n"
        "  .followup-panel { display: flex; flex-wrap: wrap; gap: 10px; padding: 8px 12px; font-size: .82rem; align-items: flex-end; }\n"
        "  .followup-panel label { display: flex; flex-direction: column; gap: 2px; color: #555; font-size: .75rem; }\n"
        "  .followup-panel input, .followup-panel textarea { padding: 4px 6px; border: 1px solid #ccc; border-radius: 4px; font-size: .82rem; font-family: inherit; }\n"
        "  .followup-panel input { width: 170px; }\n"
        "  .followup-panel textarea { width: 300px; height: 42px; resize: vertical; }\n"
        "  .followup-panel .fu-save { padding: 4px 12px; background: #1a3a5c; color: #fff; border: none; border-radius: 4px; cursor: pointer; font-size: .78rem; align-self: flex-end; }\n"
        "  .followup-panel .fu-save:hover { background: #24507a; }\n"
        "  .followed-tag { display: inline-block; font-size: .65rem; padding: 1px 5px; border-radius: 4px; font-weight: 700; background: #f0ad4e; color: #fff; margin-left: 4px; vertical-align: middle; }\n"
    )
    html = html.replace("  .removed-tag {", followup_css + "  .removed-tag {")

# Reject CSS (independent guard)
if ".rejected-tag" not in html:
    reject_css = (
        "  .rejected-tag { display: inline-block; font-size: .65rem; padding: 1px 5px; border-radius: 4px; font-weight: 700; background: #d9534f; color: #fff; margin-left: 4px; vertical-align: middle; }\n"
        "  tr.rejected-row td { background: #fdf2f2; opacity: .45; text-decoration: line-through; }\n"
        "  tr.rejected-row:hover td { opacity: .7; }\n"
    )
    html = html.replace("  .removed-tag {", reject_css + "  .removed-tag {")

# Column header: star column before Year
if "follow-star" not in html.split("<thead>")[1].split("</thead>")[0]:
    html = html.replace(
        '<th data-col="year"',
        '<th class="follow-star" title="Follow up">\u2606</th>\n  <th data-col="year"',
    )

# Followed-only filter checkbox
if "fFollowed" not in html:
    html = html.replace(
        '<label><input type="checkbox" id="fNew"> New only</label>',
        '<label><input type="checkbox" id="fNew"> New only</label>\n'
        '  <label><input type="checkbox" id="fFollowed"> Followed only</label>',
    )

# Hide rejected checkbox (unchecked by default)
if "fHideRej" not in html:
    html = html.replace(
        '<label><input type="checkbox" id="fFollowed"> Followed only</label>',
        '<label><input type="checkbox" id="fFollowed"> Followed only</label>\n'
        '  <label><input type="checkbox" id="fHideRej" checked> Hide rejected</label>',
    )
# Upgrade: ensure fHideRej is checked by default
html = html.replace('id="fHideRej">', 'id="fHideRej" checked>')

# followedTag and rejectedTag helper functions
if "function followedTag" not in html:
    html = html.replace(
        "function removedTag() {",
        "function followedTag() { return '<span class=\"followed-tag\">FOLLOWING</span>'; }\n"
        "function removedTag() {",
    )
if "function rejectedTag" not in html:
    html = html.replace(
        "function removedTag() {",
        "function rejectedTag() { return '<span class=\"rejected-tag\">REJECTED</span>'; }\n"
        "function removedTag() {",
    )

# Add FOLLOWING tag after REMOVED/NEW tags in source cell
if "isFollowed(r.url) ? followedTag()" not in html:
    html = html.replace(
        "(r.isRemoved ? removedTag() : r.isNew ? newTag() : '')",
        "(r.isRemoved ? removedTag() : r.isNew ? newTag() : '') + (isFollowed(r.url) ? followedTag() : '')",
    )
# Add REJECTED tag (upgrade existing tag expression)
if "isRejected(r.url) ? rejectedTag()" not in html:
    html = html.replace(
        "(isFollowed(r.url) ? followedTag() : '')",
        "(isRejected(r.url) ? rejectedTag() : isFollowed(r.url) ? followedTag() : '')",
    )

# Update row class to include 'followed' class
# Update row class to include 'followed' and 'rejected' classes
if "isFollowed" not in html.split("const cls")[1].split(";")[0] if "const cls" in html else True:
    html = html.replace(
        """const cls = r.isRemoved ? ' class="removed-row"' : r.isNew ? ' class="new-row"' : '';""",
        """const fu = isFollowed(r.url); const rej = isRejected(r.url);
    const cls = r.isRemoved ? ' class="removed-row"' : rej ? ' class="rejected-row"' : r.isNew ? ' class="new-row"' : fu ? ' class="followed"' : '';""",
    )
# Upgrade existing followed-only cls to include rejected
if "isRejected" not in html.split("const cls")[1].split(";")[0] if "const cls" in html else False:
    html = html.replace(
        """const fu = isFollowed(r.url);
    const cls = r.isRemoved ? ' class="removed-row"' : r.isNew ? ' class="new-row"' : fu ? ' class="followed"' : '';""",
        """const fu = isFollowed(r.url); const rej = isRejected(r.url);
    const cls = r.isRemoved ? ' class="removed-row"' : rej ? ' class="rejected-row"' : r.isNew ? ' class="new-row"' : fu ? ' class="followed"' : '';""",
    )

# Add data-url attribute and star cell to row rendering
if "data-url" not in html:
    html = html.replace(
        """return '<tr' + cls + '>' +
      '<td>' + r.year + '</td>'""",
        "return '<tr' + cls + ' data-url=\"' + r.url.replace(/\"/g,'&quot;') + '\">' +\n"
        "      '<td class=\"follow-star\" onclick=\"togglePanel(\\'' + r.url.replace(/'/g,\"\\\\\\\\\\'\" ) + '\\', this)\">'"
        " + ((fuStore()[r.url]||{}).starred ? '\u2605' : '\u2606') + '</td>' +\n"
        "      '<td>' + r.year + '</td>'",
    )

# Add followed filter in the render function
if "fFollowed" not in html.split("function render")[1].split("}")[0]:
    html = html.replace(
        "(!document.getElementById('fNew').checked || r.isNew)\n  );",
        "(!document.getElementById('fNew').checked || r.isNew) &&\n"
        "    (!document.getElementById('fFollowed').checked || isFollowed(r.url))\n  );",
    )
# Add reject filter (independent guard)
if "fHideRej" not in html.split("function render")[1].split("}")[0]:
    html = html.replace(
        "(!document.getElementById('fFollowed').checked || isFollowed(r.url))\n  );",
        "(!document.getElementById('fFollowed').checked || isFollowed(r.url)) &&\n"
        "    (!document.getElementById('fHideRej').checked || !isRejected(r.url))\n  );",
    )

# Default sort by distance instead of price
html = html.replace("let sortCol = 'price'", "let sortCol = 'distance'")
html = html.replace('data-col="price" data-type="num" class="sorted"', 'data-col="price" data-type="num"')
if 'data-col="distance" data-type="num" class="sorted"' not in html:
    html = html.replace('data-col="distance" data-type="num"', 'data-col="distance" data-type="num" class="sorted"')

# Move initial render() call to after follow-up tracking (FU_KEY must be initialized first)
# This handles both fresh injection and existing HTMLs
if "render();\n\n// --- Follow-up tracking" in html:
    # The early render() is before FU_KEY -- remove it; the one at end-of-script suffices
    html = html.replace(
        "});\n\nrender();\n\n// --- Follow-up tracking",
        "});\n\n// --- Follow-up tracking",
    )
    # If there's no render() before </script>, add one
    if "render();\n</script>" not in html:
        html = html.replace("// --- End follow-up tracking ---\n</script>",
                            "// --- End follow-up tracking ---\nrender();\n</script>")

# Inject follow-up tracking JS (Azure Blob + localStorage cache)
followup_js_azure = """
// --- Follow-up tracking (Azure Blob + localStorage cache) ---
const FU_KEY = 'car_followups';
const STATE_URL = 'https://usedcarscannersite.blob.core.windows.net/state/car_followups.json?se=2028-03-23T17%3A04Z&sp=racwd&sv=2026-02-06&sr=b&sig=xnkKW5c%2BFglLX4AkguQft6VvWJs3whqsnj%2B4wyBYDV0%3D';
let _fuCache = {};
let _fuSaveTimer = null;
try { _fuCache = JSON.parse(localStorage.getItem(FU_KEY) || '{}'); } catch(e) {}

function fuStore() { return _fuCache; }
function fuSave(store) {
  _fuCache = store;
  localStorage.setItem(FU_KEY, JSON.stringify(store));
  clearTimeout(_fuSaveTimer);
  _fuSaveTimer = setTimeout(_fuPush, 500);
}
function fuGet(url) { return _fuCache[url] || {}; }
function fuSet(url, data) { const s = fuStore(); s[url] = data; fuSave(s); }
function isFollowed(url) { const d = _fuCache[url]; return !!(d && (d.phone || d.email || d.lastContact || d.notes || d.starred)); }
function isRejected(url) { const d = _fuCache[url]; return !!(d && d.rejected); }

async function _fuPush() {
  try {
    await fetch(STATE_URL, {
      method: 'PUT',
      headers: {'Content-Type':'application/json','x-ms-blob-type':'BlockBlob'},
      body: JSON.stringify(_fuCache)
    });
  } catch(e) { console.warn('State sync failed:', e); }
}

(async function _fuInit() {
  const local = {..._fuCache};
  let remote = {};
  try {
    const resp = await fetch(STATE_URL + '&_t=' + Date.now());
    if (resp.ok) remote = await resp.json();
  } catch(e) {}
  const merged = {...remote};
  for (const [url, ld] of Object.entries(local)) {
    if (!merged[url]) { merged[url] = ld; continue; }
    const lf = [ld.phone,ld.email,ld.lastContact,ld.notes].filter(Boolean).length;
    const rf = [merged[url].phone,merged[url].email,merged[url].lastContact,merged[url].notes].filter(Boolean).length;
    if (lf > rf) merged[url] = {...ld};
    if (ld.starred) merged[url].starred = true;
    if (ld.rejected) merged[url].rejected = true;
  }
  _fuCache = merged;
  localStorage.setItem(FU_KEY, JSON.stringify(merged));
  if (Object.keys(local).length && JSON.stringify(remote) !== JSON.stringify(merged)) {
    await _fuPush();
  }
  render();
})();

function escAttr(s) { return (s||'').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;'); }

function togglePanel(url, btn) {
  const existing = document.querySelector('.followup-row');
  const wasForSame = existing && existing.dataset.fuUrl === url;
  if (existing) existing.remove();
  if (wasForSame) return;
  const tr = btn.closest('tr');
  if (!tr) return;
  const d = fuGet(url);
  const colSpan = tr.children.length;
  const starred = d.starred ? true : false;
  const rej = d.rejected ? true : false;
  const newTr = document.createElement('tr');
  newTr.className = 'followup-row';
  newTr.dataset.fuUrl = url;
  newTr.innerHTML = '<td colspan="' + colSpan + '"><div class="followup-panel">' +
    '<label>Phone<input type="tel" class="fu-phone" value="' + escAttr(d.phone) + '" placeholder="(206) 555-1234"></label>' +
    '<label>Email<input type="email" class="fu-email" value="' + escAttr(d.email) + '" placeholder="seller@example.com"></label>' +
    '<label>Last Contact<input type="datetime-local" class="fu-contact" value="' + (d.lastContact||'') + '"></label>' +
    '<label>Notes<textarea class="fu-notes" placeholder="e.g. Left voicemail, asking price firm...">' + escAttr(d.notes) + '</textarea></label>' +
    '<button class="fu-save">Save</button>' +
    '<button class="fu-save" style="background:' + (starred ? '#d9534f' : '#5cb85c') + '" data-fur="star">' + (starred ? '\\u2605 Unfollow' : '\\u2606 Follow') + '</button>' +
    '<button class="fu-save" style="background:' + (rej ? '#5cb85c' : '#d9534f') + '" data-fur="rej">' + (rej ? '\\u2714 Unreject' : '\\u2718 Reject') + '</button>' +
    '</div></td>';
  newTr.querySelector('.fu-save').addEventListener('click', function() { saveFollowup(url); });
  newTr.querySelector('[data-fur="star"]').addEventListener('click', function() {
    const dd = fuGet(url);
    dd.starred = !dd.starred;
    if (!dd.starred && !dd.phone && !dd.email && !dd.lastContact && !dd.notes) {
      const s = fuStore(); delete s[url]; fuSave(s);
    } else { fuSet(url, dd); }
    document.querySelector('.followup-row').remove();
    render();
  });
  newTr.querySelector('[data-fur="rej"]').addEventListener('click', function() {
    const dd = fuGet(url);
    dd.rejected = !dd.rejected;
    fuSet(url, dd);
    document.querySelector('.followup-row').remove();
    render();
  });
  tr.after(newTr);
}

function saveFollowup(url) {
  const panel = document.querySelector('.followup-row');
  if (!panel) return;
  const d = fuGet(url);
  d.phone = panel.querySelector('.fu-phone').value.trim();
  d.email = panel.querySelector('.fu-email').value.trim();
  d.lastContact = panel.querySelector('.fu-contact').value;
  d.notes = panel.querySelector('.fu-notes').value.trim();
  if (!d.starred) d.starred = true;
  fuSet(url, d);
  panel.remove();
  render();
}
// --- End follow-up tracking ---
"""

# Upgrade existing localStorage-only tracking to Azure-backed, or update SAS URL
if "// --- Follow-up tracking" in html:
    _new_js = followup_js_azure.strip()
    html = re.sub(
        r'// --- Follow-up tracking.*?// --- End follow-up tracking ---',
        lambda m: _new_js,
        html,
        flags=re.DOTALL,
    )
# Fresh injection (no tracking at all)
elif "FU_KEY" not in html:
    html = html.replace("render();\n</script>", followup_js_azure + "\nrender();\n</script>")

# ── Upgrade existing follow-up JS to add reject support ──
# These patches apply to HTMLs that already have FU_KEY but not reject features.

if "function isRejected" not in html:
    html = html.replace(
        "function isFollowed(url) {",
        "function isRejected(url) { const d = fuStore()[url]; return !!(d && d.rejected); }\n"
        "function isFollowed(url) {",
    )

if "const rej" not in html.split("function togglePanel")[1].split("function ")[0] if "function togglePanel" in html else False:
    html = html.replace(
        "  const starred = d.starred ? true : false;\n  const newTr",
        "  const starred = d.starred ? true : false;\n  const rej = d.rejected ? true : false;\n  const newTr",
    )

if 'data-fur="rej"' not in html and 'data-fur=\\"rej\\"' not in html:
    html = html.replace(
        """'</button>' +
    '</div></td>';
  newTr.querySelector('.fu-save')""",
        """'</button>' +
    '<button class="fu-save" style="background:' + (rej ? '#5cb85c' : '#d9534f') + '" data-fur="rej">' + (rej ? '\\u2714 Unreject' : '\\u2718 Reject') + '</button>' +
    '</div></td>';
  newTr.querySelector('.fu-save')""",
    )

if "data-fur" in html and '[data-fur="rej"]' not in html:
    html = html.replace(
        "  tr.after(newTr);\n}",
        "  newTr.querySelector('[data-fur=\"rej\"]').addEventListener('click', function() {\n"
        "    const dd = fuGet(url);\n"
        "    dd.rejected = !dd.rejected;\n"
        "    fuSet(url, dd);\n"
        "    document.querySelector('.followup-row').remove();\n"
        "    render();\n"
        "  });\n"
        "  tr.after(newTr);\n}",
    )

# Inject mobile-responsive card layout CSS if not already present
if "@media (max-width: 768px)" not in html:
    mobile_css = (
        "  @media (max-width: 768px) {\n"
        "    body { padding: 8px; }\n"
        "    h1 { font-size: 1.2rem; }\n"
        "    .subtitle { font-size: .78rem; }\n"
        "    .controls { gap: 6px; }\n"
        "    .controls label { font-size: .75rem; }\n"
        "    .controls select, .controls input { font-size: .78rem; padding: 4px 6px; }\n"
        "    table { box-shadow: none; background: transparent; border-radius: 0; }\n"
        "    thead { display: none; }\n"
        "    table, tbody, tr, td { display: block; width: 100%; }\n"
        "    tr { margin-bottom: 12px; border: 1px solid #ddd; border-radius: 8px; padding: 10px; background: #fff; box-shadow: 0 1px 3px rgba(0,0,0,.08); position: relative; }\n"
        "    td { padding: 3px 0; border: none; text-align: right; }\n"
        "    td::before { float: left; font-weight: 600; color: #555; }\n"
        "    tr > td:nth-child(1) { position: absolute; top: 8px; right: 8px; }\n"
        "    tr > td:nth-child(1)::before { display: none; }\n"
        "    tr > td:nth-child(2) { text-align: center; padding: 4px 0 8px; }\n"
        "    tr > td:nth-child(2)::before { display: none; }\n"
        "    tr > td:nth-child(3)::before { content: 'Year'; }\n"
        "    tr > td:nth-child(4)::before { content: 'Trim'; }\n"
        "    tr > td:nth-child(5)::before { content: 'Price'; }\n"
        "    tr > td:nth-child(6)::before { content: 'Miles'; }\n"
        "    tr > td:nth-child(7)::before { content: 'Dist'; }\n"
        "    tr > td:nth-child(8)::before { content: 'Location'; }\n"
        "    tr > td:nth-child(9)::before { content: 'Dealer'; }\n"
        "    tr > td:nth-child(10)::before { content: 'Rating'; }\n"
        "    tr > td:nth-child(11)::before { content: 'Details'; display: block; float: none; margin-bottom: 2px; }\n"
        "    tr > td:nth-child(11) { text-align: left; }\n"
        "    tr > td:nth-child(12)::before { content: 'Source'; }\n"
        "    .car-thumb { width: 100%; max-width: 280px; height: auto; max-height: 180px; }\n"
        "    .car-thumb:hover { transform: none; position: static; box-shadow: none; }\n"
        "    td.thumb-cell { width: 100%; }\n"
        "    .vin-data { max-width: 100%; }\n"
        "    .followup-row { padding: 0; }\n"
        "    .followup-row td { text-align: left; }\n"
        "    .followup-row td::before { display: none; }\n"
        "    .followup-panel { flex-direction: column; padding: 8px; }\n"
        "    .followup-panel input, .followup-panel textarea { width: 100%; }\n"
        "    .count { font-size: .75rem; }\n"
        "  }\n"
    )
    html = html.replace("</style>", mobile_css + "</style>")

# Strip the 'far' class -- all rows should render at full contrast
html = html.replace("  tr.far td { color: #999; }\n", "")
html = re.sub(r' far"', '"', html)
html = re.sub(r"""r\.distance > 100 \? ' class="(\w[\w-]*) far"' : ' class="\1"'""", r"""' class="\1"'""", html)
html = re.sub(r"""r\.distance > 100 \? ' class="far"' : ''""", "''", html)

with open(f"{OUT}/{prefix}_results.html", "w", encoding="utf-8") as f:
    f.write(html)

print(f"\nHTML rebuilt with {len(rows)} rows ({new_count} new, {gone_count} removed)")
