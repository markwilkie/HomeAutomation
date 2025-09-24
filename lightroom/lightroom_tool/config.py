"""
Configuration management for Adobe Lightroom Cloud Access Tool
"""

import os
import json
import yaml
from typing import Optional, Dict, Any, List
from pathlib import Path
from dotenv import load_dotenv


class Config:
    """Application configuration"""
    
    def __init__(self, env_file: Optional[str] = None, config_file: Optional[str] = None):
        # Load environment variables from .env file
        if env_file:
            load_dotenv(env_file)
        else:
            load_dotenv()
        
        # Load general configuration from YAML or JSON file
        self._general_config = self._load_general_config(config_file)
    
    def _load_general_config(self, config_file: Optional[str] = None) -> Dict[str, Any]:
        """Load general configuration from YAML or JSON file"""
        if config_file:
            config_path = Path(config_file)
        else:
            # Try to find config files in order of preference
            base_dir = Path.cwd()
            config_files = [
                base_dir / "config.yaml",
                base_dir / "config.yml", 
                base_dir / "config.json",
                base_dir / "config.example.json"
            ]
            config_path = None
            for path in config_files:
                if path.exists():
                    config_path = path
                    break
        
        if not config_path or not config_path.exists():
            # Return default configuration if no file found
            return self._get_default_config()
        
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                if config_path.suffix.lower() in ['.yaml', '.yml']:
                    return yaml.safe_load(f) or {}
                elif config_path.suffix.lower() == '.json':
                    return json.load(f)
                else:
                    return {}
        except Exception as e:
            print(f"Warning: Could not load config file {config_path}: {e}")
            return self._get_default_config()
    
    def _get_default_config(self) -> Dict[str, Any]:
        """Get default configuration values"""
        return {
            "paths": {
                "photo_base": "Photos",
                "jpg_subdir": "jpg",
                "raw_subdir": "raw",
                "export_base": "Lightroom_Exports",
                "rejected_photos": "Lightroom_Exports/rejected_photos",
                "backups": "Lightroom_Exports/backups"
            },
            "filenames": {
                "rejected_json": "rejected_photos.json",
                "metadata_json": "photo_metadata.json",
                "deletion_plan": "deletion_plan.json",
                "accepted_photos": "accepted_photos.json"
            },
            "settings": {
                "default_photo_limit": 1000,
                "max_photos_per_request": 500,
                "default_output_format": "table",
                "default_date_type": "import",
                "filename_only_matching": False,
                "require_deletion_confirmation": True,
                "show_dry_run_by_default": True
            },
            "photo_extensions": {
                "raw": [".RAF", ".CR2", ".CR3", ".NEF", ".ARW", ".ORF", ".RW2", ".DNG"],
                "processed": [".JPG", ".JPEG", ".TIFF", ".TIF", ".PNG"]
            },
            "directories": {
                "create_missing": True,
                "backup_before_delete": True
            }
        }
    
    @property
    def adobe_client_id(self) -> str:
        """Adobe API Client ID"""
        client_id = os.getenv('ADOBE_CLIENT_ID')
        if not client_id:
            raise ValueError(
                "ADOBE_CLIENT_ID not found in environment. "
                "Please set it in your .env file or environment variables."
            )
        return client_id
    
    @property
    def adobe_client_secret(self) -> Optional[str]:
        """Adobe API Client Secret (optional for PKCE flow)"""
        return os.getenv('ADOBE_CLIENT_SECRET')
    
    @property
    def use_pkce_flow(self) -> bool:
        """Whether to use PKCE flow (no client secret)"""
        return self.adobe_client_secret is None
    
    @property
    def adobe_redirect_uri(self) -> str:
        """OAuth redirect URI"""
        return os.getenv('ADOBE_REDIRECT_URI', 'http://localhost:8080/callback')
    
    @property
    def log_level(self) -> str:
        """Logging level"""
        return os.getenv('LOG_LEVEL', 'INFO')
    
    @property
    def cache_dir(self) -> str:
        """Cache directory for temporary files"""
        return os.getenv('CACHE_DIR', '.cache')
    
    # Path configuration methods
    @property
    def photo_base_path(self) -> str:
        """Base path for photo storage"""
        return self._general_config.get("paths", {}).get("photo_base", "Photos")
    
    @property
    def jpg_subdir(self) -> str:
        """JPG subdirectory name"""
        return self._general_config.get("paths", {}).get("jpg_subdir", "jpg")
    
    @property
    def raw_subdir(self) -> str:
        """RAW subdirectory name"""
        return self._general_config.get("paths", {}).get("raw_subdir", "raw")
    
    @property
    def export_base_path(self) -> str:
        """Base path for exports"""
        return self._general_config.get("paths", {}).get("export_base", "Lightroom_Exports")
    
    @property
    def rejected_photos_path(self) -> str:
        """Path for rejected photos exports"""
        return self._general_config.get("paths", {}).get("rejected_photos", "Lightroom_Exports/rejected_photos")
    
    @property
    def backups_path(self) -> str:
        """Path for backup files"""
        return self._general_config.get("paths", {}).get("backups", "Lightroom_Exports/backups")
    
    # Filename configuration methods
    @property
    def default_rejected_json_filename(self) -> str:
        """Default filename for rejected photos JSON"""
        return self._general_config.get("filenames", {}).get("rejected_json", "rejected_photos.json")
    
    @property
    def default_metadata_json_filename(self) -> str:
        """Default filename for metadata JSON"""
        return self._general_config.get("filenames", {}).get("metadata_json", "photo_metadata.json")
    
    @property
    def default_deletion_plan_filename(self) -> str:
        """Default filename for deletion plan"""
        return self._general_config.get("filenames", {}).get("deletion_plan", "deletion_plan.json")
    
    # Settings configuration methods
    @property
    def default_photo_limit(self) -> int:
        """Default limit for photo queries"""
        return self._general_config.get("settings", {}).get("default_photo_limit", 1000)
    
    @property
    def max_photos_per_request(self) -> int:
        """Maximum photos per API request"""
        return self._general_config.get("settings", {}).get("max_photos_per_request", 500)
    
    @property
    def default_output_format(self) -> str:
        """Default output format (table or json)"""
        return self._general_config.get("settings", {}).get("default_output_format", "table")
    
    @property
    def default_date_type(self) -> str:
        """Default date type for filtering"""
        return self._general_config.get("settings", {}).get("default_date_type", "import")
    
    @property
    def filename_only_matching(self) -> bool:
        """Whether to use filename-only matching by default"""
        return self._general_config.get("settings", {}).get("filename_only_matching", False)
    
    @property
    def require_deletion_confirmation(self) -> bool:
        """Whether to require confirmation for deletions"""
        return self._general_config.get("settings", {}).get("require_deletion_confirmation", True)
    
    @property
    def show_dry_run_by_default(self) -> bool:
        """Whether to show dry run by default"""
        return self._general_config.get("settings", {}).get("show_dry_run_by_default", True)
    
    # Photo extensions
    @property
    def raw_extensions(self) -> List[str]:
        """List of RAW photo file extensions"""
        return self._general_config.get("photo_extensions", {}).get("raw", [".RAF", ".CR2", ".CR3", ".NEF", ".ARW", ".ORF", ".RW2", ".DNG"])
    
    @property
    def processed_extensions(self) -> List[str]:
        """List of processed photo file extensions"""
        return self._general_config.get("photo_extensions", {}).get("processed", [".JPG", ".JPEG", ".TIFF", ".TIF", ".PNG"])
    
    @property
    def all_photo_extensions(self) -> List[str]:
        """List of all supported photo file extensions"""
        return self.raw_extensions + self.processed_extensions
    
    # Directory settings
    @property
    def create_missing_directories(self) -> bool:
        """Whether to create missing directories automatically"""
        return self._general_config.get("directories", {}).get("create_missing", True)
    
    @property
    def backup_before_delete(self) -> bool:
        """Whether to backup files before deletion"""
        return self._general_config.get("directories", {}).get("backup_before_delete", True)
    
    def get_full_path(self, path_key: str) -> Path:
        """Get full path for a configured path, creating directories if needed"""
        path_map = {
            'photo_base': self.photo_base_path,
            'export_base': self.export_base_path,
            'rejected_photos': self.rejected_photos_path,
            'backups': self.backups_path
        }
        
        path_str = path_map.get(path_key, path_key)
        full_path = Path(path_str).expanduser().resolve()
        
        if self.create_missing_directories and not full_path.exists():
            full_path.mkdir(parents=True, exist_ok=True)
            
        return full_path
    
    def get_default_export_path(self, filename: str) -> Path:
        """Get default export path with filename"""
        export_dir = self.get_full_path('export_base')
        return export_dir / filename
    
    def get_rejected_photos_export_path(self, filename: Optional[str] = None) -> Path:
        """Get path for rejected photos export"""
        if not filename:
            filename = self.default_rejected_json_filename
        rejected_dir = self.get_full_path('rejected_photos')
        return rejected_dir / filename
    
    def get_setting(self, key_path: str, default: Any = None) -> Any:
        """Get a setting using dot notation path (e.g., 'paths.jpg_subdir')"""
        keys = key_path.split('.')
        value = self._general_config
        
        for key in keys:
            if isinstance(value, dict) and key in value:
                value = value[key]
            else:
                return default
        
        return value
    
    def get_path(self, path_key: str) -> Optional[str]:
        """Get a configured path value"""
        return self.get_setting(f'paths.{path_key}')
    
    def validate(self) -> bool:
        """Validate configuration"""
        try:
            # Check required settings
            self.adobe_client_id
            # Client secret is optional for PKCE flow
            return True
        except ValueError:
            return False
