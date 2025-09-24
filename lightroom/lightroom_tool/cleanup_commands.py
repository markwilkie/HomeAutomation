"""
Cleanup command for automated rejected photo management
"""

import click
import json
import os
from datetime import datetime
from pathlib import Path

from lightroom_tool.config import Config
from lightroom_tool.photos.api_client import LightroomAPIClient
from lightroom_tool.auth.token_manager import TokenManager
from lightroom_tool.photos.photo_commands import delete_jpg_raw_pairs


@click.command()
def cleanup():
    """Complete cleanup workflow: create rejected.json, preview deletions, and delete with confirmation
    
    This command performs the full cleanup workflow:
    1. Creates rejected.json by fetching rejected photos from Lightroom Cloud
    2. Shows what files would be deleted (dry-run)
    3. Asks for confirmation
    4. Deletes the files if confirmed
    
    No parameters needed - uses configuration defaults.
    """
    
    config = Config()
    token_manager = TokenManager(config)
    
    # Check authentication first
    if not token_manager.get_valid_access_token():
        click.echo("❌ Not authenticated. Please run 'lightroom-tool auth login' first.", err=True)
        raise click.Abort()
    
    click.echo("🧹 Adobe Lightroom Cleanup Workflow")
    click.echo("=" * 50)
    
    # Step 1: Create rejected.json
    click.echo("\n📋 Step 1: Fetching rejected photos from Lightroom Cloud...")
    
    try:
        api_client = LightroomAPIClient(config)
        
        # Get catalogs
        catalogs = api_client.get_catalogs()
        if not catalogs:
            click.echo("❌ No catalogs found.", err=True)
            raise click.Abort()
        
        # Use first catalog by default
        catalog_id = catalogs[0]['id']
        catalog_name = catalogs[0].get('name', catalog_id)
        click.echo(f"   Using catalog: {catalog_name}")
        
        # Get rejected photos
        rejected_photos = api_client.get_rejected_photos(catalog_id, limit=1000)
        
        if not rejected_photos:
            click.echo("✅ No rejected photos found. Nothing to clean up!")
            return
        
        # Save to rejected.json
        rejected_json_path = "rejected.json"
        rejected_data = [photo.to_dict() for photo in rejected_photos]
        
        with open(rejected_json_path, 'w', encoding='utf-8') as f:
            json.dump(rejected_data, f, indent=2)
        
        click.echo(f"✅ Saved {len(rejected_photos)} rejected photos to {rejected_json_path}")
        
    except Exception as e:
        click.echo(f"❌ Error fetching rejected photos: {e}", err=True)
        raise click.Abort()
    
    # Step 2: Show dry-run preview
    click.echo("\n🔍 Step 2: Previewing what would be deleted...")
    click.echo("-" * 30)
    
    # Use config defaults for paths
    directory = config.get_path('photo_base')
    if not directory:
        click.echo("❌ No photo_base configured in config.yaml", err=True)
        raise click.Abort()
    
    jpg_subdir = config.get_setting('paths.jpg_subdir', 'jpg')
    raw_subdir = config.get_setting('paths.raw_subdir', 'raw')
    
    try:
        # Call the delete function in dry-run mode by importing the logic
        from lightroom_tool.photos.photo_commands import delete_jpg_raw_pairs
        
        # Create a context to simulate the CLI call
        ctx = click.Context(delete_jpg_raw_pairs)
        ctx.params = {
            'directory': directory,
            'rejected_json': rejected_json_path,
            'jpg_subdir': jpg_subdir,
            'raw_subdir': raw_subdir,
            'dry_run': True,  # Always dry-run first
            'confirm': False,
            'recursive': False,
            'use_api_dates': True,
            'date_type': 'import'
        }
        
        # This is a bit tricky - we need to call the function with the right parameters
        # Let's simulate the dry run by calling the underlying logic directly
        _run_delete_operation(directory, rejected_json_path, jpg_subdir, raw_subdir, dry_run=True)
        
    except Exception as e:
        click.echo(f"❌ Error during dry-run preview: {e}", err=True)
        raise click.Abort()
    
    # Step 3: Ask for confirmation
    click.echo("\n❓ Step 3: Confirmation")
    click.echo("-" * 20)
    
    if not click.confirm("Do you want to proceed with deleting these files?"):
        click.echo("❌ Operation cancelled by user.")
        return
    
    # Step 4: Actually delete the files
    click.echo("\n🗑️  Step 4: Deleting files...")
    click.echo("-" * 25)
    
    try:
        _run_delete_operation(directory, rejected_json_path, jpg_subdir, raw_subdir, dry_run=False)
        click.echo("\n✅ Cleanup completed successfully!")
        
    except Exception as e:
        click.echo(f"❌ Error during file deletion: {e}", err=True)
        raise click.Abort()


def _run_delete_operation(directory, rejected_json, jpg_subdir, raw_subdir, dry_run=True):
    """Helper function to run the delete operation with specified parameters"""
    import os
    from pathlib import Path
    from datetime import datetime
    
    # Load configuration
    config = Config()
    
    # Load rejected photos
    with open(rejected_json, 'r') as f:
        rejected_data = json.load(f)
    
    # Handle both array format and object format
    if isinstance(rejected_data, list):
        rejected_photos = rejected_data
    else:
        rejected_photos = rejected_data.get('photos', [])
    
    click.echo(f"Loaded {len(rejected_photos)} rejected photos from {rejected_json}")
    
    # Build lookup set of rejected photos with date information
    rejected_photo_map = {}
    rejected_filenames = set()
    use_api_dates = True
    date_type = 'import'
    
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
    
    # Track files by directory for summary
    found_by_directory = {}
    not_found_by_directory = {}
    
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
                # Track by directory with jpg and raw counts
                dir_key = f"{year_str}\\{month_day_str}"
                if dir_key not in found_by_directory:
                    found_by_directory[dir_key] = {'jpg': 0, 'raw': 0}
                found_by_directory[dir_key]['jpg'] += 1
                
                # Check if corresponding raw file exists
                base_name = Path(filename).stem
                raw_file = date_raw_path / f"{base_name}.RAF"
                if raw_file.exists():
                    found_by_directory[dir_key]['raw'] += 1
            else:
                # Track not found by directory
                dir_key = f"{year_str}\\{month_day_str}"
                if dir_key not in not_found_by_directory:
                    not_found_by_directory[dir_key] = 0
                not_found_by_directory[dir_key] += 1
        
        # If not found by date, fall back to recursive search
        if not found_files:
            jpg_pattern = "**/*"  # Always use recursive for cleanup
            
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
    
    # Display directory summaries
    if found_by_directory:
        click.echo("📁 Files found by directory:")
        for dir_path, counts in sorted(found_by_directory.items()):
            jpg_count = counts['jpg']
            raw_count = counts['raw']
            click.echo(f"   {dir_path}: {jpg_count} jpg, {raw_count} raw")
    
    if not_found_by_directory:
        click.echo("❌ Files not found by directory:")
        for dir_path, count in sorted(not_found_by_directory.items()):
            click.echo(f"   {dir_path}: {count} files")
    
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
        
        # For actual deletion, we always confirm each file in cleanup mode
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