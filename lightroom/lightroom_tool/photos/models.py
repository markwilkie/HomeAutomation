"""
Data models for photos and collections
"""

from dataclasses import dataclass
from datetime import datetime
from typing import Optional, Dict, Any, List


@dataclass
class Photo:
    """Represents a photo from Lightroom Cloud"""
    
    id: str
    filename: str
    flag: Optional[str] = None  # 'pick', 'reject', or None for unflagged
    created_date: Optional[datetime] = None
    updated_date: Optional[datetime] = None
    file_size: Optional[int] = None
    format: Optional[str] = None
    thumbnail_url: Optional[str] = None
    album_id: Optional[str] = None
    metadata: Optional[Dict[str, Any]] = None
    raw_api_data: Optional[Dict[str, Any]] = None  # Store complete API response
    
    @classmethod
    def from_api_response(cls, data: Dict[str, Any]) -> 'Photo':
        """Create Photo instance from API response data"""
        # Extract basic information
        payload = data.get('payload', {})
        develop_settings = payload.get('develop', {}).get('settings', {})
        import_source = payload.get('importSource', {})
        
        # Extract flag from reviews section
        reviews = payload.get('reviews', {})
        flag = None
        if reviews:
            # Get the first (and typically only) user's review
            for user_id, review in reviews.items():
                flag = review.get('flag')
                break
        
        # Extract comprehensive metadata
        metadata = {
            'develop_settings': develop_settings,
            'import_source': import_source,
            'capture_date': payload.get('captureDate'),
            'original_filename': import_source.get('fileName'),
            'flag': flag,
            'import_timestamp': import_source.get('importTimestamp'),
            'imported_device': import_source.get('importedOnDevice'),
            'content_type': import_source.get('contentType'),
            'sha256': import_source.get('sha256'),
            'camera_info': {
                'make': develop_settings.get('exif', {}).get('camera', {}).get('make'),
                'model': develop_settings.get('exif', {}).get('camera', {}).get('model'),
                'lens': develop_settings.get('exif', {}).get('lens', {}).get('model'),
            },
            'exposure_info': {
                'iso': develop_settings.get('exif', {}).get('camera', {}).get('iso'),
                'aperture': develop_settings.get('exif', {}).get('camera', {}).get('aperture'),
                'shutter_speed': develop_settings.get('exif', {}).get('camera', {}).get('shutterSpeed'),
                'focal_length': develop_settings.get('exif', {}).get('camera', {}).get('focalLength'),
            },
            'ratings': {
                'picked': develop_settings.get('picked', 0),
                'rating': develop_settings.get('rating', 0),
                'color_label': develop_settings.get('colorLabel'),
            },
            'dimensions': {
                'width': import_source.get('originalWidth'),
                'height': import_source.get('originalHeight'),
            },
            'keywords': develop_settings.get('keywords', []),
            'title': develop_settings.get('title'),
            'caption': develop_settings.get('caption'),
        }
        
        # Clean up None values
        metadata = {k: v for k, v in metadata.items() if v is not None}
        
        return cls(
            id=data.get('id', ''),
            filename=import_source.get('fileName') or payload.get('name', 'Unknown'),
            created_date=cls._parse_date(data.get('created')),
            updated_date=cls._parse_date(data.get('updated')),
            flag=flag,
            file_size=import_source.get('fileSize'),
            format=data.get('subtype'),
            thumbnail_url=data.get('links', {}).get('/rels/thumbnail'),
            metadata=metadata,
            raw_api_data=data  # Store complete response for debugging
        )
    
    @staticmethod
    def _parse_date(date_str: Optional[str]) -> Optional[datetime]:
        """Parse ISO date string to datetime object"""
        if not date_str:
            return None
        
        try:
            return datetime.fromisoformat(date_str.replace('Z', '+00:00'))
        except ValueError:
            return None
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert Photo to dictionary for serialization"""
        result = {
            'id': self.id,
            'filename': self.filename,
            'updated_date': self.updated_date.isoformat() if self.updated_date else None,
            'file_size': self.file_size,
            'format': self.format,
            'thumbnail_url': self.thumbnail_url,
            'album_id': self.album_id,
            'metadata': self.metadata,
            'flag': self.flag
        }
        
        # Include raw API data if available (for debugging)
        if self.raw_api_data:
            result['raw_api_data'] = self.raw_api_data
            
        return result


@dataclass
class PhotoCollection:
    """Represents a collection of photos"""
    
    photos: List[Photo]
    total_count: int
    has_more: bool = False
    next_cursor: Optional[str] = None
    
    def rejected_photos(self) -> List[Photo]:
        """Get only rejected photos from the collection"""
        return [photo for photo in self.photos if photo.flag == 'reject']
    
    def accepted_photos(self) -> List[Photo]:
        """Get only picked photos from the collection"""
        return [photo for photo in self.photos if photo.flag == 'pick']
    
    def unflagged_photos(self) -> List[Photo]:
        """Get only unflagged photos from the collection"""
        return [photo for photo in self.photos if photo.flag is None]
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert PhotoCollection to dictionary for serialization"""
        return {
            'photos': [photo.to_dict() for photo in self.photos],
            'total_count': self.total_count,
            'has_more': self.has_more,
            'next_cursor': self.next_cursor
        }
