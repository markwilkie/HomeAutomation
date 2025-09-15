#!/usr/bin/env python3
"""
Debug script to examine raw API responses from Adobe Lightroom
"""

import sys
import os
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from lightroom_tool.config import Config
from lightroom_tool.photos.api_client import LightroomAPIClient
from lightroom_tool.auth.token_manager import TokenManager
import json

def main():
    config = Config()
    token_manager = TokenManager(config)
    
    if not token_manager.get_valid_access_token():
        print("Not authenticated. Run 'lightroom-tool auth login' first.")
        return
    
    api_client = LightroomAPIClient(config)
    
    # Get catalogs
    catalogs = api_client.get_catalogs()
    if not catalogs:
        print("No catalogs found.")
        return
    
    catalog_id = catalogs[0]['id']
    print("Using catalog: " + catalog_id)
    
    # Make a raw API call to see the response structure
    print("\nMaking raw API call...")
    
    try:
        # Get first page of assets
        collection = api_client.get_assets(catalog_id, limit=50)
        
        print("Total photos in first batch: " + str(len(collection.photos)))
        print("Has more: " + str(collection.has_more))
        print("Next cursor: " + str(collection.next_cursor))
        print("Total count: " + str(collection.total_count))
        
        # Count WJD photos in first batch
        wjd_photos = [p for p in collection.photos if p.filename.startswith('WJD')]
        print("WJD photos in first batch: " + str(len(wjd_photos)))
        
        if wjd_photos:
            print("\nFirst few WJD photos:")
            for photo in wjd_photos[:5]:
                print("  " + photo.filename + " - " + photo.id + " - Created: " + str(photo.created_date))
        
        # Check if there are any additional filters or parameters we should be aware of
        print("\nAPI Base URL: " + api_client.BASE_URL)
        print("API Version: " + api_client.API_VERSION)
        
    except Exception as e:
        print("Error: " + str(e))

if __name__ == "__main__":
    main()