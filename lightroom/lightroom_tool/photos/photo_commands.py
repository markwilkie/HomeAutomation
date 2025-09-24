"""
CLI commands for photo operations
"""

import click
import json
from typing import Optional

from lightroom_tool.config import Config
from lightroom_tool.photos.api_client import LightroomAPIClient
from lightroom_tool.auth.token_manager import TokenManager


@click.group()
def photos():
    """Photo management commands"""
    pass


@photos.command('list-rejected')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
@click.option('--limit', default=100, help='Maximum number of photos to retrieve per request')
@click.option('--output', type=click.Choice(['table', 'json']), default='table', help='Output format')
@click.option('--save-to', type=click.Path(), help='Save results to file')
@click.option('--year', type=int, help='Filter rejected photos by year (e.g., --year 2025)')
@click.option('--this-year', is_flag=True, help='Show rejected photos from this year')
@click.option('--date-type', type=click.Choice(['import', 'sync', 'capture']), default='import',
              help='Which date to use for year filtering: import (when imported to Lightroom), sync (when synced to cloud), capture (when photo was taken)')
def list_rejected(catalog_id: Optional[str], limit: int, output: str, save_to: Optional[str], year: Optional[int], this_year: bool, date_type: str):
    """List all rejected photos from Lightroom Cloud"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            
            # Use first catalog by default
            catalog_id = catalogs[0]['id']
            click.echo(f"Using catalog: {catalogs[0].get('name', catalog_id)}")
        

        # Validate year options
        if year and this_year:
            click.echo("Cannot specify both --year and --this-year", err=True)
            raise click.Abort()

        # Determine year filter
        target_year = None
        if this_year:
            from datetime import datetime
            target_year = datetime.now().year
            click.echo(f"Filtering rejected photos from {target_year}...")
        elif year:
            if year < 1900 or year > 2100:
                click.echo("Error: Year must be between 1900 and 2100", err=True)
                raise click.Abort()
            target_year = year
            click.echo(f"Filtering rejected photos from {target_year}...")

        # Use search_photos for combined filtering
        query = {
            'rejected_only': True,
            'year': target_year,
            'date_type': date_type
        }
        collection = api_client.search_photos(catalog_id, query, limit=limit)
        rejected_photos = collection.photos

        if not rejected_photos:
            click.echo("No rejected photos found.")
            return

        # Format output
        if output == 'json':
            photos_data = [photo.to_dict() for photo in rejected_photos]
            output_text = json.dumps(photos_data, indent=2, default=str)
        else:
            # Table format
            lines = [
                "ID\tFilename\tCreated\tSize\tFormat",
                "-" * 80
            ]
            for photo in rejected_photos:
                created = photo.created_date.strftime('%Y-%m-%d %H:%M') if photo.created_date else 'Unknown'
                size = f"{photo.file_size // 1024}KB" if photo.file_size else 'Unknown'
                format_type = photo.format or 'Unknown'
                lines.append(f"{photo.id[:8]}\t{photo.filename[:30]}\t{created}\t{size}\t{format_type}")
            output_text = "\n".join(lines)
            output_text += f"\n\nTotal rejected photos: {len(rejected_photos)}"

        # Save to file or print
        if save_to:
            with open(save_to, 'w') as f:
                f.write(output_text)
            click.echo(f"Results saved to {save_to}")
        else:
            click.echo(output_text)
            
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('details')
@click.argument('photo_id')
@click.option('--catalog-id', help='Specific catalog ID (optional)')
@click.option('--output', type=click.Choice(['table', 'json']), default='table', help='Output format')
def photo_details(photo_id: str, catalog_id: Optional[str], output: str):
    """Get detailed information about a specific photo"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            catalog_id = catalogs[0]['id']
        
        # Get photo details
        photo = api_client.get_photo_details(catalog_id, photo_id)
        
        if output == 'json':
            click.echo(json.dumps(photo.to_dict(), indent=2, default=str))
        else:
            # Table format
            click.echo(f"Photo Details for {photo.filename}")
            click.echo("=" * 50)
            click.echo(f"ID: {photo.id}")
            click.echo(f"Filename: {photo.filename}")
            click.echo(f"Status: {photo.flag or 'UNFLAGGED'}")
            click.echo(f"Created: {photo.created_date or 'Unknown'}")
            click.echo(f"Updated: {photo.updated_date or 'Unknown'}")
            click.echo(f"File Size: {photo.file_size or 'Unknown'}")
            click.echo(f"Format: {photo.format or 'Unknown'}")
            
            if photo.metadata:
                click.echo("\nMetadata:")
                for key, value in photo.metadata.items():
                    click.echo(f"  {key}: {value}")
                    
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('catalogs')
@click.option('--output', type=click.Choice(['table', 'json']), default='table', help='Output format')
def list_catalogs(output: str):
    """List available Lightroom catalogs"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        catalogs = api_client.get_catalogs()
        
        if not catalogs:
            click.echo("No catalogs found.")
            return
        
        if output == 'json':
            click.echo(json.dumps(catalogs, indent=2, default=str))
        else:
            click.echo("Available Catalogs:")
            click.echo("=" * 50)
            for catalog in catalogs:
                click.echo(f"ID: {catalog['id']}")
                click.echo(f"Name: {catalog.get('name', 'Unnamed')}")
                click.echo(f"Created: {catalog.get('created', 'Unknown')}")
                click.echo("-" * 30)
                
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('dump')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
@click.option('--limit', default=10, help='Number of photos to dump (default: 10)')
@click.option('--output', type=click.Choice(['table', 'json']), default='table', help='Output format')
@click.option('--save-to', type=click.Path(), help='Save results to file')
@click.option('--include-tasks', is_flag=True, help='Include system tasks (default: photos only)')
@click.option('--year', type=int, help='Filter photos by year')
@click.option('--this-year', is_flag=True, help='Filter photos from this year')
def dump_photos(catalog_id: Optional[str], limit: int, output: str, save_to: Optional[str], include_tasks: bool, year: Optional[int], this_year: bool):
    """Dump detailed attributes and metadata for photos"""
    import json  # Add json import at the top
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            
            # Use first catalog by default
            catalog_id = catalogs[0]['id']
            click.echo(f"Using catalog: {catalog_id}")
        
        # Handle year filtering options
        target_year = None
        if this_year:
            from datetime import datetime
            target_year = datetime.now().year
            click.echo(f"Filtering photos from {target_year}...")
        elif year:
            if year < 1900 or year > 2100:
                click.echo("Error: Year must be between 1900 and 2100", err=True)
                raise click.Abort()
            target_year = year
            click.echo(f"Filtering photos from {target_year}...")

        # Get photos using proper pagination to ensure we get ALL photos
        click.echo(f"Fetching photos with full metadata...")
        
        # Always fetch ALL photos using proper timestamp pagination
        all_photos = []
        cursor = None
        
        while True:
            # Use the updated get_assets method with timestamp pagination
            collection = api_client.get_assets(catalog_id, limit=500, updated_since=cursor)
            all_photos.extend(collection.photos)
            
            # Check if we have more pages
            if not collection.has_more or not collection.next_cursor:
                break
                
            cursor = collection.next_cursor
        
        # Create a PhotoCollection with all photos
        from lightroom_tool.photos.models import PhotoCollection
        collection = PhotoCollection(
            photos=all_photos,
            total_count=len(all_photos),
            has_more=False,
            next_cursor=None
        )
        
        click.echo(f"Total photos fetched: {len(collection.photos)}")
        
        # Filter out system tasks unless specifically requested
        if not include_tasks:
            filtered_photos = [photo for photo in collection.photos if photo.format not in ['task']]
            collection.photos = filtered_photos
        
        # Apply year filtering if specified
        if target_year:
            year_filtered = []
            for photo in collection.photos:
                # Check import_timestamp first (most meaningful), then created_date, then capture_date
                photo_year = None
                
                # Try import_timestamp first (when imported to Lightroom)
                if photo.metadata and photo.metadata.get('import_timestamp'):
                    try:
                        from datetime import datetime
                        import_date_str = photo.metadata['import_timestamp']
                        if import_date_str:
                            import_date = datetime.fromisoformat(import_date_str.replace('Z', '+00:00'))
                            photo_year = import_date.year
                    except (ValueError, TypeError):
                        pass
                
                # Fall back to created_date (cloud sync date)
                if photo_year is None and photo.created_date:
                    photo_year = photo.created_date.year
                
                # Fall back to capture_date from metadata (original photo date)
                if photo_year is None and photo.metadata and photo.metadata.get('capture_date'):
                    try:
                        from datetime import datetime
                        capture_date_str = photo.metadata['capture_date']
                        if capture_date_str and capture_date_str != '0000-00-00T00:00:00':
                            capture_date = datetime.fromisoformat(capture_date_str.replace('Z', '+00:00'))
                            photo_year = capture_date.year
                    except (ValueError, TypeError):
                        continue
                
                if photo_year == target_year:
                    year_filtered.append(photo)
            
            collection.photos = year_filtered[:limit]  # Take only the requested number
        else:
            collection.photos = collection.photos[:limit]
        
        if not collection.photos:
            click.echo("No photos found.")
            return
        
        # Format output - show RAW API data exactly as received
        if output == 'json':
            # Output raw API response data exactly as received from Adobe
            raw_photos_data = []
            for photo in collection.photos:
                if hasattr(photo, 'raw_api_data') and photo.raw_api_data:
                    raw_photos_data.append(photo.raw_api_data)
                else:
                    # Fallback to parsed data if raw not available
                    raw_photos_data.append(photo.to_dict())
            
            output_text = json.dumps(raw_photos_data, indent=2, default=str)
        else:
            # Detailed table format showing RAW API fields
            lines = [
                f"RAW API Data Dump for Catalog: {catalog_id}",
                f"Total Photos Retrieved: {len(collection.photos)}",
                "=" * 80,
                ""
            ]
            
            for i, photo in enumerate(collection.photos, 1):
                lines.append(f"Photo #{i} - RAW API Response:")
                lines.append("-" * 50)
                
                # Show the raw API data exactly as received
                if hasattr(photo, 'raw_api_data') and photo.raw_api_data:
                    raw_data = photo.raw_api_data
                    lines.append(f"  RAW API Fields:")
                    lines.append(f"    id: {raw_data.get('id', 'N/A')}")
                    lines.append(f"    type: {raw_data.get('type', 'N/A')}")
                    lines.append(f"    subtype: {raw_data.get('subtype', 'N/A')}")
                    lines.append(f"    created: {raw_data.get('created', 'N/A')}")
                    lines.append(f"    updated: {raw_data.get('updated', 'N/A')}")
                    
                    # Show asset data structure
                    if 'asset' in raw_data:
                        asset = raw_data['asset']
                        lines.append(f"    asset.id: {asset.get('id', 'N/A')}")
                        if 'payload' in asset:
                            payload = asset['payload']
                            lines.append(f"    asset.payload.name: {payload.get('name', 'N/A')}")
                            lines.append(f"    asset.payload.format: {payload.get('format', 'N/A')}")
                            lines.append(f"    asset.payload.file_size: {payload.get('file_size', 'N/A')}")
                            lines.append(f"    asset.payload.sha256: {payload.get('sha256', 'N/A')}")
                            
                            # Show XMP metadata if available
                            if 'xmp' in payload:
                                lines.append(f"    asset.payload.xmp keys: {list(payload['xmp'].keys())}")
                    
                    # Show payload structure
                    if 'payload' in raw_data:
                        payload = raw_data['payload']
                        lines.append(f"    payload.action: {payload.get('action', 'N/A')}")
                        lines.append(f"    payload.application: {payload.get('application', 'N/A')}")
                        lines.append(f"    payload.captureDate: {payload.get('captureDate', 'N/A')}")
                        lines.append(f"    payload.status: {payload.get('status', 'N/A')}")
                    
                    # Show links if available
                    if 'links' in raw_data:
                        links = raw_data['links']
                        lines.append(f"    links: {list(links.keys())}")
                        if 'self' in links:
                            lines.append(f"    links.self.href: {links['self'].get('href', 'N/A')}")
                    
                    # Show the complete raw JSON for this photo
                    lines.append("    COMPLETE RAW JSON:")
                    raw_json = json.dumps(raw_data, indent=6, default=str)
                    lines.append(raw_json)
                else:
                    lines.append("    No raw API data available")
                    lines.append(f"    Parsed filename: {photo.filename}")
                    lines.append(f"    Parsed ID: {photo.id}")
                
                lines.append("")  # Add spacing between photos
            
            output_text = "\n".join(lines)
        
        # Save to file or print
        if save_to:
            with open(save_to, 'w', encoding='utf-8') as f:
                f.write(output_text)
            click.echo(f"Photo dump saved to {save_to}")
        else:
            click.echo(output_text)
            
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('albums')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
@click.option('--output', type=click.Choice(['table', 'json']), default='table', help='Output format')
def list_albums(catalog_id: Optional[str], output: str):
    """List albums from Lightroom Cloud catalog"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            catalog_id = catalogs[0]['id']
            click.echo(f"Using catalog: {catalog_id}")
        
        # Get albums
        albums = api_client.get_albums(catalog_id)
        
        if not albums:
            click.echo("No albums found.")
            return
        
        if output == 'json':
            click.echo(json.dumps(albums, indent=2, default=str))
        else:
            click.echo("Available Albums:")
            click.echo("=" * 50)
            for album in albums:
                album_payload = album.get('payload', {})
                click.echo(f"ID: {album['id']}")
                click.echo(f"Name: {album_payload.get('name', 'Unnamed')}")
                click.echo(f"Created: {album.get('created', 'Unknown')}")
                click.echo(f"Updated: {album.get('updated', 'Unknown')}")
                click.echo(f"Subtype: {album.get('subtype', 'Unknown')}")
                
                # Show additional details if available
                if album_payload.get('description'):
                    click.echo(f"Description: {album_payload['description']}")
                
                click.echo("-" * 30)
                
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('api-explore')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
def explore_api_endpoints(catalog_id: Optional[str]):
    """Explore different API endpoints to find all photos"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            catalog_id = catalogs[0]['id']
        
        click.echo(f"Exploring API endpoints for catalog: {catalog_id}")
        click.echo("=" * 60)
        
        # Test 1: Standard assets endpoint with different parameters
        click.echo("\n1. Testing standard /assets endpoint with different parameters:")
        
        test_params = [
            {},
            {'embed': 'asset'},
            {'embed': 'asset;develop_settings'},
            {'embed': 'asset;develop_settings;revisions'},
            {'include_deleted': 'true'},
            {'include_all': 'true'},
            {'status': 'all'},
        ]
        
        for i, params in enumerate(test_params):
            try:
                import requests
                from urllib.parse import urlencode
                
                url = f"{api_client.BASE_URL}/{api_client.API_VERSION}/catalogs/{catalog_id}/assets"
                if params:
                    url += f"?{urlencode(params)}"
                
                response = requests.get(url, headers=api_client._get_headers())
                if response.status_code == 200:
                    data = response.json()
                    # Remove JSONP padding if present
                    if isinstance(data, str) and data.startswith('while (1) {}'):
                        data = data[11:]
                        import json
                        data = json.loads(data)
                    
                    resources = data.get('resources', [])
                    wjd_count = sum(1 for r in resources if r.get('asset', {}).get('payload', {}).get('importSource', {}).get('fileName', '').startswith('WJD'))
                    click.echo(f"  Test {i+1} - Params: {params}")
                    click.echo(f"    Total photos: {len(resources)}, WJD photos: {wjd_count}")
                else:
                    click.echo(f"  Test {i+1} - Params: {params} - HTTP {response.status_code}")
            except Exception as e:
                click.echo(f"  Test {i+1} - Params: {params} - Error: {str(e)}")
        
        # Test 2: Try different asset types or endpoints
        click.echo("\n2. Testing alternative endpoints:")
        
        alternative_endpoints = [
            f"catalogs/{catalog_id}/assets/photos",
            f"catalogs/{catalog_id}/photos", 
            f"catalogs/{catalog_id}/images",
            f"catalogs/{catalog_id}/media",
            f"catalogs/{catalog_id}/assets?type=image",
        ]
        
        for endpoint in alternative_endpoints:
            try:
                import requests
                url = f"{api_client.BASE_URL}/{api_client.API_VERSION}/{endpoint}"
                response = requests.get(url, headers=api_client._get_headers())
                
                if response.status_code == 200:
                    data = response.json()
                    if isinstance(data, str) and data.startswith('while (1) {}'):
                        data = data[11:]
                        import json
                        data = json.loads(data)
                    
                    resources = data.get('resources', [])
                    click.echo(f"  {endpoint}: {len(resources)} items")
                else:
                    click.echo(f"  {endpoint}: HTTP {response.status_code}")
            except Exception as e:
                click.echo(f"  {endpoint}: Error - {str(e)}")
        
        # Test 3: Check pagination with very large limits
        click.echo("\n3. Testing pagination with large limits:")
        
        for limit in [1000, 2000, 5000]:
            try:
                collection = api_client.get_assets(catalog_id, limit=limit)
                wjd_count = sum(1 for p in collection.photos if p.filename.startswith('WJD'))
                click.echo(f"  Limit {limit}: {len(collection.photos)} photos, {wjd_count} WJD, has_more: {collection.has_more}")
            except Exception as e:
                click.echo(f"  Limit {limit}: Error - {str(e)}")
                
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('count-all')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
@click.option('--pattern', help='Filter by filename pattern (e.g., WJD, DSC)')
def count_all_photos(catalog_id: Optional[str], pattern: Optional[str]):
    """Count ALL photos in Lightroom Cloud using proper pagination"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            catalog_id = catalogs[0]['id']
        
        # Fetch ALL photos using proper pagination
        click.echo("Fetching ALL photos using pagination...")
        all_photos = []
        cursor = None
        page_count = 0
        
        while True:
            page_count += 1
            click.echo(f"Fetching page {page_count}...")
            
            collection = api_client.get_assets(catalog_id, limit=500, cursor=cursor)
            all_photos.extend(collection.photos)
            
            click.echo(f"  Got {len(collection.photos)} photos, total so far: {len(all_photos)}")
            
            # Check if we have more pages
            if not collection.has_more or not collection.next_cursor:
                break
                
            cursor = collection.next_cursor
        
        click.echo(f"\nTotal photos found: {len(all_photos)}")
        
        # Apply pattern filter if specified
        if pattern:
            filtered_photos = [photo for photo in all_photos if pattern.upper() in photo.filename.upper()]
            click.echo(f"Photos matching '{pattern}': {len(filtered_photos)}")
            
            # Show first few matches
            if filtered_photos:
                click.echo("\nFirst 10 matches:")
                for i, photo in enumerate(filtered_photos[:10]):
                    click.echo(f"  {photo.filename}")
                if len(filtered_photos) > 10:
                    click.echo(f"  ... and {len(filtered_photos) - 10} more")
        else:
            # Show breakdown by pattern
            wjd_count = sum(1 for photo in all_photos if photo.filename.startswith('WJD'))
            dsc_count = sum(1 for photo in all_photos if photo.filename.startswith('DSC'))
            other_count = len(all_photos) - wjd_count - dsc_count
            
            click.echo(f"\nBreakdown:")
            click.echo(f"  WJD photos: {wjd_count}")
            click.echo(f"  DSC photos: {dsc_count}")
            click.echo(f"  Other photos: {other_count}")
            
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()

@photos.command('search')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
@click.option('--rejected-only', is_flag=True, help='Show only rejected photos')
@click.option('--accepted-only', is_flag=True, help='Show only accepted photos')
@click.option('--year', type=int, help='Filter photos by year (e.g., --year 2025)')
@click.option('--this-year', is_flag=True, help='Show photos from this year (2025)')
@click.option('--date-type', type=click.Choice(['import', 'sync', 'capture']), default='import', 
              help='Which date to use for year filtering: import (when imported to Lightroom), sync (when synced to cloud), capture (when photo was taken)')
@click.option('--limit', default=100, help='Maximum number of photos to retrieve')
@click.option('--output', type=click.Choice(['table', 'json']), default='table', help='Output format')
def search_photos(catalog_id: Optional[str], rejected_only: bool, accepted_only: bool, year: Optional[int], this_year: bool, date_type: str, limit: int, output: str):
    """Search photos with filters"""
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    if rejected_only and accepted_only:
        click.echo("Cannot specify both --rejected-only and --accepted-only", err=True)
        raise click.Abort()
    
    if year and this_year:
        click.echo("Cannot specify both --year and --this-year", err=True)
        raise click.Abort()
    
    # Set year filter
    target_year = None
    if this_year:
        target_year = 2025  # Current year
        click.echo("Filtering photos from 2025...")
    elif year:
        target_year = year
        click.echo(f"Filtering photos from {year}...")
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs if catalog_id not specified
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                return
            catalog_id = catalogs[0]['id']
        
        # Build search query
        query = {
            'rejected_only': rejected_only,
            'accepted_only': accepted_only,
            'year': target_year,
            'date_type': date_type
        }
        
        # Search photos
        collection = api_client.search_photos(catalog_id, query, limit=limit)
        
        if not collection.photos:
            click.echo("No photos found matching criteria.")
            return
        
        # Format output
        if output == 'json':
            click.echo(json.dumps(collection.to_dict(), indent=2, default=str))
        else:
            click.echo(f"Found {len(collection.photos)} photos:")
            click.echo("=" * 50)
            
            for photo in collection.photos:
                status = photo.flag or "UNFLAGGED"
                click.echo(f"{photo.filename} [{status}] - {photo.id[:8]}")
                
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()


@photos.command('delete-jpg-raw-pairs')
@click.option('--directory', type=click.Path(exists=True, file_okay=False, dir_okay=True), help='Base directory containing jpg and raw subdirectories (uses config if not specified)')
@click.option('--rejected-json', default='rejected.json', type=click.Path(exists=True), help='JSON file containing rejected photos from list-rejected command (default: rejected.json)')
@click.option('--jpg-subdir', help='Name of the jpg subdirectory (uses config default if not specified)')
@click.option('--raw-subdir', help='Name of the raw subdirectory (uses config default if not specified)')
@click.option('--dry-run', is_flag=True, help='Show what would be deleted without actually deleting')
@click.option('--confirm', is_flag=True, help='Prompt for confirmation before each deletion')
@click.option('--recursive', is_flag=True, help='Search subdirectories recursively')
@click.option('--use-api-dates/--no-api-dates', default=True, help='Use API date information to find photos in jpg\\YYYY\\mm-dd directory structure (default: enabled)')
@click.option('--date-type', type=click.Choice(['import', 'capture']), default='import', help='Date type to use for directory structure: import (when imported) or capture (when photo was taken) (default: import)')
def delete_jpg_raw_pairs(directory: Optional[str], rejected_json: str, jpg_subdir: Optional[str], raw_subdir: Optional[str], dry_run: bool, confirm: bool, recursive: bool, use_api_dates: bool, date_type: str):
    """Delete rejected photo files in jpg subdirectory and matching files (.RAF extension) in raw subdirectory"""
    import os
    from pathlib import Path
    from datetime import datetime
    
    # Load configuration
    config = Config()
    
    # Use config defaults if not specified
    if not directory:
        directory = config.get_path('photo_base')
        if not directory:
            click.echo("Error: No directory specified and no photo_base configured", err=True)
            raise click.Abort()
    
    if not jpg_subdir:
        jpg_subdir = config.get_setting('paths.jpg_subdir', 'jpg')
    
    if not raw_subdir:
        raw_subdir = config.get_setting('paths.raw_subdir', 'raw')
    
    # Load rejected photos
    try:
        with open(rejected_json, 'r') as f:
            rejected_data = json.load(f)
        
        # Handle both array format and object format
        if isinstance(rejected_data, list):
            rejected_photos = rejected_data
        else:
            rejected_photos = rejected_data.get('photos', [])
            
        click.echo(f"Loaded {len(rejected_photos)} rejected photos from {rejected_json}")
    except Exception as e:
        click.echo(f"Error loading rejected photos JSON: {e}", err=True)
        raise click.Abort()
    
    # Build lookup set of rejected photos with date information
    rejected_photo_map = {}
    rejected_filenames = set()
    
    for photo in rejected_photos:
        if 'filename' in photo:
            filename = photo['filename']
            rejected_filenames.add(filename)
            
            # Extract date information for directory structure
            photo_date = None
            if use_api_dates and 'metadata' in photo:
                metadata = photo['metadata']
                if date_type == 'import' and 'import_source' in metadata and 'importTimestamp' in metadata['import_source']:
                    # Import timestamp is an ISO string
                    timestamp = metadata['import_source']['importTimestamp']
                    if isinstance(timestamp, str):
                        try:
                            photo_date = datetime.fromisoformat(timestamp.replace('Z', '+00:00'))
                        except ValueError:
                            pass
                elif date_type == 'capture' and 'capture_date' in metadata:
                    # Capture date is typically an ISO string
                    capture_date = metadata['capture_date']
                    if isinstance(capture_date, str):
                        try:
                            photo_date = datetime.fromisoformat(capture_date.replace('Z', '+00:00'))
                        except ValueError:
                            pass
            
            rejected_photo_map[filename] = {
                'photo': photo,
                'date': photo_date
            }
    
    click.echo(f"Built lookup map: {len(rejected_filenames)} rejected filenames")
    if use_api_dates:
        dated_photos = sum(1 for info in rejected_photo_map.values() if info['date'])
        click.echo(f"Found date information for {dated_photos} photos")
    
    base_path = Path(directory)
    jpg_path = base_path / jpg_subdir
    raw_path = base_path / raw_subdir
    
    # Validate paths
    if not jpg_path.exists():
        click.echo(f"Error: jpg directory not found: {jpg_path}", err=True)
        raise click.Abort()
    
    if not raw_path.exists():
        click.echo(f"Error: raw directory not found: {raw_path}", err=True)
        raise click.Abort()
    
    click.echo(f"Searching in jpg directory: {jpg_path}")
    click.echo(f"Searching in raw directory: {raw_path}")
    
    # Find rejected photo files using API date information
    rejected_jpg_files = []
    files_found_by_date = 0
    files_found_by_search = 0
    files_not_found = 0
    
    jpg_extensions = ['.jpg', '.jpeg', '.JPG', '.JPEG']
    
    for filename, photo_info in rejected_photo_map.items():
        photo_date = photo_info['date']
        found_files = []
        
        if use_api_dates and photo_date:
            # Add date-based directory structure: base_path\jpg\YYYY\mm-dd and base_path\raw\YYYY\mm-dd
            year_str = f"{photo_date.year:04d}"
            month_day_str = f"{photo_date.month:02d}-{photo_date.day:02d}"
            
            # Construct full path with date: base_path\jpg\YYYY\mm-dd
            date_jpg_path = jpg_path / year_str / month_day_str
            date_raw_path = raw_path / year_str / month_day_str
            
            if date_jpg_path.exists():
                for ext in jpg_extensions:
                    # Try exact filename match
                    base_name = Path(filename).stem
                    potential_file = date_jpg_path / f"{base_name}{ext}"
                    if potential_file.exists():
                        found_files.append((potential_file, date_raw_path))
                        break
                    
                    # Also try the original filename with different extension
                    if filename.lower().endswith(('.jpg', '.jpeg')):
                        potential_file = date_jpg_path / filename
                        if potential_file.exists():
                            found_files.append((potential_file, date_raw_path))
                            break
            
            if found_files:
                files_found_by_date += 1
        
        # If not found by date, fall back to recursive search
        if not found_files:
            jpg_pattern = "**/*" if recursive else "*"
            
            for ext in jpg_extensions:
                # Search for files matching the base name
                base_name = Path(filename).stem
                matching_files = list(jpg_path.glob(f"{jpg_pattern}{base_name}{ext}"))
                
                # Also search for exact filename matches
                if not matching_files:
                    matching_files = list(jpg_path.glob(f"{jpg_pattern}{filename}"))
                
                for jpg_file in matching_files:
                    # Determine corresponding raw path (maintain directory structure)
                    relative_path = jpg_file.relative_to(jpg_path)
                    corresponding_raw_path = raw_path / relative_path.parent
                    found_files.append((jpg_file, corresponding_raw_path))
                
                if matching_files:
                    break
            
            if found_files:
                files_found_by_search += 1
        
        if found_files:
            for jpg_file, raw_dir in found_files:
                rejected_jpg_files.append((jpg_file, raw_dir, photo_info))
        else:
            files_not_found += 1
            if not use_api_dates or not photo_date:
                click.echo(f"  File not found: {filename} (no date info)")
            else:
                expected_path = jpg_path / f"{photo_date.year:04d}" / f"{photo_date.month:02d}-{photo_date.day:02d}"
                click.echo(f"  File not found: {filename} (expected in {expected_path})")
    
    click.echo(f"Found {len(rejected_jpg_files)} rejected jpg files to process")
    if use_api_dates:
        click.echo(f"  - {files_found_by_date} found using date-based directories")
        click.echo(f"  - {files_found_by_search} found using recursive search")
        click.echo(f"  - {files_not_found} not found")
    
    if not rejected_jpg_files:
        click.echo("No rejected jpg files found in directory.")
        return
    
    # Track deletion statistics
    jpg_deleted = 0
    raw_deleted = 0
    jpg_skipped = 0
    raw_skipped = 0
    raw_not_found = 0
    
    for jpg_file, raw_dir, photo_info in rejected_jpg_files:
        photo_date = photo_info['date']
        
        # Get the base filename for matching raw files
        base_name = jpg_file.stem
        
        # Find matching raw file (only .RAF extension) in the corresponding raw directory
        raw_file = raw_dir / f"{base_name}.RAF"
        
        click.echo(f"\nProcessing: {jpg_file.name}")
        click.echo(f"  JPG: {jpg_file}")
        if photo_date and use_api_dates:
            click.echo(f"  Date: {photo_date.strftime('%Y-%m-%d')} ({date_type} date)")
        
        # Check if raw file exists
        if raw_file.exists():
            click.echo(f"  RAW: {raw_file}")
        else:
            click.echo(f"  RAW: {raw_file} (not found)")
            raw_not_found += 1
        
        if dry_run:
            click.echo("  [DRY RUN] Would delete jpg file")
            if raw_file.exists():
                click.echo("  [DRY RUN] Would delete raw file")
            continue
        
        should_delete = True
        if confirm:
            msg = f"Delete {jpg_file.name}"
            if raw_file.exists():
                msg += f" and {raw_file.name}"
            msg += "?"
            should_delete = click.confirm(msg)
        
        if should_delete:
            # Delete jpg file
            try:
                jpg_file.unlink()
                click.echo("  âœ“ Deleted jpg file")
                jpg_deleted += 1
            except Exception as e:
                click.echo(f"  âœ— Error deleting jpg file: {e}")
                jpg_skipped += 1
            
            # Delete raw file if it exists
            if raw_file.exists():
                try:
                    raw_file.unlink()
                    click.echo("  âœ“ Deleted raw file")
                    raw_deleted += 1
                except Exception as e:
                    click.echo(f"  âœ— Error deleting raw file: {e}")
                    raw_skipped += 1
        else:
            click.echo("  - Skipped")
            jpg_skipped += 1
            if raw_file.exists():
                raw_skipped += 1
    
    # Summary
    if dry_run:
        raw_would_delete = len(rejected_jpg_files) - raw_not_found
        summary = f"\n[DRY RUN] Would delete {len(rejected_jpg_files)} rejected jpg files and {raw_would_delete} matching raw files."
        if raw_not_found > 0:
            summary += f" ({raw_not_found} raw files not found)"
        click.echo(summary)
    else:
        summary = f"\nCompleted: {jpg_deleted} rejected jpg files deleted, {raw_deleted} raw files deleted."
        if jpg_skipped > 0 or raw_skipped > 0:
            summary += f" Skipped: {jpg_skipped} jpg, {raw_skipped} raw."
        if raw_not_found > 0:
            summary += f" ({raw_not_found} raw files not found)"
        click.echo(summary)
