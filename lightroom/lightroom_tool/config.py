"""
Configuration management for Adobe Lightroom Cloud Access Tool
"""

import os
from typing import Optional
from dotenv import load_dotenv


class Config:
    """Application configuration"""
    
    def __init__(self, env_file: Optional[str] = None):
        # Load environment variables from .env file
        if env_file:
            load_dotenv(env_file)
        else:
            load_dotenv()
    
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
    
    def validate(self) -> bool:
        """Validate configuration"""
        try:
            # Check required settings
            self.adobe_client_id
            # Client secret is optional for PKCE flow
            return True
        except ValueError:
            return False
