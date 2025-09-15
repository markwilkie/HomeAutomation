"""
Test configuration
"""

import pytest
import os
from unittest.mock import patch
from lightroom_tool.config import Config


class TestConfig:
    """Test Config class"""
    
    def test_config_with_env_vars(self):
        """Test config with environment variables"""
        with patch.dict(os.environ, {
            'ADOBE_CLIENT_ID': 'test_client_id',
            'ADOBE_REDIRECT_URI': 'http://localhost:8080/callback'
        }):
            config = Config()
            assert config.adobe_client_id == 'test_client_id'
            assert config.adobe_client_secret is None  # No client secret for PKCE
            assert config.adobe_redirect_uri == 'http://localhost:8080/callback'
            assert config.use_pkce_flow is True
    
    def test_config_missing_required_vars(self):
        """Test config with missing required environment variables"""
        # Create a temporary config that doesn't load .env file
        with patch.dict(os.environ, {}, clear=True):
            config = Config(env_file='nonexistent.env')  # Force no .env loading
            
            # Should raise when trying to access the property
            with pytest.raises(ValueError, match="ADOBE_CLIENT_ID not found"):
                _ = config.adobe_client_id
    
    def test_config_default_values(self):
        """Test config default values"""
        with patch.dict(os.environ, {
            'ADOBE_CLIENT_ID': 'test_client_id'
        }):
            config = Config()
            assert config.adobe_redirect_uri == 'http://localhost:8080/callback'
            assert config.log_level == 'INFO'
            assert config.cache_dir == '.cache'
    
    def test_config_validation(self):
        """Test config validation"""
        with patch.dict(os.environ, {
            'ADOBE_CLIENT_ID': 'test_client_id'
        }):
            config = Config(env_file='nonexistent.env')  # Force no .env loading
            assert config.validate() is True
        
        with patch.dict(os.environ, {}, clear=True):
            config = Config(env_file='nonexistent.env')  # Force no .env loading
            assert config.validate() is False
