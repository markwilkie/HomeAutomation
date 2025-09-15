# Adobe Lightroom Cloud Access Tool

A command-line interface application for accessing Adobe Lightroom Cloud with single sign-on authentication and rejected photos retrieval functionality.

## Features

- Single Sign-On (SSO) authentication with Adobe Creative SDK
- Retrieve and list photos marked as 'rejected' from Lightroom Cloud
- Command-line interface for easy automation and scripting
- Secure token management and caching

## Requirements

- Python 3.8+ (Python 2.7 is not supported)
- Adobe Developer Account with Lightroom Cloud API access
- Internet connection for API calls

## Installation

⚠️ **Note**: This application requires Python 3.8 or higher. The current system has Python 2.7 which is not compatible.

To install Python 3.8+:
1. Download Python from https://python.org/downloads/
2. Install Python 3.8 or higher
3. Ensure Python 3 is in your PATH

Once Python 3.8+ is installed:

1. Clone this repository
2. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```
3. Configure your Adobe API credentials (see Configuration section)

## Configuration

1. Create a `.env` file in the project root
2. Add your Adobe API Client ID:
   ```
   ADOBE_CLIENT_ID=your_client_id_here
   ADOBE_REDIRECT_URI=http://localhost:8080/callback
   ```

Note: This application uses PKCE (Proof Key for Code Exchange) flow, so no client secret is required. This is the recommended approach for applications that cannot securely store secrets.

### PKCE vs Confidential Flow

If you later obtain a client secret and want to use the classic confidential (non-PKCE) flow:
1. Add `ADOBE_CLIENT_SECRET=your_secret` to the `.env` file.
2. The tool will automatically switch to the confidential flow using legacy endpoints (authorize/v1 + token/v1).
3. Without a secret, the tool uses newer endpoints (authorize/v2 + token/v3) suitable for public PKCE clients.

### Redirect URI
Ensure the redirect URI you configure in the Adobe Developer Console matches exactly (character-for-character) the value in `ADOBE_REDIRECT_URI`. A mismatch causes `invalid_grant` or `invalid_client` errors during token exchange.

### Scopes
The tool currently requests the following scopes:
```
openid AdobeID lr_partner_apis offline_access lr_partner_rendition_apis
```
If you encounter scope errors, try re-authenticating temporarily with only:
```
openid
```
Once confirmed, reintroduce the other scopes after enabling them for your integration in the Adobe console.

### Troubleshooting Authentication
Common issues:
* invalid_client
   - Wrong client ID (typo or unregistered)
   - Using PKCE with an integration not configured as public; add a secret or recreate integration
   - Wrong endpoint version (the tool auto-selects based on presence of client secret)
* invalid_grant
   - Authorization code already used or expired
   - Redirect URI mismatch
   - PKCE code_verifier mismatch (rare unless modified manually)
* invalid_scope
   - One or more scopes not enabled for your integration

Run a diagnostic authorization URL display (does not complete login):
```bash
python -m lightroom_tool auth url
```
Then proceed with full login:
```bash
python -m lightroom_tool auth login
```

## Usage

### Authentication
```bash
python -m lightroom_tool auth login
```

### List Rejected Photos
```bash
python -m lightroom_tool photos list-rejected
```

### List Albums
```bash
python -m lightroom_tool photos albums
```

### List Catalogs
```bash
python -m lightroom_tool photos catalogs
```

### Dump Photo Metadata
```bash
python -m lightroom_tool photos dump --limit 10
```

### Get Photo Details
```bash
python -m lightroom_tool photos details <photo_id>
```

## Development

### Setup Development Environment
```bash
pip install -r requirements-dev.txt
```

### Run Tests
```bash
pytest
```

### Code Formatting
```bash
black .
flake8 .
```

## License

MIT License

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests
5. Submit a pull request
