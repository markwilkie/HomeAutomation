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
