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


@photos.command('test-pagination')
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
def test_timestamp_pagination(catalog_id: Optional[str]):
    """Test the timestamp-based pagination to get ALL photos"""
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
        
        click.echo(f"Testing timestamp-based pagination for catalog: {catalog_id}")
        click.echo("=" * 60)
        
        # Fetch ALL photos using timestamp pagination
        all_photos = []
        cursor = None
        page_count = 0
        
        while True:
            page_count += 1
            click.echo(f"Fetching page {page_count}...")
            
            try:
                collection = api_client.get_assets(catalog_id, limit=500, updated_since=cursor)
                all_photos.extend(collection.photos)
                
                click.echo(f"  Got {len(collection.photos)} photos, total so far: {len(all_photos)}")
                click.echo(f"  Has more: {collection.has_more}")
                click.echo(f"  Next cursor (updated_since): {collection.next_cursor}")
                
                # Check if we have more pages
                if not collection.has_more or not collection.next_cursor:
                    break
                    
                cursor = collection.next_cursor
            except Exception as e:
                click.echo(f"  Error on page {page_count}: {str(e)}")
                break
                
            # Safety break to avoid infinite loops
            if page_count > 20:
                click.echo("  Stopping after 20 pages for safety")
                break
        
        click.echo(f"\nTotal photos found via timestamp pagination: {len(all_photos)}")
        
        # Count WJD photos
        wjd_photos = [photo for photo in all_photos if photo.filename.startswith('WJD')]
        dsc_photos = [photo for photo in all_photos if photo.filename.startswith('DSC')]
        other_photos = len(all_photos) - len(wjd_photos) - len(dsc_photos)
        
        click.echo(f"\nBreakdown:")
        click.echo(f"  WJD photos: {len(wjd_photos)}")
        click.echo(f"  DSC photos: {len(dsc_photos)}")
        click.echo(f"  Other photos: {other_photos}")
        
        if wjd_photos:
            click.echo(f"\nWJD photo filename range:")
            wjd_filenames = sorted([photo.filename for photo in wjd_photos])
            click.echo(f"  First: {wjd_filenames[0]}")
            click.echo(f"  Last: {wjd_filenames[-1]}")
            
            # Show if we now have the missing photos
            if len(wjd_photos) > 75:
                click.echo(f"\n🎉 SUCCESS! Found {len(wjd_photos)} WJD photos (vs. previous 75)")
                extra_photos = len(wjd_photos) - 75
                click.echo(f"   That's {extra_photos} additional WJD photos!")
            else:
                click.echo(f"\n❌ Still only found {len(wjd_photos)} WJD photos")
                
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
def debug_api_response(catalog_id: Optional[str]):
    """Debug raw API response to understand what's being returned"""
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
        
        click.echo(f"Debugging API response for catalog: {catalog_id}")
        click.echo("=" * 60)
        
        # Get raw API response
        raw_response = api_client.debug_raw_api_response(catalog_id, limit=5)
        
        click.echo(f"Response keys: {list(raw_response.keys())}")
        click.echo(f"Total resources in this response: {len(raw_response.get('resources', []))}")
        click.echo(f"Total count from API: {raw_response.get('total_count', 'Not provided')}")
        click.echo(f"Has more: {raw_response.get('has_more', 'Not provided')}")
        click.echo(f"Next cursor: {raw_response.get('next_cursor', 'Not provided')}")
        
        # Check the links field for pagination info
        links = raw_response.get('links', {})
        if links:
            click.echo(f"Links: {links}")
        
        # Check if there's pagination info at the top level
        click.echo(f"Full response keys: {raw_response.keys()}")
        for key in raw_response.keys():
            if 'page' in key.lower() or 'cursor' in key.lower() or 'next' in key.lower():
                click.echo(f"{key}: {raw_response[key]}")
        
        # Show first resource structure
        resources = raw_response.get('resources', [])
        if resources:
            click.echo(f"\nFirst resource keys: {list(resources[0].keys())}")
            
            # Look for any filtering hints
            first_resource = resources[0]
            if 'payload' in first_resource:
                payload = first_resource['payload']
                click.echo(f"Payload keys: {list(payload.keys())}")
                
                # Check for state information
                if 'state' in payload:
                    click.echo(f"State: {payload['state']}")
                if 'hidden' in payload:
                    click.echo(f"Hidden: {payload['hidden']}")
                if 'reviews' in payload:
                    reviews = payload['reviews']
                    if reviews:
                        for user_id, review in reviews.items():
                            flag = review.get('flag')
                            if flag:
                                click.echo(f"Review flag: {flag}")
                            break  # Just show first review
                    
            # Show the filename to verify it's a WJD photo
            if 'asset' in first_resource and 'payload' in first_resource['asset']:
                asset_payload = first_resource['asset']['payload']
                filename = asset_payload.get('name', 'Unknown')
                click.echo(f"Sample filename: {filename}")
                
        # Check the last_updated field which might have pagination hints
        last_updated = raw_response.get('last_updated')
        if last_updated:
            click.echo(f"Last updated: {last_updated}")
        
        # Test with different parameters to see if we can get more photos
        click.echo(f"\nTesting with various parameters:")
        
        # Test 1: Higher limit
        for test_limit in [500, 1000, 2000]:
            try:
                response = api_client.debug_raw_api_response(catalog_id, limit=test_limit)
                total_resources = len(response.get('resources', []))
                click.echo(f"  Limit {test_limit}: Got {total_resources} resources")
            except Exception as e:
                click.echo(f"  Limit {test_limit}: Error - {str(e)}")
        
    except Exception as e:
        click.echo(f"Error: {e}", err=True)
        raise click.Abort()
@click.option('--catalog-id', help='Specific catalog ID to search (optional)')
@click.option('--pattern', help='Filter by filename pattern (e.g., WJD, DSC)')
def test_assets_photos_endpoint(catalog_id: Optional[str], pattern: Optional[str]):
    """Test different asset endpoints to see if they return more photos"""
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
        
        click.echo(f"Testing different asset endpoints for catalog: {catalog_id}")
        click.echo("=" * 60)
        
        # Test 1: Regular assets endpoint
        click.echo("\n1. Regular /assets endpoint:")
        try:
            regular_collection = api_client.get_assets(catalog_id, limit=500)
            regular_wjd = sum(1 for photo in regular_collection.photos if photo.filename.startswith('WJD'))
            click.echo(f"  Total: {len(regular_collection.photos)}, WJD: {regular_wjd}")
        except Exception as e:
            click.echo(f"  Error: {str(e)}")
        
        # Test 2: Assets with type=photos parameter
        click.echo("\n2. /assets with type=photos parameter:")
        try:
            photos_collection = api_client.get_assets_photos(catalog_id, limit=500)
            photos_wjd = sum(1 for photo in photos_collection.photos if photo.filename.startswith('WJD'))
            click.echo(f"  Total: {len(photos_collection.photos)}, WJD: {photos_wjd}")
        except Exception as e:
            click.echo(f"  Error: {str(e)}")
        
        # Test 3: Assets with comprehensive embed
        click.echo("\n3. /assets with comprehensive embed:")
        try:
            all_types_collection = api_client.get_assets_all_types(catalog_id, limit=500)
            all_types_wjd = sum(1 for photo in all_types_collection.photos if photo.filename.startswith('WJD'))
            click.echo(f"  Total: {len(all_types_collection.photos)}, WJD: {all_types_wjd}")
        except Exception as e:
            click.echo(f"  Error: {str(e)}")
        
        # Test 4: Different limit values with regular endpoint
        click.echo("\n4. Testing different limits with regular endpoint:")
        for test_limit in [100, 200, 500, 1000, 2000]:
            try:
                test_collection = api_client.get_assets(catalog_id, limit=test_limit)
                test_wjd = sum(1 for photo in test_collection.photos if photo.filename.startswith('WJD'))
                click.echo(f"  Limit {test_limit}: Total {len(test_collection.photos)}, WJD {test_wjd}, has_more: {test_collection.has_more}")
            except Exception as e:
                click.echo(f"  Limit {test_limit}: Error: {str(e)}")
                
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


@photos.command('delete-rejected-local')
@click.option('--rejected-json', required=True, type=click.Path(exists=True), help='Path to JSON file containing rejected photos')
@click.option('--search-paths', multiple=True, help='Local directories to search for photos (can specify multiple)')
@click.option('--dry-run', is_flag=True, help='Show what would be deleted without actually deleting')
@click.option('--confirm', is_flag=True, help='Confirm each deletion individually')
@click.option('--recursive', is_flag=True, help='Search subdirectories recursively')
@click.option('--delete-from-cloud', is_flag=True, help='Also delete photos from Lightroom Cloud')
@click.option('--catalog-id', help='Specific catalog ID (required if deleting from cloud)')
@click.option('--filename-only', is_flag=True, help='Match by filename only (faster for network drives, less accurate)')
def delete_rejected_local(rejected_json: str, search_paths: tuple, dry_run: bool, confirm: bool, recursive: bool, delete_from_cloud: bool, catalog_id: Optional[str], filename_only: bool):
    """Find and delete local files matching rejected photos from Lightroom Cloud"""
    import json
    import os
    import hashlib
    from pathlib import Path
    
    # Initialize API client if deleting from cloud
    api_client = None
    if delete_from_cloud:
        config = Config()
        token_manager = TokenManager(config)
        
        # Check authentication
        if not token_manager.get_valid_access_token():
            click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
            raise click.Abort()
            
        api_client = LightroomAPIClient(config)
        
        # Get catalog ID if not provided
        if not catalog_id:
            catalogs = api_client.get_catalogs()
            if not catalogs:
                click.echo("No catalogs found.")
                raise click.Abort()
            catalog_id = catalogs[0]['id']
            click.echo(f"Using catalog: {catalogs[0].get('name', catalog_id)}")
    
    if not search_paths and not delete_from_cloud:
        click.echo("Error: At least one search path must be specified with --search-paths, or use --delete-from-cloud", err=True)
        raise click.Abort()
    
    # Load rejected photos
    try:
        with open(rejected_json, 'r') as f:
            rejected_data = json.load(f)
            if isinstance(rejected_data, dict) and 'photos' in rejected_data:
                rejected_photos = rejected_data['photos']
            elif isinstance(rejected_data, list):
                rejected_photos = rejected_data
            else:
                click.echo("Error: Invalid JSON format. Expected list of photos or object with 'photos' key.", err=True)
                raise click.Abort()
    except Exception as e:
        click.echo(f"Error reading rejected photos file: {e}", err=True)
        raise click.Abort()
    
    if not rejected_photos:
        click.echo("No rejected photos found in file.")
        return
    
    click.echo(f"Loaded {len(rejected_photos)} rejected photos from {rejected_json}")
    
    # Build lookup dictionaries for matching (including file sizes for faster filtering)
    rejected_by_filename = {}
    rejected_by_sha256 = {}
    rejected_by_size = {}  # Map file size to list of photos
    
    for photo in rejected_photos:
        filename = photo.get('filename') or photo.get('metadata', {}).get('original_filename')
        sha256 = photo.get('metadata', {}).get('sha256')
        file_size = photo.get('file_size') or photo.get('metadata', {}).get('fileSize')
        
        if filename:
            rejected_by_filename[filename.lower()] = photo
        if sha256:
            rejected_by_sha256[sha256] = photo
        if file_size:
            if file_size not in rejected_by_size:
                rejected_by_size[file_size] = []
            rejected_by_size[file_size].append(photo)
    
    click.echo(f"Built lookup tables: {len(rejected_by_filename)} filenames, {len(rejected_by_sha256)} hashes, {len(rejected_by_size)} file sizes")
    
    # Search for local files (if search paths provided)
    found_files = []
    
    if search_paths:
        for search_path in search_paths:
            path = Path(search_path)
            if not path.exists():
                click.echo(f"Warning: Search path does not exist: {search_path}")
                continue
                
            click.echo(f"Searching in: {search_path}")
            
            if recursive:
                pattern = "**/*"
            else:
                pattern = "*"
            
            # Collect all files first to show progress
            all_files = []
            click.echo("  Collecting file list...")
            for file_path in path.glob(pattern):
                if file_path.is_file():
                    all_files.append(file_path)
            
            click.echo(f"  Found {len(all_files)} files to check")
            
            # Process files with progress reporting (filename matching only for speed)
            processed = 0
            matched_by_filename = 0
            
            for file_path in all_files:
                processed += 1
                if processed % 100 == 0:
                    click.echo(f"  Progress: {processed}/{len(all_files)} files checked")
                
                # Match by filename only (fastest - no file I/O)
                filename = file_path.name.lower()
                matched_photo = rejected_by_filename.get(filename)
                
                if matched_photo:
                    found_files.append((file_path, matched_photo, 'filename'))
                    matched_by_filename += 1
            
            click.echo(f"  Completed: {matched_by_filename} files matched by filename")
    
    # Handle cloud-only deletion if no local files specified
    if not search_paths and delete_from_cloud:
        click.echo(f"\nPreparing to delete {len(rejected_photos)} rejected photos from Lightroom Cloud:")
        
        if dry_run:
            for photo in rejected_photos:
                photo_id = photo.get('id', 'unknown')[:8]
                filename = photo.get('filename', 'unknown')
                click.echo(f"  [DRY RUN] Would delete from cloud: {filename} (ID: {photo_id})")
            click.echo(f"\n[DRY RUN] Would delete {len(rejected_photos)} photos from Lightroom Cloud.")
            return
        
        # Delete from cloud
        cloud_deleted = 0
        cloud_failed = 0
        
        for photo in rejected_photos:
            photo_id = photo.get('id')
            filename = photo.get('filename', 'unknown')
            
            if not photo_id:
                click.echo(f"  ✗ No ID for photo {filename}, skipping")
                cloud_failed += 1
                continue
                
            should_delete = True
            if confirm:
                should_delete = click.confirm(f"Delete from cloud: {filename} (ID: {photo_id[:8]})?")
            
            if should_delete:
                if api_client.delete_photo(catalog_id, photo_id):
                    click.echo(f"  ✓ Deleted from cloud: {filename}")
                    cloud_deleted += 1
                else:
                    click.echo(f"  ✗ Failed to delete from cloud: {filename}")
                    cloud_failed += 1
            else:
                click.echo(f"  - Skipped cloud deletion: {filename}")
                cloud_failed += 1
        
        click.echo(f"\nCloud deletion completed: {cloud_deleted} deleted, {cloud_failed} skipped/failed.")
        return
    
    if not found_files and search_paths:
        click.echo("No local files found matching rejected photos.")
        if not delete_from_cloud:
            return
    
    if found_files:
        click.echo(f"\nFound {len(found_files)} local files matching rejected photos:")
    
    deleted_count = 0
    skipped_count = 0
    cloud_deleted = 0
    cloud_failed = 0
    
    for file_path, photo, match_type in found_files:
        file_size = file_path.stat().st_size
        photo_id = photo.get('id', 'unknown')[:8]
        full_photo_id = photo.get('id')
        filename = photo.get('filename', 'unknown')
        
        click.echo(f"\nFile: {file_path}")
        click.echo(f"  Size: {file_size:,} bytes")
        click.echo(f"  Matched by: {match_type}")
        click.echo(f"  Lightroom ID: {photo_id}")
        
        if dry_run:
            click.echo("  [DRY RUN] Would delete local file")
            if delete_from_cloud:
                click.echo("  [DRY RUN] Would delete from Lightroom Cloud")
            continue
        
        should_delete = True
        if confirm:
            msg = f"Delete local file: {file_path.name}"
            if delete_from_cloud:
                msg += " and from Lightroom Cloud"
            msg += "?"
            should_delete = click.confirm(msg)
        
        if should_delete:
            # Delete local file
            local_deleted = False
            try:
                file_path.unlink()
                click.echo("  ✓ Deleted local file")
                deleted_count += 1
                local_deleted = True
            except Exception as e:
                click.echo(f"  ✗ Error deleting local file: {e}")
                skipped_count += 1
            
            # Delete from cloud if requested and local deletion succeeded
            if delete_from_cloud and local_deleted and full_photo_id:
                if api_client.delete_photo(catalog_id, full_photo_id):
                    click.echo("  ✓ Deleted from Lightroom Cloud")
                    cloud_deleted += 1
                else:
                    click.echo("  ✗ Failed to delete from Lightroom Cloud")
                    cloud_failed += 1
            elif delete_from_cloud and not full_photo_id:
                click.echo("  ✗ No photo ID for cloud deletion")
                cloud_failed += 1
                
        else:
            click.echo("  - Skipped")
            skipped_count += 1
    
    # Summary
    if dry_run:
        summary = f"\n[DRY RUN] Found {len(found_files)} local files that would be deleted."
        if delete_from_cloud:
            summary += f" Would also delete {len(found_files)} photos from Lightroom Cloud."
        click.echo(summary)
    else:
        summary = f"\nCompleted: {deleted_count} local files deleted, {skipped_count} skipped."
        if delete_from_cloud:
            summary += f" Cloud: {cloud_deleted} deleted, {cloud_failed} failed."
        click.echo(summary)


@photos.command('delete-jpg-raw-pairs')
@click.option('--directory', required=True, type=click.Path(exists=True, file_okay=False, dir_okay=True), help='Base directory containing jpg and raw subdirectories')
@click.option('--rejected-json', required=True, type=click.Path(exists=True), help='JSON file containing rejected photos from list-rejected command')
@click.option('--jpg-subdir', default='jpg', help='Name of the jpg subdirectory (default: jpg)')
@click.option('--raw-subdir', default='raw', help='Name of the raw subdirectory (default: raw)')
@click.option('--dry-run', is_flag=True, help='Show what would be deleted without actually deleting')
@click.option('--confirm', is_flag=True, help='Prompt for confirmation before each deletion')
@click.option('--recursive', is_flag=True, help='Search subdirectories recursively')
def delete_jpg_raw_pairs(directory: str, rejected_json: str, jpg_subdir: str, raw_subdir: str, dry_run: bool, confirm: bool, recursive: bool):
    """Delete rejected photo files in jpg subdirectory and matching files (.RAF extension) in raw subdirectory"""
    import os
    from pathlib import Path
    
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
    
    # Build lookup set of rejected filenames (without path, just the filename)
    rejected_filenames = set()
    for photo in rejected_photos:
        if 'filename' in photo:
            rejected_filenames.add(photo['filename'])
    
    click.echo(f"Built lookup set: {len(rejected_filenames)} rejected filenames")
    
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
    
    # Find all jpg files
    jpg_pattern = "**/*" if recursive else "*"
    jpg_extensions = ['.jpg', '.jpeg', '.JPG', '.JPEG']
    
    jpg_files = set()  # Use set to avoid duplicates
    for ext in jpg_extensions:
        jpg_files.update(jpg_path.glob(f"{jpg_pattern}{ext}"))
    
    jpg_files = list(jpg_files)  # Convert back to list
    
    if not jpg_files:
        click.echo("No jpg files found.")
        return
    
    click.echo(f"Found {len(jpg_files)} jpg files")
    
    # Filter to only rejected files
    rejected_jpg_files = []
    for jpg_file in jpg_files:
        if jpg_file.name in rejected_filenames:
            rejected_jpg_files.append(jpg_file)
    
    if not rejected_jpg_files:
        click.echo("No rejected jpg files found in directory.")
        return
    
    click.echo(f"Found {len(rejected_jpg_files)} rejected jpg files to process")
    
    # Track deletion statistics
    jpg_deleted = 0
    raw_deleted = 0
    jpg_skipped = 0
    raw_skipped = 0
    raw_not_found = 0
    
    for jpg_file in rejected_jpg_files:
        # Get the relative path from jpg_path to maintain directory structure
        relative_path = jpg_file.relative_to(jpg_path)
        
        # Remove extension to get base filename
        base_name = relative_path.with_suffix('')
        
        # Find matching raw file (only .RAF extension)
        raw_file = raw_path / f"{base_name}.RAF"
        
        click.echo(f"\nProcessing: {jpg_file.name}")
        click.echo(f"  JPG: {jpg_file}")
        
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
                click.echo("  ✓ Deleted jpg file")
                jpg_deleted += 1
            except Exception as e:
                click.echo(f"  ✗ Error deleting jpg file: {e}")
                jpg_skipped += 1
            
            # Delete raw file if it exists
            if raw_file.exists():
                try:
                    raw_file.unlink()
                    click.echo("  ✓ Deleted raw file")
                    raw_deleted += 1
                except Exception as e:
                    click.echo(f"  ✗ Error deleting raw file: {e}")
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


@photos.command('delete-rejected-cloud')
@click.option('--rejected-json', required=True, type=click.Path(exists=True), help='JSON file containing rejected photos from list-rejected command')
@click.option('--catalog-id', help='Specific catalog ID (optional, will use default if not specified)')
@click.option('--dry-run', is_flag=True, help='Show what would be deleted without actually deleting')
@click.option('--save-to', type=click.Path(), help='Save dry-run results to file')
@click.option('--confirm', is_flag=True, help='Prompt for confirmation before each deletion')
def delete_rejected_cloud(rejected_json: str, catalog_id: Optional[str], dry_run: bool, save_to: Optional[str], confirm: bool):
    """Delete rejected photos from Lightroom Cloud using rejected JSON file"""
    
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
    
    if not rejected_photos:
        click.echo("No rejected photos found in JSON file.")
        return
    
    # Setup authentication and API client
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication
    if not token_manager.get_valid_access_token():
        click.echo("Not authenticated. Run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    api_client = LightroomAPIClient(config)
    
    # Get catalog ID if not specified
    if not catalog_id:
        catalogs = api_client.get_catalogs()
        if not catalogs:
            click.echo("No catalogs found.")
            raise click.Abort()
        catalog_id = catalogs[0]['id']
        click.echo(f"Using catalog: {catalog_id}")
    
    # Collect output for saving to file
    output_lines = []
    if save_to:
        output_lines.append(f"Loaded {len(rejected_photos)} rejected photos from {rejected_json}")
        output_lines.append(f"Using catalog: {catalog_id}")
        output_lines.append("")
    
    # Track deletion statistics
    deleted_count = 0
    skipped_count = 0
    error_count = 0
    
    for i, photo in enumerate(rejected_photos, 1):
        photo_id = photo.get('id')
        filename = photo.get('filename', 'Unknown')
        
        if not photo_id:
            click.echo(f"[{i}/{len(rejected_photos)}] Skipping {filename}: No photo ID")
            if save_to:
                output_lines.append(f"[{i}/{len(rejected_photos)}] Skipping {filename}: No photo ID")
            skipped_count += 1
            continue
        
        click.echo(f"[{i}/{len(rejected_photos)}] Processing: {filename} (ID: {photo_id})")
        if save_to:
            output_lines.append(f"[{i}/{len(rejected_photos)}] Processing: {filename} (ID: {photo_id})")
        
        if dry_run:
            click.echo("  [DRY RUN] Would delete from Lightroom Cloud")
            if save_to:
                output_lines.append("  [DRY RUN] Would delete from Lightroom Cloud")
            continue
        
        should_delete = True
        if confirm:
            should_delete = click.confirm(f"Delete {filename} from Lightroom Cloud?")
        
        if should_delete:
            try:
                if api_client.delete_photo(catalog_id, photo_id):
                    click.echo("  ✓ Deleted from Lightroom Cloud")
                    if save_to:
                        output_lines.append("  ✓ Deleted from Lightroom Cloud")
                    deleted_count += 1
                else:
                    click.echo("  ✗ Failed to delete from Lightroom Cloud")
                    if save_to:
                        output_lines.append("  ✗ Failed to delete from Lightroom Cloud")
                    error_count += 1
            except Exception as e:
                click.echo(f"  ✗ Error deleting from Lightroom Cloud: {e}")
                if save_to:
                    output_lines.append(f"  ✗ Error deleting from Lightroom Cloud: {e}")
                error_count += 1
        else:
            click.echo("  - Skipped")
            if save_to:
                output_lines.append("  - Skipped")
            skipped_count += 1
    
    # Summary
    if dry_run:
        summary = f"\n[DRY RUN] Would delete {len(rejected_photos)} photos from Lightroom Cloud."
        click.echo(summary)
        
        # Save to file if requested
        if save_to:
            output_lines.append(summary.strip())
            try:
                with open(save_to, 'w') as f:
                    f.write('\n'.join(output_lines))
                click.echo(f"\nDry-run results saved to: {save_to}")
            except Exception as e:
                click.echo(f"Error saving to file: {e}", err=True)
    else:
        summary = f"\nCompleted: {deleted_count} photos deleted from Lightroom Cloud, {skipped_count} skipped, {error_count} errors."
        click.echo(summary)
