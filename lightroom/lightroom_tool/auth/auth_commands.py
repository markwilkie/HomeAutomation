"""
CLI commands for authentication
"""

import click
from lightroom_tool.config import Config
from lightroom_tool.auth.oauth_client import OAuthClient
from lightroom_tool.auth.token_manager import TokenManager


@click.group()
def auth():
    """Authentication commands"""
    pass


@auth.command()
@click.option('--force', is_flag=True, help='Force re-authentication even if tokens exist')
def login(force):
    """Authenticate with Adobe Creative SDK"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check if already authenticated
    if not force and token_manager.get_valid_access_token():
        click.echo("Already authenticated. Use --force to re-authenticate.")
        return
    
    try:
        # Perform OAuth authentication
        oauth_client = OAuthClient(config)
        token_data = oauth_client.authenticate()
        
        # Store tokens securely
        token_manager.store_tokens(token_data)
        
        click.echo("Authentication successful! Tokens stored securely.")
        
    except Exception as e:
        click.echo(f"Authentication failed: {e}", err=True)
        raise click.Abort()


@auth.command()
def logout():
    """Clear stored authentication tokens"""
    config = Config()
    token_manager = TokenManager(config)
    
    token_manager.clear_tokens()
    click.echo("Logged out successfully. Authentication tokens cleared.")


@auth.command()
def status():
    """Check authentication status"""
    config = Config()
    token_manager = TokenManager(config)
    
    token_data = token_manager.get_tokens()
    
    if not token_data:
        click.echo("Not authenticated. Run 'lightroom-tool auth login' to authenticate.")
        return
    
    if token_manager.is_token_valid(token_data):
        click.echo("✓ Authenticated and token is valid")
        
        # Show token expiration if available
        if 'expires_at' in token_data:
            from datetime import datetime
            expires_at = datetime.fromisoformat(token_data['expires_at'])
            click.echo(f"Token expires at: {expires_at}")
    else:
        click.echo("⚠ Authenticated but token has expired. Re-authentication required.")


@auth.command('url')
def auth_url():
    """Print the authorization URL (diagnostic helper)"""
    config = Config()
    oauth_client = OAuthClient(config)
    state = 'diagnostic_state'
    # Generate a one-off PKCE pair if needed to show full URL (code_challenge must align with eventual verifier, so we warn user it's diagnostic only)
    code_challenge = None
    if oauth_client.use_pkce:
        _, code_challenge = oauth_client._generate_pkce_pair()  # noqa: SLF001 (internal use acceptable here)
    url = oauth_client.get_authorization_url(state, code_challenge)
    click.echo("Authorization URL (for copy/paste):")
    click.echo(url)
    if oauth_client.use_pkce:
        click.echo("Note: This URL uses a transient code_challenge for display only. Run 'lightroom-tool auth login' for a real flow.")


@auth.command()
def refresh():
    """Refresh authentication token"""
    config = Config()
    token_manager = TokenManager(config)
    
    access_token = token_manager.get_valid_access_token()
    
    if access_token:
        click.echo("✓ Token refreshed successfully")
    else:
        click.echo("Failed to refresh token. Please re-authenticate.", err=True)
        raise click.Abort()


@auth.command()
@click.option('--timeout', default=300, show_default=True, help='Seconds to wait for the browser')
def signin(timeout):
    """Sign in without a terminal prompt: opens the browser and catches the redirect on
    localhost:8080 (IPv4 and IPv6). If Adobe redirects somewhere else (e.g. httpbin.org),
    paste that page's address when asked."""
    import secrets, socket, threading, time, webbrowser
    from http.server import HTTPServer, BaseHTTPRequestHandler
    from urllib.parse import urlparse, parse_qs

    config = Config()
    oc, tm = OAuthClient(config), TokenManager(config)
    verifier = challenge = None
    if oc.use_pkce:
        verifier, challenge = oc._generate_pkce_pair()
    state = secrets.token_urlsafe(32)
    url = oc.get_authorization_url(state, challenge)
    got = {}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            q = parse_qs(urlparse(self.path).query)
            ok = q.get('state', [''])[0] == state and 'code' in q
            if ok:
                got['code'] = q['code'][0]
            body = b"<h2>Signed in. You can close this tab.</h2>" if ok else b"<h2>Not a valid sign-in response.</h2>"
            self.send_response(200); self.send_header("Content-Type", "text/html"); self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    class Server6(HTTPServer):
        address_family = socket.AF_INET6

    servers = []
    for cls, addr in ((HTTPServer, ("127.0.0.1", 8080)), (Server6, ("::1", 8080))):
        try:
            servers.append(cls(addr, Handler))
        except OSError:
            pass
    for s in servers:
        threading.Thread(target=s.serve_forever, daemon=True).start()

    click.echo("Opening Adobe sign-in in your browser. Approve access there.")
    click.echo(f"If no browser opens, use: {url}")
    webbrowser.open(url)
    deadline = time.time() + timeout
    while 'code' not in got and time.time() < deadline:
        time.sleep(1)
    for s in servers:
        s.shutdown()
    code = got.get('code')
    if not code:
        pasted = click.prompt("The browser didn't come back here. Paste the address it ended on", default="",
                              show_default=False).strip()
        q = parse_qs(urlparse(pasted).query)
        if q.get('state', [''])[0] != state or 'code' not in q:
            click.echo("That address isn't from this sign-in.", err=True)
            raise click.Abort()
        code = q['code'][0]
    tm.store_tokens(oc.exchange_code_for_token(code, verifier))
    click.echo("OK: signed in.")


@auth.command('export-token')
@click.argument('path')
def export_token(path):
    """Write the current sign-in (refreshed if needed) as plain JSON to PATH, for a headless host."""
    import json, os
    config = Config()
    tm = TokenManager(config)
    if not tm.get_valid_access_token():
        click.echo("Not signed in. Run 'auth signin' first.", err=True)
        raise click.Abort()
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump(tm.get_tokens(), f)
    click.echo(f"OK: wrote sign-in to {path}")
