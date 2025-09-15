"""
Token management for secure storage and retrieval of authentication tokens
"""

import json
import os
from datetime import datetime, timedelta
from typing import Optional, Dict, Any
import keyring
import platform
import traceback
from cryptography.fernet import Fernet

from lightroom_tool.config import Config


class TokenManager:
    """Manages secure storage and retrieval of authentication tokens"""
    
    SERVICE_NAME = "lightroom-tool"
    USERNAME = "adobe-tokens"
    
    def __init__(self, config: Config):
        self.config = config
        self._encryption_key = self._get_or_create_encryption_key()
        # Fallback file path (used only if keyring write fails)
        self._fallback_dir = os.path.join(os.path.expanduser('~'), '.lightroom_tool')
        self._fallback_file = os.path.join(self._fallback_dir, 'tokens.enc')
        self._fallback_enabled = False
    
    def _get_or_create_encryption_key(self) -> bytes:
        """Get or create encryption key for token storage"""
        try:
            key = keyring.get_password(self.SERVICE_NAME, "encryption-key")
            if not key:
                # Generate new encryption key
                key = Fernet.generate_key().decode()
                keyring.set_password(self.SERVICE_NAME, "encryption-key", key)
            return key.encode()
        except Exception:
            # If keyring itself is unusable, enable fallback immediately
            self._enable_fallback()
            if not os.path.isdir(self._fallback_dir):
                os.makedirs(self._fallback_dir, exist_ok=True)
            key_path = os.path.join(self._fallback_dir, 'encryption.key')
            if os.path.isfile(key_path):
                with open(key_path, 'rb') as f:
                    return f.read()
            new_key = Fernet.generate_key()
            with open(key_path, 'wb') as f:
                f.write(new_key)
            return new_key

    def _enable_fallback(self):
        if not self._fallback_enabled:
            self._fallback_enabled = True
            if not os.path.isdir(self._fallback_dir):
                os.makedirs(self._fallback_dir, exist_ok=True)
            print(
                "Warning: Falling back to file-based token storage (keyring unavailable).\n"
                f" Location: {self._fallback_file}\n"
                " Tokens are encrypted with a key stored alongside your user profile.\n"
                " Recommendations: (1) Ensure your user account is protected, (2) Don't commit this directory, (3) Delete the file after logging out if using a shared machine."
            )

    def _write_fallback(self, encrypted_data: str):
        if not self._fallback_enabled:
            self._enable_fallback()
        with open(self._fallback_file, 'w', encoding='utf-8') as f:
            f.write(encrypted_data)

    def _read_fallback(self) -> Optional[str]:
        if not os.path.isfile(self._fallback_file):
            return None
        try:
            with open(self._fallback_file, 'r', encoding='utf-8') as f:
                return f.read()
        except Exception:
            return None
    
    def _encrypt_data(self, data: str) -> str:
        """Encrypt data using Fernet encryption"""
        fernet = Fernet(self._encryption_key)
        return fernet.encrypt(data.encode()).decode()
    
    def _decrypt_data(self, encrypted_data: str) -> str:
        """Decrypt data using Fernet encryption"""
        fernet = Fernet(self._encryption_key)
        return fernet.decrypt(encrypted_data.encode()).decode()
    
    def store_tokens(self, token_data: Dict[str, Any]) -> None:
        """Store authentication tokens securely"""
        # Add timestamp for expiration tracking
        token_data['stored_at'] = datetime.utcnow().isoformat()
        
        # Calculate expiration time
        if 'expires_in' in token_data:
            expires_at = datetime.utcnow() + timedelta(seconds=token_data['expires_in'])
            token_data['expires_at'] = expires_at.isoformat()
        
        # Encrypt and store tokens
        encrypted_data = self._encrypt_data(json.dumps(token_data))
        try:
            if not self._fallback_enabled:
                keyring.set_password(self.SERVICE_NAME, self.USERNAME, encrypted_data)
            else:
                self._write_fallback(encrypted_data)
        except Exception as e:
            # Windows CredWrite error 1783 or other keyring backend issues
            msg = str(e)
            needs_fallback = False
            if 'CredWrite' in msg or '1783' in msg:
                needs_fallback = True
            else:
                # Some keyring backends raise generic exceptions
                needs_fallback = True
            if needs_fallback:
                self._enable_fallback()
                self._write_fallback(encrypted_data)
                print("Info: Stored tokens using fallback file backend due to keyring error (" + msg + ")")
            else:
                raise
    
    def get_tokens(self) -> Optional[Dict[str, Any]]:
        """Retrieve stored authentication tokens"""
        encrypted_data = None
        # If a fallback file exists, prefer enabling fallback immediately (covers new process start)
        if not self._fallback_enabled and os.path.isfile(self._fallback_file):
            self._enable_fallback()

        if not self._fallback_enabled:
            try:
                encrypted_data = keyring.get_password(self.SERVICE_NAME, self.USERNAME)
            except Exception:
                # Switch to fallback if read failed unexpectedly
                self._enable_fallback()
        if self._fallback_enabled and encrypted_data is None:
            encrypted_data = self._read_fallback()
        
        if not encrypted_data:
            return None
        
        try:
            decrypted_data = self._decrypt_data(encrypted_data)
            return json.loads(decrypted_data)
        except Exception:
            # If decryption fails, tokens are invalid
            self.clear_tokens()
            return None
    
    def clear_tokens(self) -> None:
        """Clear stored authentication tokens"""
        if self._fallback_enabled:
            try:
                if os.path.isfile(self._fallback_file):
                    os.remove(self._fallback_file)
            except Exception:
                pass
        else:
            try:
                keyring.delete_password(self.SERVICE_NAME, self.USERNAME)
            except keyring.errors.PasswordDeleteError:
                pass  # Token already deleted or doesn't exist
    
    def is_token_valid(self, token_data: Optional[Dict[str, Any]] = None) -> bool:
        """Check if stored token is still valid"""
        if not token_data:
            token_data = self.get_tokens()
        
        if not token_data:
            return False
        
        # Check if token has expiration time
        if 'expires_at' not in token_data:
            return True  # Assume valid if no expiration info
        
        try:
            expires_at = datetime.fromisoformat(token_data['expires_at'])
            # Add 5-minute buffer for clock skew
            return datetime.utcnow() < (expires_at - timedelta(minutes=5))
        except ValueError:
            return False
    
    def get_valid_access_token(self) -> Optional[str]:
        """Get a valid access token, refreshing if necessary"""
        from lightroom_tool.auth.oauth_client import OAuthClient
        
        token_data = self.get_tokens()
        
        if not token_data:
            return None
        
        # If token is still valid, return it
        if self.is_token_valid(token_data):
            return token_data.get('access_token')
        
        # Try to refresh token
        if 'refresh_token' in token_data:
            try:
                oauth_client = OAuthClient(self.config)
                new_token_data = oauth_client.refresh_token(token_data['refresh_token'])
                
                # Store new tokens
                self.store_tokens(new_token_data)
                return new_token_data.get('access_token')
                
            except Exception as e:
                print(f"Failed to refresh token: {e}")
                self.clear_tokens()
        
        return None
