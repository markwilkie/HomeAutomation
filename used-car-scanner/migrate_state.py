"""One-time migration: serve a local page that reads localStorage and pushes to Azure."""
import http.server
import webbrowser
import threading

PORT = 8765

BLOB_URL = (
    "https://usedcarscannersite.blob.core.windows.net/state/car_followups.json"
    "?se=2028-03-23T17%3A04Z&sp=racwd&sv=2026-02-06&sr=b"
    "&sig=xnkKW5c%2BFglLX4AkguQft6VvWJs3whqsnj%2B4wyBYDV0%3D"
)

HTML = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Migrating...</title></head>
<body style="font-family:system-ui;background:#1a1a2e;color:#e0e0e0;padding:40px;max-width:700px;margin:0 auto">
<h1 style="color:#00d4ff">Migrating State to Azure...</h1>
<pre id="log" style="background:#222;padding:12px;border-radius:6px;white-space:pre-wrap"></pre>
<script>
const log = s => {{ document.getElementById('log').textContent += s + '\\n'; }};

(async () => {{
  const FU_KEY = 'car_followups';
  const STATE_URL = '{BLOB_URL}';
  
  // Read local
  let local = {{}};
  try {{ local = JSON.parse(localStorage.getItem(FU_KEY) || '{{}}'); }} catch(e) {{}}
  log('Local localStorage: ' + Object.keys(local).length + ' listings');
  
  if (Object.keys(local).length === 0) {{
    log('\\nERROR: No data found! Make sure this is the same browser where you view local HTML results.');
    return;
  }}
  
  const starred = Object.values(local).filter(d => d.starred).length;
  const rejected = Object.values(local).filter(d => d.rejected).length;
  const withNotes = Object.values(local).filter(d => d.notes).length;
  log('  ' + starred + ' starred, ' + rejected + ' rejected, ' + withNotes + ' with notes');
  
  // Fetch remote
  let remote = {{}};
  try {{
    const resp = await fetch(STATE_URL + '&_t=' + Date.now());
    if (resp.ok) remote = await resp.json();
  }} catch(e) {{ log('Remote fetch note: ' + e.message); }}
  log('Remote blob: ' + Object.keys(remote).length + ' listings');
  
  // Merge
  const merged = {{...remote}};
  for (const [url, ld] of Object.entries(local)) {{
    if (!merged[url]) {{ merged[url] = ld; continue; }}
    const lf = [ld.phone,ld.email,ld.lastContact,ld.notes].filter(Boolean).length;
    const rf = [merged[url].phone,merged[url].email,merged[url].lastContact,merged[url].notes].filter(Boolean).length;
    if (lf > rf) merged[url] = {{...ld}};
    if (ld.starred) merged[url].starred = true;
    if (ld.rejected) merged[url].rejected = true;
  }}
  log('Merged: ' + Object.keys(merged).length + ' listings');
  
  // Push
  try {{
    const resp = await fetch(STATE_URL, {{
      method: 'PUT',
      headers: {{'Content-Type':'application/json','x-ms-blob-type':'BlockBlob'}},
      body: JSON.stringify(merged)
    }});
    if (resp.ok || resp.status === 201) {{
      log('\\n✓ SUCCESS! Pushed ' + Object.keys(merged).length + ' listings to Azure.');
      log('You can close this tab now.');
    }} else {{
      log('\\n✗ Upload failed: HTTP ' + resp.status);
      log(await resp.text());
    }}
  }} catch(e) {{
    log('\\n✗ Upload failed: ' + e.message);
  }}
}})();
</script>
</body></html>
"""

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(HTML.encode())
    def log_message(self, format, *args):
        pass  # suppress logs

server = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
print(f"Starting local server on http://127.0.0.1:{PORT}")
print("Opening browser... (close this window or Ctrl+C when done)")

webbrowser.open(f"http://127.0.0.1:{PORT}")
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
server.server_close()
print(f"Found {len(local)} tracked listings in localStorage")

# Show summary
starred = sum(1 for d in local.values() if d.get("starred"))
rejected = sum(1 for d in local.values() if d.get("rejected"))
with_notes = sum(1 for d in local.values() if d.get("notes"))
print(f"  {starred} starred, {rejected} rejected, {with_notes} with notes")

# Fetch remote
try:
    resp = urllib.request.urlopen(BLOB_URL)
    remote = json.loads(resp.read().decode())
except Exception:
    remote = {}
print(f"Remote has {len(remote)} listings")

# Merge: local wins for richer data
merged = {**remote}
for url, ld in local.items():
    if url not in merged:
        merged[url] = ld
        continue
    lf = sum(1 for x in [ld.get("phone"), ld.get("email"), ld.get("lastContact"), ld.get("notes")] if x)
    rf = sum(1 for x in [merged[url].get("phone"), merged[url].get("email"), merged[url].get("lastContact"), merged[url].get("notes")] if x)
    if lf > rf:
        merged[url] = dict(ld)
    if ld.get("starred"):
        merged[url]["starred"] = True
    if ld.get("rejected"):
        merged[url]["rejected"] = True

# Push
body = json.dumps(merged).encode()
req = urllib.request.Request(BLOB_URL, data=body, method="PUT")
req.add_header("Content-Type", "application/json")
req.add_header("x-ms-blob-type", "BlockBlob")
resp = urllib.request.urlopen(req)
print(f"Pushed {len(merged)} listings to Azure (HTTP {resp.status})")
