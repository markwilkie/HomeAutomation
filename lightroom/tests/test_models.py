"""
Test photo models
"""

import pytest
from datetime import datetime
from lightroom_tool.photos.models import Photo, PhotoCollection


class TestPhoto:
    """Test Photo model"""
    
    def test_photo_creation(self):
        """Test Photo object creation"""
        photo = Photo(
            id="test_id",
            filename="test.jpg",
            rejected=True,
            created_date=datetime.now(),
            file_size=1024000,
            format="image/jpeg"
        )
        
        assert photo.id == "test_id"
        assert photo.filename == "test.jpg"
        assert photo.rejected is True
        assert photo.file_size == 1024000
        assert photo.format == "image/jpeg"
    
    def test_photo_from_api_response(self):
        """Test Photo creation from API response"""
        api_data = {
            'id': 'test_id',
            'created': '2023-01-01T12:00:00Z',
            'updated': '2023-01-02T12:00:00Z',
            'subtype': 'image/jpeg',
            'payload': {
                'name': 'test_photo.jpg',
                'importSource': {
                    'fileSize': 1024000
                },
                'develop': {
                    'settings': {
                        'picked': -1  # -1 indicates rejected
                    }
                }
            },
            'links': {
                '/rels/thumbnail': 'https://example.com/thumb.jpg'
            }
        }
        
        photo = Photo.from_api_response(api_data)
        
        assert photo.id == 'test_id'
        assert photo.filename == 'test_photo.jpg'
        assert photo.rejected is True
        assert photo.file_size == 1024000
        assert photo.format == 'image/jpeg'
        assert photo.thumbnail_url == 'https://example.com/thumb.jpg'
    
    def test_photo_to_dict(self):
        """Test Photo serialization to dictionary"""
        photo = Photo(
            id="test_id",
            filename="test.jpg",
            rejected=False,
            file_size=1024000
        )
        
        photo_dict = photo.to_dict()
        
        assert photo_dict['id'] == 'test_id'
        assert photo_dict['filename'] == 'test.jpg'
        assert photo_dict['rejected'] is False
        assert photo_dict['file_size'] == 1024000


class TestPhotoCollection:
    """Test PhotoCollection model"""
    
    def test_photo_collection_creation(self):
        """Test PhotoCollection creation"""
        photos = [
            Photo(id="1", filename="photo1.jpg", rejected=True),
            Photo(id="2", filename="photo2.jpg", rejected=False),
            Photo(id="3", filename="photo3.jpg", rejected=True)
        ]
        
        collection = PhotoCollection(
            photos=photos,
            total_count=3,
            has_more=False
        )
        
        assert len(collection.photos) == 3
        assert collection.total_count == 3
        assert collection.has_more is False
    
    def test_rejected_photos_filter(self):
        """Test filtering rejected photos"""
        photos = [
            Photo(id="1", filename="photo1.jpg", rejected=True),
            Photo(id="2", filename="photo2.jpg", rejected=False),
            Photo(id="3", filename="photo3.jpg", rejected=True)
        ]
        
        collection = PhotoCollection(photos=photos, total_count=3)
        rejected = collection.rejected_photos()
        
        assert len(rejected) == 2
        assert all(photo.rejected for photo in rejected)
    
    def test_accepted_photos_filter(self):
        """Test filtering accepted photos"""
        photos = [
            Photo(id="1", filename="photo1.jpg", rejected=True),
            Photo(id="2", filename="photo2.jpg", rejected=False),
            Photo(id="3", filename="photo3.jpg", rejected=True)
        ]
        
        collection = PhotoCollection(photos=photos, total_count=3)
        accepted = collection.accepted_photos()
        
        assert len(accepted) == 1
        assert not accepted[0].rejected
