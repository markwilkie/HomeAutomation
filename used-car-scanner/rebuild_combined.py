"""
Build combined_results.html merging all searches into a single page.
Reads each {prefix}_results_by_distance.csv and {prefix}_previous_urls.txt.
"""
import csv
import json
import os
import sys
from datetime import datetime

OUT = "output"

# Load search configs
with open("searches.json", encoding="utf-8") as f:
    searches = json.load(f)

# Build rows from all searches
all_rows = []
total_new = 0
total_gone = 0

for prefix, cfg in searches.items():
    csv_path = f"{OUT}/{prefix}_results_by_distance.csv"
    if not os.path.exists(csv_path):
        print(f"  Skipping {prefix}: no CSV")
        continue

    search_label = cfg.get("title", f"{cfg['make']} {cfg['model']}")

    # Load previous URLs for new-detection
    prev_urls = set()
    try:
        with open(f"{OUT}/{prefix}_previous_urls.txt", encoding="utf-8") as f2:
            for line in f2:
                prev_urls.add(line.strip())
    except FileNotFoundError:
        pass

    count = 0
    new_count = 0
    with open(csv_path, encoding="utf-8") as f2:
        for r in csv.DictReader(f2):
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

            all_rows.append({
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
                "search": prefix,
            })
            count += 1
            if is_new:
                new_count += 1

    # Gone listings
    current_urls = {row["url"] for row in all_rows if row["search"] == prefix}
    gone_urls = prev_urls - current_urls
    prev_csv = f"{OUT}/{prefix}_previous_results.csv"
    if gone_urls and os.path.exists(prev_csv):
        with open(prev_csv, encoding="utf-8") as f2:
            for r in csv.DictReader(f2):
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
                    all_rows.append({
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
                        "search": prefix,
                    })
                    total_gone += 1

    print(f"  {prefix}: {count} listings ({new_count} new, {len(gone_urls)} gone)")
    total_new += new_count

# Build search filter options
search_options = "\n    ".join(
    f'<option value="{p}">{cfg.get("title", p)}</option>'
    for p, cfg in searches.items()
)

# Build DATA JSON
data_json = ",\n".join(json.dumps(r, ensure_ascii=False) for r in all_rows)

today = datetime.now().strftime("%b %#d, %Y %#I:%M %p") if __import__('os').name == 'nt' else datetime.now().strftime("%b %-d, %Y %-I:%M %p")

html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>All Searches \u2014 Combined Results</title>
<style>
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f0f2f5; color: #1a1a1a; padding: 20px; }}
  h1 {{ text-align: center; margin-bottom: 6px; font-size: 1.6rem; }}
  .subtitle {{ text-align: center; color: #666; margin-bottom: 18px; font-size: .9rem; }}
  .controls {{ display: flex; flex-wrap: wrap; gap: 10px; justify-content: center; margin-bottom: 18px; }}
  .controls select, .controls input {{ padding: 6px 10px; border: 1px solid #ccc; border-radius: 6px; font-size: .85rem; background: #fff; }}
  .controls label {{ font-size: .82rem; color: #555; display: flex; align-items: center; gap: 4px; }}
  table {{ width: 100%; border-collapse: collapse; background: #fff; border-radius: 8px; overflow: hidden; box-shadow: 0 1px 4px rgba(0,0,0,.1); font-size: .85rem; }}
  thead {{ background: #1a3a5c; color: #fff; }}
  th {{ padding: 10px 8px; text-align: left; cursor: pointer; user-select: none; white-space: nowrap; position: relative; }}
  th:hover {{ background: #24507a; }}
  th .arrow {{ font-size: .7rem; margin-left: 3px; opacity: .5; }}
  th.sorted .arrow {{ opacity: 1; }}
  td {{ padding: 8px; border-bottom: 1px solid #eee; }}
  tr:hover td {{ background: #f7faff; }}

  a {{ color: #1a6fb5; text-decoration: none; }}
  a:hover {{ text-decoration: underline; }}
  .price {{ font-weight: 600; white-space: nowrap; }}
  .mi {{ white-space: nowrap; }}
  .rating {{ text-align: center; }}
  .rating-pill {{ display: inline-block; padding: 2px 8px; border-radius: 10px; font-weight: 600; font-size: .78rem; min-width: 32px; text-align: center; }}
  .r7 {{ background: #d4edda; color: #155724; }}
  .r6 {{ background: #d1ecf1; color: #0c5460; }}
  .r5 {{ background: #fff3cd; color: #856404; }}
  .r4 {{ background: #f8d7da; color: #721c24; }}
  .r3 {{ background: #e2e3e5; color: #383d41; }}
  .dist {{ white-space: nowrap; }}
  .count {{ text-align: center; color: #666; font-size: .82rem; margin-bottom: 10px; }}
  .source-tag {{ display: inline-block; font-size: .7rem; padding: 1px 6px; border-radius: 4px; font-weight: 500; vertical-align: middle; }}
  .src-cl {{ background: #e0d4f5; color: #4a2d82; }}
  .src-at {{ background: #fce4c4; color: #8a5a00; }}
  .src-cars {{ background: #c4e0fc; color: #00458a; }}
  .src-cg {{ background: #d4f5e0; color: #1a6b3a; }}
  .src-fb {{ background: #d4e0f5; color: #1a3d82; }}
  .src-dealer {{ background: #d4edda; color: #155724; }}
  .search-tag {{ display: inline-block; font-size: .7rem; padding: 1px 6px; border-radius: 4px; font-weight: 600; vertical-align: middle; margin-right: 3px; }}
  .car-thumb {{ width: 80px; height: 60px; object-fit: cover; border-radius: 4px; cursor: pointer; }}
  .car-thumb:hover {{ transform: scale(2.5); position: relative; z-index: 10; box-shadow: 0 4px 12px rgba(0,0,0,.3); }}
  td.thumb-cell {{ padding: 2px 4px; width: 88px; text-align: center; }}
  .vin-data {{ font-size: .75rem; max-width: 180px; word-wrap: break-word; color: #555; }}
  .follow-star {{ cursor: pointer; font-size: 1.1rem; user-select: none; text-align: center; width: 30px; }}
  .follow-star:hover {{ transform: scale(1.2); }}
  tr.followed > td:first-child {{ background: #fffde7; }}
  .followup-row td {{ padding: 0 !important; border-bottom: 2px solid #1a3a5c; background: #fafbfc; }}
  .followup-panel {{ display: flex; flex-wrap: wrap; gap: 10px; padding: 8px 12px; font-size: .82rem; align-items: flex-end; }}
  .followup-panel label {{ display: flex; flex-direction: column; gap: 2px; color: #555; font-size: .75rem; }}
  .followup-panel input, .followup-panel textarea {{ padding: 4px 6px; border: 1px solid #ccc; border-radius: 4px; font-size: .82rem; font-family: inherit; }}
  .followup-panel input {{ width: 170px; }}
  .followup-panel textarea {{ width: 300px; height: 42px; resize: vertical; }}
  .followup-panel .fu-save {{ padding: 4px 12px; background: #1a3a5c; color: #fff; border: none; border-radius: 4px; cursor: pointer; font-size: .78rem; align-self: flex-end; }}
  .followup-panel .fu-save:hover {{ background: #24507a; }}
  .followed-tag {{ display: inline-block; font-size: .65rem; padding: 1px 5px; border-radius: 4px; font-weight: 700; background: #f0ad4e; color: #fff; margin-left: 4px; vertical-align: middle; }}
  .rejected-tag {{ display: inline-block; font-size: .65rem; padding: 1px 5px; border-radius: 4px; font-weight: 700; background: #d9534f; color: #fff; margin-left: 4px; vertical-align: middle; }}
  tr.rejected-row td {{ background: #fdf2f2; opacity: .45; text-decoration: line-through; }}
  tr.rejected-row:hover td {{ opacity: .7; }}
  .removed-tag {{ display: inline-block; font-size: .65rem; padding: 1px 5px; border-radius: 4px; font-weight: 700; background: #888; color: #fff; margin-left: 4px; vertical-align: middle; }}
  tr.removed-row td {{ background: #f5f5f5; color: #999; text-decoration: line-through; }}
  tr.removed-row td a {{ color: #999; }}
  tr.removed-row td .source-tag, tr.removed-row td .rating-pill {{ opacity: .5; }}
  .new-tag {{ display: inline-block; font-size: .65rem; padding: 1px 5px; border-radius: 4px; font-weight: 700; background: #ff4444; color: #fff; margin-left: 4px; vertical-align: middle; animation: pulse 2s ease-in-out 3; }}
  @keyframes pulse {{ 0%,100% {{ opacity: 1; }} 50% {{ opacity: .5; }} }}
  tr.new-row td {{ background: #fffde7; }}
  tr.new-row:hover td {{ background: #fff9c4; }}
  @media (max-width: 900px) {{
    table {{ font-size: .78rem; }}
    td, th {{ padding: 6px 4px; }}
  }}
  @media (max-width: 768px) {{
    body {{ padding: 8px; }}
    h1 {{ font-size: 1.2rem; }}
    .subtitle {{ font-size: .78rem; }}
    .controls {{ gap: 6px; }}
    .controls label {{ font-size: .75rem; }}
    .controls select, .controls input {{ font-size: .78rem; padding: 4px 6px; }}
    table {{ box-shadow: none; background: transparent; border-radius: 0; }}
    thead {{ display: none; }}
    table, tbody, tr, td {{ display: block; width: 100%; }}
    tr {{ margin-bottom: 12px; border: 1px solid #ddd; border-radius: 8px; padding: 10px; background: #fff; box-shadow: 0 1px 3px rgba(0,0,0,.08); position: relative; }}
    td {{ padding: 3px 0; border: none; text-align: right; }}
    td::before {{ float: left; font-weight: 600; color: #555; }}
    tr > td:nth-child(1) {{ position: absolute; top: 8px; right: 8px; }}
    tr > td:nth-child(1)::before {{ display: none; }}
    tr > td:nth-child(2) {{ text-align: center; padding: 4px 0 8px; }}
    tr > td:nth-child(2)::before {{ display: none; }}
    .car-thumb {{ width: 100%; max-width: 280px; height: auto; max-height: 180px; }}
    .car-thumb:hover {{ transform: none; position: static; box-shadow: none; }}
    td.thumb-cell {{ width: 100%; }}
    tr > td:nth-child(3)::before {{ content: 'Search'; }}
    tr > td:nth-child(4)::before {{ content: 'Year'; }}
    tr > td:nth-child(5)::before {{ content: 'Trim'; }}
    tr > td:nth-child(6)::before {{ content: 'Price'; }}
    tr > td:nth-child(7)::before {{ content: 'Miles'; }}
    tr > td:nth-child(8)::before {{ content: 'Dist'; }}
    tr > td:nth-child(9)::before {{ content: 'Location'; }}
    tr > td:nth-child(10)::before {{ content: 'Dealer'; }}
    tr > td:nth-child(11)::before {{ content: 'Rating'; }}
    tr > td:nth-child(12)::before {{ content: 'Details'; display: block; float: none; margin-bottom: 2px; }}
    tr > td:nth-child(12) {{ text-align: left; }}
    tr > td:nth-child(13)::before {{ content: 'Source'; }}
    .vin-data {{ max-width: 100%; }}
    .followup-row {{ padding: 0; }}
    .followup-row td {{ text-align: left; }}
    .followup-row td::before {{ display: none; }}
    .followup-panel {{ flex-direction: column; padding: 8px; }}
    .followup-panel input, .followup-panel textarea {{ width: 100%; }}
    .count {{ font-size: .75rem; }}
  }}
</style>
</head>
<body>

<h1>All Searches \u2014 Combined Results</h1>
<p class="subtitle">Updated {today} &nbsp;|&nbsp; Lake Forest Park, WA 98155</p>

<div class="controls">
  <label>Search: <select id="fSearch">
    <option value="">All</option>
    {search_options}
  </select></label>
  <label>Max distance: <select id="fDist">
    <option value="9999">All</option>
    <option value="30">\u2264 30 mi</option>
    <option value="50">\u2264 50 mi</option>
    <option value="75">\u2264 75 mi</option>
    <option value="100">\u2264 100 mi</option>
    <option value="200">\u2264 200 mi</option>
  </select></label>
  <label>Min rating: <select id="fRating">
    <option value="0">All</option>
    <option value="5">5+</option>
    <option value="6">6+</option>
    <option value="7">7+</option>
  </select></label>
  <label>Source: <select id="fSource">
    <option value="">All</option>
    <option value="craigslist">Craigslist</option>
    <option value="autotrader">AutoTrader</option>
    <option value="cars.com">Cars.com</option>
    <option value="cargurus">CarGurus</option>
    <option value="facebook">Facebook</option>
    <option value="dealer">Dealer site</option>
  </select></label>
  <label><input type="checkbox" id="fNew"> New only</label>
  <label><input type="checkbox" id="fFollowed"> Followed only</label>
  <label><input type="checkbox" id="fHideRej" checked> Hide rejected</label>
  <label>Max price: <input id="fPrice" type="number" placeholder="e.g. 18000" style="width:100px"></label>
</div>

<p class="count"><span id="shown">0</span> of <span id="total">0</span> listings shown (<span id="newCount">0</span> new, <span id="removedCount">0</span> removed)</p>

<table>
<thead>
<tr>
  <th class="follow-star" title="Follow up">\u2606</th>
  <th>Photo</th>
  <th data-col="search">Search <span class="arrow">\u25B2</span></th>
  <th data-col="year" data-type="num">Year <span class="arrow">\u25B2</span></th>
  <th data-col="trim">Trim <span class="arrow">\u25B2</span></th>
  <th data-col="price" data-type="num">Price <span class="arrow">\u25B2</span></th>
  <th data-col="mileage" data-type="num">Mileage <span class="arrow">\u25B2</span></th>
  <th data-col="distance" data-type="num" class="sorted">Dist <span class="arrow">\u25B2</span></th>
  <th data-col="location">Location <span class="arrow">\u25B2</span></th>
  <th data-col="dealer">Dealer <span class="arrow">\u25B2</span></th>
  <th data-col="rating" data-type="num">Rating <span class="arrow">\u25B2</span></th>
  <th>VIN Data</th>
  <th>Source</th>
</tr>
</thead>
<tbody id="tbody"></tbody>
</table>

<script>
const DATA = [
{data_json}
];

const SEARCH_COLORS = {{}};
(function() {{
  const palette = ['#4a90d9','#d94a4a','#3aa56f','#c07a28','#7b5ea7','#c44daf'];
  const searches = [...new Set(DATA.map(r => r.search))];
  searches.forEach((s, i) => SEARCH_COLORS[s] = palette[i % palette.length]);
}})();

let sortCol = 'distance', sortAsc = true;

function fmt$(n) {{ return n > 0 ? '$' + n.toLocaleString() : '\u2014'; }}
function fmtMi(n) {{ return n > 0 ? n.toLocaleString() + ' mi' : '\u2014'; }}
function followedTag() {{ return '<span class="followed-tag">FOLLOWING</span>'; }}
function rejectedTag() {{ return '<span class="rejected-tag">REJECTED</span>'; }}
function removedTag() {{ return '<span class="removed-tag">REMOVED</span>'; }}
function newTag() {{ return '<span class="new-tag">NEW</span>'; }}
function sourceTag(s) {{
  const map = {{ craigslist: ['CL','src-cl'], autotrader: ['AT','src-at'], 'cars.com': ['Cars','src-cars'], cargurus: ['CG','src-cg'], facebook: ['FB','src-fb'], dealer: ['Dealer','src-dealer'] }};
  const [label, cls] = map[s] || [s, ''];
  return '<span class="source-tag ' + cls + '">' + label + '</span>';
}}
function searchTag(s) {{
  const c = SEARCH_COLORS[s] || '#888';
  return '<span class="search-tag" style="background:' + c + '22;color:' + c + ';border:1px solid ' + c + '44">' + s + '</span>';
}}
function ratingPill(r) {{
  const cls = r >= 7 ? 'r7' : r >= 6 ? 'r6' : r >= 5 ? 'r5' : r >= 4 ? 'r4' : 'r3';
  return '<span class="rating-pill ' + cls + '">' + r + '/10</span>';
}}

function val(row, col) {{
  const m = {{ search:'search', year:'year', trim:'trim', price:'price', mileage:'mileage', distance:'distance', location:'location', dealer:'dealer', rating:'rating' }};
  return row[m[col]] ?? '';
}}

function render() {{
  const maxDist = +document.getElementById('fDist').value;
  const minRat = +document.getElementById('fRating').value;
  const src = document.getElementById('fSource').value;
  const srch = document.getElementById('fSearch').value;
  const maxPrice = +document.getElementById('fPrice').value || Infinity;

  let rows = DATA.filter(r =>
    r.distance <= maxDist &&
    r.rating >= minRat &&
    r.price <= maxPrice &&
    (!src || r.source === src) &&
    (!srch || r.search === srch) &&
    (!document.getElementById('fNew').checked || r.isNew) &&
    (!document.getElementById('fFollowed').checked || isFollowed(r.url)) &&
    (!document.getElementById('fHideRej').checked || !isRejected(r.url))
  );

  rows.sort((a, b) => {{
    let av = val(a, sortCol), bv = val(b, sortCol);
    if (typeof av === 'string') {{ av = av.toLowerCase(); bv = bv.toLowerCase(); }}
    if (av < bv) return sortAsc ? -1 : 1;
    if (av > bv) return sortAsc ? 1 : -1;
    return 0;
  }});

  const tbody = document.getElementById('tbody');
  tbody.innerHTML = rows.map(r => {{
    const fu = isFollowed(r.url); const rej = isRejected(r.url);
    const cls = r.isRemoved ? ' class="removed-row"' : rej ? ' class="rejected-row"' : r.isNew ? ' class="new-row"' : fu ? ' class="followed"' : '';
    return '<tr' + cls + ' data-url="' + r.url.replace(/"/g,'&quot;') + '">' +
      '<td class="follow-star" onclick="togglePanel(\\'' + r.url.replace(/'/g,"\\\\\\\\'") + '\\', this)">' + ((fuStore()[r.url]||{{}}).starred ? '\u2605' : '\u2606') + '</td>' +
      '<td class="thumb-cell">' + (r.image_url ? '<img class="car-thumb" src="' + r.image_url + '" loading="lazy" onerror="this.style.display=\\\'none\\\'">' : '') + '</td>' +
      '<td>' + searchTag(r.search) + '</td>' +
      '<td>' + r.year + '</td>' +
      '<td>' + (r.trim || '\u2014') + '</td>' +
      '<td class="price">' + fmt$(r.price) + '</td>' +
      '<td class="mi">' + fmtMi(r.mileage) + '</td>' +
      '<td class="dist">' + r.distance + ' mi</td>' +
      '<td>' + r.location + '</td>' +
      '<td><a href="' + r.url + '" target="_blank" rel="noopener">' + r.dealer + '</a></td>' +
      '<td class="rating">' + ratingPill(r.rating) + '</td>' +
      '<td class="vin-data">' + (r.vin_data || '') + '</td>' +
      '<td>' + sourceTag(r.source) + (r.isRemoved ? removedTag() : r.isNew ? newTag() : '') + (isRejected(r.url) ? rejectedTag() : isFollowed(r.url) ? followedTag() : '') + '</td>' +
      '</tr>';
  }}).join('');

  document.getElementById('shown').textContent = rows.length;
  document.getElementById('total').textContent = DATA.length;
  const newShown = rows.filter(r => r.isNew).length;
  const newTotal = DATA.filter(r => r.isNew).length;
  document.getElementById('newCount').textContent = newShown + ' of ' + newTotal;
  const removedCount = DATA.filter(r => r.isRemoved).length;
  document.getElementById('removedCount').textContent = removedCount;

  document.querySelectorAll('th[data-col]').forEach(th => {{
    th.classList.toggle('sorted', th.dataset.col === sortCol);
    th.querySelector('.arrow').textContent = (th.dataset.col === sortCol) ? (sortAsc ? '\u25B2' : '\u25BC') : '\u25B2';
  }});
}}

document.querySelectorAll('th[data-col]').forEach(th => {{
  th.addEventListener('click', () => {{
    if (sortCol === th.dataset.col) sortAsc = !sortAsc;
    else {{ sortCol = th.dataset.col; sortAsc = true; }}
    render();
  }});
}});

document.querySelectorAll('.controls select, .controls input').forEach(el => {{
  el.addEventListener('change', render);
  el.addEventListener('input', render);
}});

// --- Follow-up tracking (Azure Blob + localStorage cache) ---
const FU_KEY = 'car_followups';
const STATE_URL = 'https://usedcarscannersite.blob.core.windows.net/state/car_followups.json?se=2028-03-23T17%3A04Z&sp=racwd&sv=2026-02-06&sr=b&sig=xnkKW5c%2BFglLX4AkguQft6VvWJs3whqsnj%2B4wyBYDV0%3D';
let _fuCache = {{}};
let _fuSaveTimer = null;
try {{ _fuCache = JSON.parse(localStorage.getItem(FU_KEY) || '{{}}'); }} catch(e) {{}}

function fuStore() {{ return _fuCache; }}
function fuSave(store) {{
  _fuCache = store;
  localStorage.setItem(FU_KEY, JSON.stringify(store));
  clearTimeout(_fuSaveTimer);
  _fuSaveTimer = setTimeout(_fuPush, 500);
}}
function fuGet(url) {{ return _fuCache[url] || {{}}; }}
function fuSet(url, data) {{ const s = fuStore(); s[url] = data; fuSave(s); }}
function isFollowed(url) {{ const d = _fuCache[url]; return !!(d && (d.phone || d.email || d.lastContact || d.notes || d.starred)); }}
function isRejected(url) {{ const d = _fuCache[url]; return !!(d && d.rejected); }}

async function _fuPush() {{
  try {{
    await fetch(STATE_URL, {{
      method: 'PUT',
      headers: {{'Content-Type':'application/json','x-ms-blob-type':'BlockBlob'}},
      body: JSON.stringify(_fuCache)
    }});
  }} catch(e) {{ console.warn('State sync failed:', e); }}
}}

(async function _fuInit() {{
  const local = {{..._fuCache}};
  let remote = {{}};
  try {{
    const resp = await fetch(STATE_URL + '&_t=' + Date.now());
    if (resp.ok) remote = await resp.json();
  }} catch(e) {{}}
  const merged = {{...remote}};
  for (const [url, ld] of Object.entries(local)) {{
    if (!merged[url]) {{ merged[url] = ld; continue; }}
    const lf = [ld.phone,ld.email,ld.lastContact,ld.notes].filter(Boolean).length;
    const rf = [merged[url].phone,merged[url].email,merged[url].lastContact,merged[url].notes].filter(Boolean).length;
    if (lf > rf) merged[url] = {{...ld}};
    if (ld.starred) merged[url].starred = true;
    if (ld.rejected) merged[url].rejected = true;
  }}
  _fuCache = merged;
  localStorage.setItem(FU_KEY, JSON.stringify(merged));
  if (Object.keys(local).length && JSON.stringify(remote) !== JSON.stringify(merged)) {{
    await _fuPush();
  }}
  render();
}})();

function escAttr(s) {{ return (s||'').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;'); }}

function togglePanel(url, btn) {{
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
    '<button class="fu-save" style="background:' + (starred ? '#d9534f' : '#5cb85c') + '" data-fur="star">' + (starred ? '\u2605 Unfollow' : '\u2606 Follow') + '</button>' +
    '<button class="fu-save" style="background:' + (rej ? '#5cb85c' : '#d9534f') + '" data-fur="rej">' + (rej ? '\u2714 Unreject' : '\u2718 Reject') + '</button>' +
    '</div></td>';
  newTr.querySelector('.fu-save').addEventListener('click', function() {{ saveFollowup(url); }});
  newTr.querySelector('[data-fur="star"]').addEventListener('click', function() {{
    const dd = fuGet(url);
    dd.starred = !dd.starred;
    if (!dd.starred && !dd.phone && !dd.email && !dd.lastContact && !dd.notes) {{
      const s = fuStore(); delete s[url]; fuSave(s);
    }} else {{ fuSet(url, dd); }}
    document.querySelector('.followup-row').remove();
    render();
  }});
  newTr.querySelector('[data-fur="rej"]').addEventListener('click', function() {{
    const dd = fuGet(url);
    dd.rejected = !dd.rejected;
    fuSet(url, dd);
    document.querySelector('.followup-row').remove();
    render();
  }});
  tr.after(newTr);
}}

function saveFollowup(url) {{
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
}}
// --- End follow-up tracking ---

render();
</script>
</body>
</html>'''

out_path = f"{OUT}/combined_results.html"
with open(out_path, "w", encoding="utf-8") as f:
    f.write(html)

print(f"\nCombined HTML: {len(all_rows)} total listings ({total_new} new, {total_gone} removed)")
print(f"Written to {out_path}")
