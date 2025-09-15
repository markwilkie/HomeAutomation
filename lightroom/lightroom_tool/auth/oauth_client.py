"""
OAuth 2.0 client for Adobe Creative SDK authentication with PKCE support
"""

import json
import secrets
import webbrowser
import base64
import hashlib
from urllib.parse import urlencode, parse_qs
from http.server import HTTPServer, BaseHTTPRequestHandler
import threading
import time
import requests
from typing import Optional, Dict, Any, Tuple

from lightroom_tool.config import Config


class CallbackHandler(BaseHTTPRequestHandler):
    """HTTP request handler for OAuth callback"""
    
    def do_GET(self):
        """Handle GET request to callback URL"""
        if self.path.startswith('/callback'):
            # Parse authorization code from callback URL
            query_params = parse_qs(self.path.split('?')[1] if '?' in self.path else '')
            
            if 'code' in query_params:
                self.server.auth_code = query_params['code'][0]
                self.send_response(200)
                self.send_header('Content-type', 'text/html')
                self.end_headers()
                self.wfile.write(b'''
                    <html><body>
                        <h1>Authentication Successful!</h1>
                        <p>You can close this window and return to the command line.</p>
                    </body></html>
                ''')
            elif 'error' in query_params:
                self.server.auth_error = query_params['error'][0]
                self.send_response(400)
                self.send_header('Content-type', 'text/html')
                self.end_headers()
                self.wfile.write(f'''
                    <html><body>
                        <h1>Authentication Failed</h1>
                        <p>Error: {query_params['error'][0]}</p>
                        <p>Description: {query_params.get('error_description', ['Unknown error'])[0]}</p>
                    </body></html>
                '''.encode())
        else:
            self.send_response(404)
            self.end_headers()
    
    def log_message(self, format, *args):
        """Suppress log messages"""
        pass


class OAuthClient:
    """OAuth 2.0 client for Adobe Creative SDK with PKCE support

    Adobe IMS has multiple endpoint versions. PKCE-based public (no secret) flows
    commonly use newer versions (authorize v2 / token v3). The older v1 token
    endpoint may reject a public client (invalid_client) when PKCE is supplied.
    We dynamically select endpoints based on whether a client secret is present.
    """

    # Base endpoints (will be version-adjusted in __init__)
    BASE_AUTHORIZATION_URL = "https://ims-na1.adobelogin.com/ims/authorize"
    BASE_TOKEN_URL = "https://ims-na1.adobelogin.com/ims/token"
    # Adobe Lightroom Cloud API scopes
    SCOPE = "openid AdobeID lr_partner_apis offline_access lr_partner_rendition_apis"
    
    def __init__(self, config: Config):
        self.config = config
        self.client_id = config.adobe_client_id
        self.client_secret = config.adobe_client_secret
        self.redirect_uri = config.adobe_redirect_uri
        self.use_pkce = config.use_pkce_flow

        # Choose endpoint versions:
        # Public (PKCE) -> authorize/v2, token/v3
        # Confidential (secret) -> authorize/v1, token/v1 (stable classic flow)
        if self.use_pkce:
            self.AUTHORIZATION_URL = f"{self.BASE_AUTHORIZATION_URL}/v2"
            self.TOKEN_URL = f"{self.BASE_TOKEN_URL}/v3"
        else:
            self.AUTHORIZATION_URL = f"{self.BASE_AUTHORIZATION_URL}/v1"
            self.TOKEN_URL = f"{self.BASE_TOKEN_URL}/v1"
    
    def _generate_pkce_pair(self) -> Tuple[str, str]:
        """Generate PKCE code verifier and challenge"""
        # Generate code verifier (random 128 character string)
        code_verifier = base64.urlsafe_b64encode(secrets.token_bytes(96)).decode('utf-8').rstrip('=')
        
        # Generate code challenge (SHA256 hash of verifier, base64 encoded)
        challenge_bytes = hashlib.sha256(code_verifier.encode('utf-8')).digest()
        code_challenge = base64.urlsafe_b64encode(challenge_bytes).decode('utf-8').rstrip('=')
        
        return code_verifier, code_challenge
    
    def get_authorization_url(self, state: str, code_challenge: Optional[str] = None) -> str:
        """Generate authorization URL for OAuth flow"""
        params = {
            'client_id': self.client_id,
            'redirect_uri': self.redirect_uri,
            'scope': self.SCOPE,
            'response_type': 'code',
            'state': state
        }
        
        # Add PKCE parameters if using PKCE flow
        if self.use_pkce and code_challenge:
            params['code_challenge'] = code_challenge
            params['code_challenge_method'] = 'S256'
        
        return f"{self.AUTHORIZATION_URL}?{urlencode(params)}"
    
    def start_local_server(self, port: int = 8080) -> HTTPServer:
        """Start local HTTP server for OAuth callback"""
        server = HTTPServer(('localhost', port), CallbackHandler)
        server.auth_code = None
        server.auth_error = None
        return server
    
    def wait_for_callback(self, server: HTTPServer, timeout: int = 300) -> Optional[str]:
        """Wait for OAuth callback with authorization code"""
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            server.handle_request()
            
            if hasattr(server, 'auth_code') and server.auth_code:
                return server.auth_code
            elif hasattr(server, 'auth_error') and server.auth_error:
                raise Exception(f"OAuth error: {server.auth_error}")
        
        raise TimeoutError("OAuth callback timeout")
    
    def exchange_code_for_token(self, authorization_code: str, code_verifier: Optional[str] = None) -> Dict[str, Any]:
        """Exchange authorization code for access token"""
        data = {
            'grant_type': 'authorization_code',
            'client_id': self.client_id,
            'code': authorization_code,
            'redirect_uri': self.redirect_uri
        }
        
        # Add client secret for confidential clients
        if not self.use_pkce and self.client_secret:
            data['client_secret'] = self.client_secret
        
        # Add code verifier for PKCE flow
        if self.use_pkce and code_verifier:
            data['code_verifier'] = code_verifier
        
        headers = {
            'Content-Type': 'application/x-www-form-urlencoded'
        }
        
        print(f"Token exchange endpoint: {self.TOKEN_URL}")
        print(f"Token exchange request data: {data}")  # Debug output

        response = requests.post(self.TOKEN_URL, data=data, headers=headers)

        print(f"Token response status: {response.status_code}")  # Debug output
        body_text = response.text
        if response.status_code != 200:
            # Try to provide more helpful diagnostics
            try:
                err_json = response.json()
            except Exception:
                err_json = None
            print(f"Token response text: {body_text}")  # Raw
            if err_json and err_json.get('error') == 'invalid_client':
                print("Hint: 'invalid_client' with PKCE can indicate the integration is not configured as a public client, the client_id is wrong, or newer endpoint versions are required. We're already using v2/v3 for PKCE.")
            if err_json and err_json.get('error') == 'invalid_grant':
                print("Hint: 'invalid_grant' can mean code already used, expired, redirect_uri mismatch, or code_verifier mismatch.")

        response.raise_for_status()

        return response.json()
    
    def refresh_token(self, refresh_token: str) -> Dict[str, Any]:
        """Refresh access token using refresh token"""
        data = {
            'grant_type': 'refresh_token',
            'client_id': self.client_id,
            'refresh_token': refresh_token
        }
        
        # Add client secret for confidential clients only
        if not self.use_pkce and self.client_secret:
            data['client_secret'] = self.client_secret
        
        headers = {
            'Content-Type': 'application/x-www-form-urlencoded'
        }
        
        response = requests.post(self.TOKEN_URL, data=data, headers=headers)
        response.raise_for_status()
        
        return response.json()
    
    def authenticate(self) -> Dict[str, Any]:
        """Perform complete OAuth authentication flow"""
        # Generate state parameter for security
        state = secrets.token_urlsafe(32)
        
        # Generate PKCE parameters if using PKCE flow
        code_verifier = None
        code_challenge = None
        if self.use_pkce:
            code_verifier, code_challenge = self._generate_pkce_pair()
        
        # Get authorization URL and open in browser
        auth_url = self.get_authorization_url(state, code_challenge)
        print(f"Opening authorization URL in browser: {auth_url}")
        print("\nIf the browser doesn't open automatically, copy and paste this URL into your browser:")
        print(f"{auth_url}")
        
        webbrowser.open(auth_url)
        
        print("\nAfter authorization, you'll be redirected to a page that may not load.")
        print("Copy the authorization code from the URL and paste it below.")
        print("The URL will look like: http://localhost:8080/callback?code=AUTHORIZATION_CODE")
        
        # Get authorization code from user input
        while True:
            user_input = input("\nPaste the authorization code (or the full URL): ").strip()
            
            if not user_input:
                print("Please enter the authorization code or URL.")
                continue
            
            # Extract code from URL if full URL was pasted
            if 'code=' in user_input:
                try:
                    if '?' in user_input:
                        query_string = user_input.split('?')[1]
                        query_params = parse_qs(query_string)
                        if 'code' in query_params:
                            auth_code = query_params['code'][0]
                            break
                except:
                    print("Could not extract code from URL. Please paste just the authorization code.")
                    continue
            else:
                # Assume it's just the authorization code
                auth_code = user_input
                break
        
        try:
            # Exchange code for token
            token_data = self.exchange_code_for_token(auth_code, code_verifier)
            
            print("Authentication successful!")
            return token_data
            
        except Exception as e:
            print(f"Failed to exchange authorization code for token: {e}")
            print("Troubleshooting steps:")
            print(" 1. Confirm the client ID matches exactly what is in the Adobe Developer Console.")
            print(" 2. Ensure the redirect URI registered there is exactly '" + self.redirect_uri + "' (including scheme, host, port, path).")
            if self.use_pkce:
                print(" 3. This is a PKCE (public) flow. Verify the integration supports PKCE / public clients. If not, add a client secret and retry.")
                print(" 4. If issues persist, try re-running with a single minimal scope: 'openid' to isolate scope problems.")
            else:
                print(" 3. Using confidential flow. Ensure client secret is correct and not revoked.")
            print(" 5. Make sure you paste a fresh code (codes are single-use and expire quickly).")
            raise
