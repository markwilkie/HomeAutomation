"""
API client for Adobe Lightroom Cloud
"""

import requests
from typing import List, Optional, Dict, Any
from urllib.parse import urlencode

from lightroom_tool.config import Config
from lightroom_tool.auth.token_manager import TokenManager
from lightroom_tool.photos.models import Photo, PhotoCollection


class LightroomAPIClient:
    """Client for Adobe Lightroom Cloud API"""
    
    BASE_URL = "https://lr.adobe.io"
    API_VERSION = "v2"
    
    def __init__(self, config: Config):
        self.config = config
        self.token_manager = TokenManager(config)
        self._session = requests.Session()
    
    def _get_headers(self) -> Dict[str, str]:
        """Get HTTP headers with authentication"""
        access_token = self.token_manager.get_valid_access_token()
        
        if not access_token:
            raise Exception("No valid access token. Please authenticate first.")
        
        return {
            'Authorization': f'Bearer {access_token}',
            'X-API-Key': self.config.adobe_client_id,
            'Content-Type': 'application/json'
        }
    
    def _make_request(self, method: str, endpoint: str, **kwargs) -> Dict[str, Any]:
        """Make authenticated API request"""
        url = f"{self.BASE_URL}/{self.API_VERSION}/{endpoint}"
        headers = self._get_headers()
        headers['Accept'] = 'application/json'  # Ensure we request JSON
        
        response = self._session.request(
            method=method,
            url=url,
            headers=headers,
            **kwargs
        )
        
        try:
            response.raise_for_status()
        except requests.exceptions.HTTPError as e:
            if response.status_code == 401:
                raise Exception("Authentication failed. Please re-authenticate.")
            elif response.status_code == 403:
                raise Exception("Access forbidden. Check your API permissions.")
            elif response.status_code == 404:
                raise Exception(f"API endpoint not found: {url}")
            elif response.status_code == 429:
                raise Exception("API rate limit exceeded. Please try again later.")
            else:
                raise Exception(f"API request failed: {e}")
        
        # Try to parse JSON, with better error handling
        if not response.text.strip():
            raise Exception(f"Empty response from API endpoint: {url}")
        
        # Adobe API returns responses with JSONP padding "while (1) {}" - strip it
        json_text = response.text
        if json_text.startswith('while (1) {}'):
            json_text = json_text[12:].strip()  # Remove "while (1) {}" prefix
        
        try:
            import json
            return json.loads(json_text)
        except ValueError as e:
            raise Exception(f"Invalid JSON response from API: {e}")
    
    def get_user_profile(self) -> Dict[str, Any]:
        """Get user profile information"""
        return self._make_request('GET', 'account')
    
    def get_catalogs(self) -> List[Dict[str, Any]]:
        """Get user's Lightroom catalogs"""
        # Try different endpoint variations
        endpoints_to_try = ['catalog', 'catalogs']
        
        for endpoint in endpoints_to_try:
            try:
                response = self._make_request('GET', endpoint)
                
                # Handle both single catalog and multiple catalogs responses
                if 'resources' in response:
                    catalogs = response['resources']
                elif isinstance(response, list):
                    catalogs = response
                elif isinstance(response, dict) and 'id' in response:
                    # Single catalog response
                    catalogs = [response]
                else:
                    catalogs = []
                
                return catalogs
                
            except Exception as e:
                continue
        
        # If all endpoints failed, raise the last error
        raise Exception("Unable to retrieve catalogs from any known endpoint")
    
    def get_albums(self, catalog_id: str) -> List[Dict[str, Any]]:
        """Get albums from a catalog"""
        endpoint = f"catalogs/{catalog_id}/albums"
        response = self._make_request('GET', endpoint)
        return response.get('resources', [])
    
    def get_assets(self, catalog_id: str, limit: int = 100, cursor: Optional[str] = None, updated_since: Optional[str] = None) -> PhotoCollection:
        """Get assets (photos) from a catalog with proper timestamp-based pagination"""
        params = {
            'limit': limit,
            'embed': 'asset;develop_settings'
        }
        
        if cursor:
            params['cursor'] = cursor
        if updated_since:
            params['updated_since'] = updated_since
        
        endpoint = f"catalogs/{catalog_id}/assets"
        if params:
            endpoint += f"?{urlencode(params)}"
        
        response = self._make_request('GET', endpoint)
        
        # Parse photos from response
        photos = []
        for asset_data in response.get('resources', []):
            photo = Photo.from_api_response(asset_data)
            # Store raw API response data for debugging
            photo.raw_api_data = asset_data
            photos.append(photo)
        
        # Check for next page using the links.next field
        has_more = False
        next_cursor = None
        
        links = response.get('links', {})
        if 'next' in links and 'href' in links['next']:
            next_url = links['next']['href']
            # Parse the updated_since parameter from the next URL
            from urllib.parse import parse_qs, urlparse
            parsed_url = urlparse(next_url)
            query_params = parse_qs(parsed_url.query)
            if 'updated_since' in query_params:
                next_cursor = query_params['updated_since'][0]
                has_more = True
        
        return PhotoCollection(
            photos=photos,
            total_count=response.get('total_count', len(photos)),
            has_more=has_more,
            next_cursor=next_cursor
        )
    
    def get_assets_photos(self, catalog_id: str, limit: int = 100, cursor: Optional[str] = None) -> PhotoCollection:
        """Get photos using different parameters that might return more photos"""
        params = {
            'limit': limit,
            'embed': 'asset;develop_settings',
            'type': 'photos'  # Try type parameter
        }
        
        if cursor:
            params['cursor'] = cursor
        
        endpoint = f"catalogs/{catalog_id}/assets"
        if params:
            endpoint += f"?{urlencode(params)}"
        
        response = self._make_request('GET', endpoint)
        
        # Parse photos from response
        photos = []
        for asset_data in response.get('resources', []):
            photo = Photo.from_api_response(asset_data)
            # Store raw API response data for debugging
            photo.raw_api_data = asset_data
            photos.append(photo)
        
        return PhotoCollection(
            photos=photos,
            total_count=response.get('total_count', len(photos)),
            has_more=response.get('has_more', False),
            next_cursor=response.get('next_cursor')
        )
    
    def get_assets_all_types(self, catalog_id: str, limit: int = 100, cursor: Optional[str] = None) -> PhotoCollection:
        """Get assets without any filters - try to get everything"""
        params = {
            'limit': limit,
            'embed': 'asset;develop_settings;revisions;comments',  # More comprehensive embed
        }
        
        if cursor:
            params['cursor'] = cursor
        
        endpoint = f"catalogs/{catalog_id}/assets"
        if params:
            endpoint += f"?{urlencode(params)}"
        
        response = self._make_request('GET', endpoint)
        
        # Parse photos from response
        photos = []
        for asset_data in response.get('resources', []):
            photo = Photo.from_api_response(asset_data)
            # Store raw API response data for debugging
            photo.raw_api_data = asset_data
            photos.append(photo)
        
        return PhotoCollection(
            photos=photos,
            total_count=response.get('total_count', len(photos)),
            has_more=response.get('has_more', False),
            next_cursor=response.get('next_cursor')
        )
    
    def get_rejected_photos(self, catalog_id: str, limit: int = 100, max_pages: int = 1000) -> List[Photo]:
        """Get all rejected photos from a catalog using timestamp-based pagination (updated_since) for robustness."""
        all_photos = []
        updated_since = None
        page_count = 0
        while True:
            collection = self.get_assets(catalog_id, limit=limit, updated_since=updated_since)
            all_photos.extend(collection.photos)
            page_count += 1
            print(f"Fetched page {page_count}, updated_since={updated_since}, batch={len(collection.photos)}")
            if not collection.has_more or not collection.next_cursor:
                break
            if collection.next_cursor == updated_since:
                print("Warning: updated_since did not advance, breaking to avoid infinite loop.")
                break
            if page_count >= max_pages:
                print(f"Warning: reached max_pages={max_pages}, breaking loop.")
                break
            updated_since = collection.next_cursor
        # Filter for rejected photos after collecting all
        rejected_photos = [p for p in all_photos if p.flag == 'reject']
        return rejected_photos
    
    def get_photo_details(self, catalog_id: str, asset_id: str) -> Photo:
        """Get detailed information about a specific photo"""
        endpoint = f"catalogs/{catalog_id}/assets/{asset_id}"
        response = self._make_request('GET', endpoint)
        
        return Photo.from_api_response(response)
    
    def search_photos(self, catalog_id: str, query: Dict[str, Any], limit: int = 100) -> PhotoCollection:
        """Search photos in a catalog with filters using proper pagination"""
        
        # Always fetch ALL photos using proper timestamp pagination to ensure we get everything
        all_photos = []
        cursor = None
        
        while True:
            # Use the updated get_assets method with timestamp pagination
            collection = self.get_assets(catalog_id, limit=500, updated_since=cursor)
            all_photos.extend(collection.photos)
            
            # Check if we have more pages
            if not collection.has_more or not collection.next_cursor:
                break
                
            cursor = collection.next_cursor
        
        # Apply filters to the complete photo list
        filtered_photos = all_photos
        
        if query.get('rejected_only', False):
            filtered_photos = [p for p in filtered_photos if p.flag == 'reject']
        
        if query.get('accepted_only', False):
            filtered_photos = [p for p in filtered_photos if p.flag == 'pick']
        
        # Apply year filtering
        if query.get('year'):
            year = query['year']
            date_type = query.get('date_type', 'import')
            year_filtered = []
            for photo in filtered_photos:
                photo_year = None
                
                if date_type == 'import':
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
                
                elif date_type == 'sync':
                    # Use created_date (cloud sync date)
                    if photo.created_date:
                        photo_year = photo.created_date.year
                
                elif date_type == 'capture':
                    # Use capture_date from metadata (original photo date)
                    if photo.metadata and photo.metadata.get('capture_date'):
                        try:
                            from datetime import datetime
                            capture_date_str = photo.metadata['capture_date']
                            if capture_date_str and capture_date_str != '0000-00-00T00:00:00':
                                capture_date = datetime.fromisoformat(capture_date_str.replace('Z', '+00:00'))
                                photo_year = capture_date.year
                        except (ValueError, TypeError):
                            continue
                
                if photo_year == year:
                    year_filtered.append(photo)
            
            filtered_photos = year_filtered
        
        return PhotoCollection(
            photos=filtered_photos,
            total_count=len(filtered_photos),
            has_more=False
        )
    
    def get_assets_with_filters(self, catalog_id: str, limit: int = 1000, cursor: Optional[str] = None, 
                               include_deleted: bool = True, include_hidden: bool = True) -> PhotoCollection:
        """Get assets with explicit filter parameters to try to include all photos"""
        params = {
            'limit': limit,
            'embed': 'asset,links,payload,renditions,state,thumbnails,sources,location,xmp'
        }
        if cursor:
            params['cursor'] = cursor
            
        endpoint = f"catalogs/{catalog_id}/assets"
        if params:
            endpoint += f"?{urlencode(params)}"
        
        response = self._make_request('GET', endpoint)
        
        # Parse photos from response
        photos = []
        for asset_data in response.get('resources', []):
            photo = Photo.from_api_response(asset_data)
            photo.raw_api_data = asset_data
            photos.append(photo)
        
        return PhotoCollection(
            photos=photos,
            total_count=response.get('total_count', len(photos)),
            has_more=response.get('has_more', False),
            next_cursor=response.get('next_cursor')
        )
    
    def debug_raw_api_response(self, catalog_id: str, limit: int = 10) -> dict:
        """Get raw API response to see what's actually being returned"""
        params = {
            'limit': limit,
            'embed': 'asset,links,payload,renditions,state,thumbnails,sources,location,xmp'
        }
            
        endpoint = f"catalogs/{catalog_id}/assets"
        if params:
            endpoint += f"?{urlencode(params)}"
        
        response = self._make_request('GET', endpoint)
        
        return response

    def delete_photo(self, catalog_id: str, asset_id: str) -> bool:
        """Delete a photo from Lightroom Cloud
        
        Args:
            catalog_id: The catalog ID containing the photo
            asset_id: The asset ID of the photo to delete
            
        Returns:
            True if deletion was successful, False otherwise
        """
        endpoint = f"catalogs/{catalog_id}/assets/{asset_id}"
        
        try:
            response = self._make_request('DELETE', endpoint)
            return True
        except Exception as e:
            print(f"Error deleting photo {asset_id}: {e}")
            return False
    
    def delete_photos_batch(self, catalog_id: str, asset_ids: list) -> dict:
        """Delete multiple photos from Lightroom Cloud
        
        Args:
            catalog_id: The catalog ID containing the photos
            asset_ids: List of asset IDs to delete
            
        Returns:
            Dictionary with success/failure counts and failed IDs
        """
        results = {
            'success_count': 0,
            'failure_count': 0,
            'failed_ids': []
        }
        
        for asset_id in asset_ids:
            if self.delete_photo(catalog_id, asset_id):
                results['success_count'] += 1
            else:
                results['failure_count'] += 1
                results['failed_ids'].append(asset_id)
                
        return results
