from playwright.sync_api import sync_playwright
import re, json

sites = [
    ('Tacoma Nissan', 'https://www.tacomanissan.com/used-vehicles/'),
    ('KarMART Nissan', 'https://www.karmartnissan.com/used-vehicles/'),
]

with sync_playwright() as p:
    browser = p.chromium.launch(headless=False)  # headed to bypass some bot checks
    for name, url in sites:
        try:
            page = browser.new_page()
            page.goto(url, wait_until='domcontentloaded', timeout=30000)
            page.wait_for_timeout(8000)
            html = page.content()
            final_url = page.url
            print(f'\n=== {name} === url={final_url}')
            print(f'HTML length: {len(html)}')
            
            # Save HTML for analysis
            fname = name.replace(' ', '_').lower() + '_page.html'
            with open(fname, 'w', encoding='utf-8') as f:
                f.write(html)
            print(f'Saved to {fname}')
            
            # Look for vehicle cards/containers
            for pattern in [
                r'class="[^"]*vehicle[^"]*"',
                r'class="[^"]*inventory[^"]*"',
                r'class="[^"]*listing[^"]*"',
                r'class="[^"]*srp[^"]*"',
                r'class="[^"]*card[^"]*"',
                r'data-vehicle',
                r'data-vin',
                r'itemtype="[^"]*Vehicle',
            ]:
                matches = re.findall(pattern, html[:100000], re.I)
                if matches:
                    unique = list(set(matches))[:5]
                    print(f'  Pattern {pattern}: {len(matches)} matches -> {unique}')
            
            # Check for JSON-LD
            ld_matches = re.findall(r'<script[^>]*type="application/ld\+json"[^>]*>(.*?)</script>', html, re.DOTALL | re.I)
            for i, ld in enumerate(ld_matches[:2]):
                try:
                    data = json.loads(ld)
                    t = data.get('@type', data.get('type', '?'))
                    print(f'  JSON-LD #{i}: type={t}')
                except:
                    print(f'  JSON-LD #{i}: parse error, first 100: {ld[:100]}')
            
            # Check for __NEXT_DATA__ or similar JS data
            for jsvar in ['__NEXT_DATA__', 'window.__data', 'window.inventory', 'vehicleData', 'srpData']:
                if jsvar.lower() in html.lower():
                    print(f'  Found JS var: {jsvar}')
            
            page.close()
        except Exception as e:
            print(f'{name}: ERROR {str(e)[:200]}')
            try:
                page.close()
            except:
                pass
    browser.close()
