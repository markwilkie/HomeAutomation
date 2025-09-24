# Adobe Lightroom Cloud Access Tool

A command-line interface application for accessing Adobe Lightroom Cloud with single sign-on authentication and comprehensive photo management functionality.

## Features

- Single Sign-On (SSO) authentication with Adobe Creative SDK
- Retrieve and list photos marked as 'rejected' from Lightroom Cloud
- Search and filter photos by year, status, and other criteria
- Delete local photo files matching rejected photos from cloud
- Command-line interface for easy automation and scripting
- Secure token management and caching
- Support for both PKCE and confidential OAuth flows

## Requirements

- Python 3.8+ (Python 2.7 is not supported)
- Adobe Developer Account with Lightroom Cloud API access
- Internet connection for API calls

## Installation

1. Clone this repository
2. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```
3. Configure your Adobe API credentials (see Configuration section)

## Configuration

### Environment Variables (Sensitive Settings)

1. Create a `.env` file in the project root
2. Add your Adobe API Client ID:
   ```env
   ADOBE_CLIENT_ID=your_client_id_here
   ADOBE_REDIRECT_URI=http://localhost:8080/callback
   ```

### General Configuration (Paths and Settings)

The tool uses `config.yaml` for non-sensitive configuration like file paths and default settings:

1. Copy the example configuration:
   ```bash
   cp config.example.json config.yaml
   ```
   
2. Edit `config.yaml` to match your setup:
   ```yaml
   paths:
     photo_base: "D:\\Photos"          # Your main photo directory
     jpg_subdir: "jpg"                 # JPG subdirectory name
     raw_subdir: "raw"                 # RAW subdirectory name
     export_base: "D:\\Lightroom_Exports"
   
   settings:
     default_photo_limit: 1000
     default_output_format: "table"
     require_deletion_confirmation: true
   ```

The tool will automatically find and load configuration files in this order:
- `config.yaml` (preferred)
- `config.yml` 
- `config.json`
- `config.example.json` (fallback)

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

### Troubleshooting Authentication
Common issues:
* **invalid_client**: Wrong client ID, PKCE misconfiguration, or wrong endpoint version
* **invalid_grant**: Authorization code expired, redirect URI mismatch, or PKCE verification failure
* **invalid_scope**: One or more scopes not enabled for your integration

## Usage

### Quick Start - Automated Cleanup

For most users, start with the automated cleanup workflow:

```bash
# 1. Login first (opens browser for authentication)
python -m lightroom_tool auth login

# 2. Run complete cleanup workflow (recommended)
python -m lightroom_tool cleanup
```

The `cleanup` command performs the complete workflow automatically:
1. **Fetches** all rejected photos from Lightroom Cloud → `rejected.json`
2. **Previews** what local files would be deleted (dry-run)
3. **Asks for confirmation** before proceeding
4. **Deletes** the matched JPG/RAW file pairs

No parameters needed - uses your configuration defaults and shows a directory summary of found/missing files.

### Authentication Commands

#### Login to Adobe Lightroom Cloud
```bash
# Initial authentication (opens browser)
python -m lightroom_tool auth login
```

#### Check Authentication Status
```bash
# Verify if you're currently authenticated
python -m lightroom_tool auth status
```

#### Refresh Token
```bash
# Refresh expired authentication token
python -m lightroom_tool auth refresh
```

#### Logout
```bash
# Clear stored authentication tokens
python -m lightroom_tool auth logout
```

#### Get Authorization URL (Diagnostic)
```bash
# Print the authorization URL without completing login
python -m lightroom_tool auth url
```

### Photo Management Commands

#### Complete Cleanup Workflow (Recommended)
```bash
# Automated workflow: fetch rejected photos, preview, and delete with confirmation
python -m lightroom_tool cleanup
```

This command performs the complete cleanup workflow:
- Fetches all rejected photos from Lightroom Cloud
- Shows directory summary of found vs missing files
- Previews what would be deleted (JPG + RAW pairs)
- Asks for confirmation before deletion
- Uses your `config.yaml` settings automatically

#### List Rejected Photos
```bash
# List all rejected photos (default: 100 photos, table format)
python -m lightroom_tool photos list-rejected

# List rejected photos from specific year
python -m lightroom_tool photos list-rejected --year 2025

# List rejected photos from this year
python -m lightroom_tool photos list-rejected --this-year

# Get more photos and save to JSON file
python -m lightroom_tool photos list-rejected --limit 1000 --output json --save-to rejected.json

# Filter by different date types
python -m lightroom_tool photos list-rejected --year 2025 --date-type capture  # by photo date
python -m lightroom_tool photos list-rejected --year 2025 --date-type import   # by import date
python -m lightroom_tool photos list-rejected --year 2025 --date-type sync     # by sync date
```

#### Search Photos
```bash
# List all photos (not just rejected)
python -m lightroom_tool photos search

# List only accepted photos
python -m lightroom_tool photos search --accepted-only

# List only rejected photos (same as list-rejected)
python -m lightroom_tool photos search --rejected-only

# Search photos from specific year
python -m lightroom_tool photos search --year 2025 --limit 500

# Search photos from this year in JSON format
python -m lightroom_tool photos search --this-year --output json
```

#### Delete Local Photo Files
```bash
# Delete local JPG/RAF file pairs matching rejected photos
# Uses config settings and defaults to rejected.json
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --dry-run  # See what would be deleted first

# Use API date information to find photos in jpg\YYYY\mm-dd directory structure
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --date-type import \
  --dry-run

# Use capture date instead of import date for directory structure
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --date-type capture \
  --dry-run

# Override config settings with custom paths and JSON file
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --directory /path/to/photos \
  --rejected-json custom_rejected.json \
  --jpg-subdir "jpeg" \
  --raw-subdir "raw_files" \
  --dry-run

# Actually delete the files (remove --dry-run) with confirmation prompts
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --confirm
```

#### Get Photo Details
```bash
# Get detailed information about a specific photo
python -m lightroom_tool photos details <photo_id>
```

#### Dump Photo Metadata
```bash
# Dump detailed metadata for first 10 photos
python -m lightroom_tool photos dump

# Dump more photos with specific options
python -m lightroom_tool photos dump --limit 50 --output json

# Dump photos from specific year
python -m lightroom_tool photos dump --year 2025 --limit 100

# Save detailed metadata to file
python -m lightroom_tool photos dump --limit 200 --output json --save-to photo_metadata.json
```

#### List Catalogs
```bash
# List all available Lightroom catalogs
python -m lightroom_tool photos catalogs
```

#### List Albums
```bash
# List all albums in your catalog
python -m lightroom_tool photos albums

# List albums from specific catalog
python -m lightroom_tool photos albums --catalog-id <catalog_id>
```

#### Count All Photos
```bash
# Get total count of all photos in your catalog
python -m lightroom_tool photos count-all

# Count photos in specific catalog
python -m lightroom_tool photos count-all --catalog-id <catalog_id>
```

#### API Exploration (Advanced)
```bash
# Explore different API endpoints and parameters
python -m lightroom_tool photos api-explore

# Explore specific catalog
python -m lightroom_tool photos api-explore --catalog-id <catalog_id>
```

## Common Workflows

### 1. Automated Cleanup (Recommended)
```bash
# One-command workflow - handles everything automatically
python -m lightroom_tool cleanup

# Shows output like:
# 📁 Files found by directory:
#    2025\09-23: 49 jpg, 49 raw
# ❌ Files not found by directory:
#    2025\09-08: 23 files
#    2025\09-12: 22 files
```

### 2. Manual Export and Delete (Advanced)
```bash
# Step 1: Export rejected photos to JSON
python -m lightroom_tool photos list-rejected --output json --save-to rejected_2025.json --year 2025

# Step 2: Preview what local files would be deleted
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --directory /path/to/local/photos \
  --rejected-json rejected_2025.json \
  --dry-run

# Step 3: Actually delete the local files
python -m lightroom_tool photos delete-jpg-raw-pairs \
  --directory /path/to/local/photos \
  --rejected-json rejected_2025.json
```

### 3. Search and Export Photos by Year
```bash
# Export all accepted photos from 2025
python -m lightroom_tool photos search \
  --accepted-only \
  --year 2025 \
  --limit 2000 \
  --output json > accepted_2025.json

# Export all photos (accepted + rejected) from this year
python -m lightroom_tool photos search \
  --this-year \
  --limit 5000 \
  --output json > all_photos_2025.json
```

### 4. Get Detailed Photo Information
```bash
# Get summary of photo counts
python -m lightroom_tool photos count-all

# Get detailed metadata for recent photos
python -m lightroom_tool photos dump --this-year --limit 100 --output json --save-to recent_metadata.json
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
