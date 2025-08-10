#include "oauth_manager.h"
#include "oauth_flow.h"
#include <fstream>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <iomanip>
#include <algorithm>

namespace llm_re {

namespace {
    // Fernet constants
    constexpr size_t KEY_SIZE = 32;  // 256 bits total
    constexpr size_t SIGNING_KEY_SIZE = 16;  // First 128 bits for signing
    constexpr size_t ENCRYPTION_KEY_SIZE = 16;  // Last 128 bits for encryption
    constexpr size_t IV_SIZE = 16;  // 128 bits
    constexpr size_t TIMESTAMP_SIZE = 8;  // 64 bits
    constexpr size_t VERSION_SIZE = 1;  // 8 bits
    constexpr size_t HMAC_SIZE = 32;  // 256 bits
    constexpr unsigned char FERNET_VERSION = 0x80;
    
    // Base64url encode
    std::string base64url_encode(const std::vector<uint8_t>& data) {
        BIO* bio = BIO_new(BIO_s_mem());
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);
        
        BIO_write(bio, data.data(), data.size());
        BIO_flush(bio);
        
        BUF_MEM* buffer_ptr;
        BIO_get_mem_ptr(bio, &buffer_ptr);
        std::string result(buffer_ptr->data, buffer_ptr->length);
        BIO_free_all(bio);
        
        // Convert to URL-safe
        std::replace(result.begin(), result.end(), '+', '-');
        std::replace(result.begin(), result.end(), '/', '_');
        result.erase(result.find_last_not_of('=') + 1);
        
        return result;
    }
    
    // Base64url decode
    std::vector<uint8_t> base64url_decode(const std::string& encoded) {
        std::string padded = encoded;
        
        // Convert from URL-safe
        std::replace(padded.begin(), padded.end(), '-', '+');
        std::replace(padded.begin(), padded.end(), '_', '/');
        
        // Add padding
        switch (padded.length() % 4) {
            case 2: padded += "=="; break;
            case 3: padded += "="; break;
        }
        
        BIO* bio = BIO_new_mem_buf(padded.data(), padded.length());
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);
        
        std::vector<uint8_t> decoded(padded.length());
        int decoded_length = BIO_read(bio, decoded.data(), padded.length());
        BIO_free_all(bio);
        
        if (decoded_length < 0) {
            return {};
        }
        
        decoded.resize(decoded_length);
        return decoded;
    }
    
    // PKCS7 padding
    std::vector<uint8_t> pkcs7_pad(const std::vector<uint8_t>& data, size_t block_size) {
        size_t padding_len = block_size - (data.size() % block_size);
        std::vector<uint8_t> padded = data;
        padded.insert(padded.end(), padding_len, static_cast<uint8_t>(padding_len));
        return padded;
    }
    
    // Remove PKCS7 padding
    std::vector<uint8_t> pkcs7_unpad(const std::vector<uint8_t>& data) {
        if (data.empty()) return {};
        
        uint8_t padding_len = data.back();
        if (padding_len == 0 || padding_len > 16 || padding_len > data.size()) {
            return {};
        }
        
        // Verify padding
        for (size_t i = data.size() - padding_len; i < data.size(); ++i) {
            if (data[i] != padding_len) {
                return {};
            }
        }
        
        return std::vector<uint8_t>(data.begin(), data.end() - padding_len);
    }
    
    // Convert uint64 to bytes (big-endian)
    std::vector<uint8_t> uint64_to_bytes(uint64_t value) {
        std::vector<uint8_t> bytes(8);
        for (int i = 7; i >= 0; --i) {
            bytes[i] = value & 0xFF;
            value >>= 8;
        }
        return bytes;
    }
}

OAuthManager::OAuthManager(const std::string& config_dir_override) {
    // Determine config directory
    if (!config_dir_override.empty()) {
        config_dir = expand_home_directory(config_dir_override);
    } else {
        // Default to ~/.claude_cpp_sdk
        qstring home_path;
        if (qgetenv("HOME", &home_path)) {
            config_dir = std::filesystem::path(home_path.c_str()) / ".claude_cpp_sdk";
        } else {
            last_error = "Could not determine home directory";
            return;
        }
    }
    
    // Set file paths
    credentials_file = config_dir / "credentials.json";
    key_file = config_dir / ".key";
}

std::filesystem::path OAuthManager::expand_home_directory(const std::string& path) const {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    
    qstring home_path;
    if (!qgetenv("HOME", &home_path)) {
        return path;
    }
    
    return std::filesystem::path(home_path.c_str()) / path.substr(2);  // Skip "~/"
}

bool OAuthManager::has_credentials() const {
    return std::filesystem::exists(credentials_file) && std::filesystem::exists(key_file);
}

std::optional<api::OAuthCredentials> OAuthManager::get_credentials() {
    // Check cache first
    auto now = std::chrono::steady_clock::now();
    if (cached_credentials.has_value()) {
        long long elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cache_time).count();
        if (elapsed < CACHE_DURATION_SECONDS) {
            return cached_credentials;
        }
    }
    
    // Check if files exist
    if (!has_credentials()) {
        last_error = "OAuth credentials not found in " + config_dir.string();
        return std::nullopt;
    }
    
    // Read encryption key
    std::optional<std::string> key_data = read_file(key_file);
    if (!key_data) {
        last_error = "Failed to read encryption key";
        return std::nullopt;
    }
    
    // Read encrypted credentials
    std::optional<std::string> encrypted_data = read_file(credentials_file);
    if (!encrypted_data) {
        last_error = "Failed to read credentials file";
        return std::nullopt;
    }
    
    // Decrypt credentials
    std::optional<std::string> decrypted_data = decrypt_data(*encrypted_data, *key_data);
    if (!decrypted_data) {
        last_error = "Failed to decrypt credentials";
        return std::nullopt;
    }
    
    // Parse JSON
    std::optional<json> creds_json = parse_credentials_json(*decrypted_data);
    if (!creds_json) {
        last_error = "Failed to parse credentials JSON";
        return std::nullopt;
    }
    
    // Extract OAuth credentials
    try {
        api::OAuthCredentials creds = extract_oauth_credentials(*creds_json);
        
        // Cache the credentials
        cached_credentials = creds;
        cache_time = now;
        
        return creds;
    } catch (const std::exception& e) {
        last_error = std::string("Failed to extract OAuth credentials: ") + e.what();
        return std::nullopt;
    }
}

std::optional<api::OAuthCredentials> OAuthManager::get_cached_credentials() const {
    return cached_credentials;
}

void OAuthManager::clear_cache() {
    cached_credentials.reset();
}

bool OAuthManager::save_credentials(const api::OAuthCredentials& creds) {
    try {
        // Create the credentials JSON structure
        json oauth_tokens;
        oauth_tokens["claude_ai"] = {
            {"access_token", creds.access_token},
            {"refresh_token", creds.refresh_token},
            {"expires_at", creds.expires_at},
            {"account_uuid", creds.account_uuid},
            {"scopes", json::array({"user:profile", "user:inference"})}
        };
        
        json stored_creds = {
            {"version", 1},
            {"api_key", nullptr},
            {"default_provider", "claude_ai"},
            {"oauth_tokens", oauth_tokens}
        };
        
        std::string json_str = stored_creds.dump();
        
        // Get or create encryption key
        std::string key_str;
        if (std::filesystem::exists(key_file)) {
            // Read existing key
            auto key_data = read_file(key_file);
            if (!key_data) {
                last_error = "Failed to read encryption key";
                return false;
            }
            key_str = *key_data;
        } else {
            // Generate new key (32 bytes = 256 bits)
            std::vector<uint8_t> key_bytes(KEY_SIZE);
            if (RAND_bytes(key_bytes.data(), KEY_SIZE) != 1) {
                last_error = "Failed to generate encryption key";
                return false;
            }
            
            // Convert to base64url string
            key_str = base64url_encode(key_bytes);
            
            // Save the key
            std::ofstream key_out(key_file, std::ios::binary);
            if (!key_out) {
                last_error = "Failed to save encryption key";
                return false;
            }
            key_out << key_str;
            key_out.close();
            
            // Set restrictive permissions on key file
#ifndef _WIN32
            chmod(key_file.c_str(), 0600);
#endif
        }
        
        // Encrypt the credentials
        std::string encrypted_data = encrypt_data(json_str, key_str);
        
        // Write encrypted data to file
        std::ofstream out(credentials_file, std::ios::binary);
        if (!out) {
            last_error = "Failed to open credentials file for writing";
            return false;
        }
        
        out.write(encrypted_data.c_str(), encrypted_data.length());
        out.close();
        
        // Set restrictive permissions
#ifndef _WIN32
        chmod(credentials_file.c_str(), 0600);
#endif
        
        // Update cache
        cached_credentials = creds;
        cache_time = std::chrono::steady_clock::now();
        
        return true;
        
    } catch (const std::exception& e) {
        last_error = std::string("Failed to save credentials: ") + e.what();
        return false;
    }
}

std::string OAuthManager::encrypt_data(const std::string& plaintext, const std::string& key_str) const {
    // Decode the base64url key
    std::vector<uint8_t> key_bytes = base64url_decode(key_str);
    if (key_bytes.size() != KEY_SIZE) {
        throw std::runtime_error("Invalid key size");
    }
    
    // Split key into signing and encryption keys
    std::vector<uint8_t> signing_key(key_bytes.begin(), key_bytes.begin() + SIGNING_KEY_SIZE);
    std::vector<uint8_t> encryption_key(key_bytes.begin() + SIGNING_KEY_SIZE, key_bytes.end());
    
    // Generate IV
    std::vector<uint8_t> iv(IV_SIZE);
    if (RAND_bytes(iv.data(), IV_SIZE) != 1) {
        throw std::runtime_error("Failed to generate IV");
    }
    
    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    std::vector<uint8_t> timestamp_bytes = uint64_to_bytes(timestamp);
    
    // Prepare plaintext and pad it
    std::vector<uint8_t> plaintext_vec(plaintext.begin(), plaintext.end());
    std::vector<uint8_t> padded_plaintext = pkcs7_pad(plaintext_vec, AES_BLOCK_SIZE);
    
    // Encrypt using AES-128-CBC
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, encryption_key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption");
    }
    
    std::vector<uint8_t> ciphertext(padded_plaintext.size() + AES_BLOCK_SIZE);
    int len;
    int ciphertext_len;
    
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, padded_plaintext.data(), padded_plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to encrypt data");
    }
    ciphertext_len = len;
    
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize encryption");
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);
    
    EVP_CIPHER_CTX_free(ctx);
    
    // Build payload: version || timestamp || iv || ciphertext
    std::vector<uint8_t> payload;
    payload.push_back(FERNET_VERSION);
    payload.insert(payload.end(), timestamp_bytes.begin(), timestamp_bytes.end());
    payload.insert(payload.end(), iv.begin(), iv.end());
    payload.insert(payload.end(), ciphertext.begin(), ciphertext.end());
    
    // Calculate HMAC
    std::vector<uint8_t> hmac(HMAC_SIZE);
    unsigned int hmac_len;
    
    if (!HMAC(EVP_sha256(), signing_key.data(), signing_key.size(),
              payload.data(), payload.size(), hmac.data(), &hmac_len)) {
        throw std::runtime_error("Failed to calculate HMAC");
    }
    
    // Append HMAC to payload
    payload.insert(payload.end(), hmac.begin(), hmac.end());
    
    // Base64url encode the final payload
    return base64url_encode(payload);
}

std::optional<std::string> OAuthManager::read_file(const std::filesystem::path& path) const {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<uint8_t> OAuthManager::derive_key(const std::string& password) const {
    // Use SHA-256 to derive a 32-byte key from the password
    std::vector<uint8_t> key(32);
    SHA256(reinterpret_cast<const unsigned char*>(password.c_str()), 
           password.length(), key.data());
    return key;
}

std::optional<std::string> OAuthManager::decrypt_data(const std::string& encrypted_data, 
                                                      const std::string& key_str) const {
    try {
        // Decode the base64url key (not hex!)
        std::vector<uint8_t> key_bytes = base64url_decode(key_str);
        if (key_bytes.size() != KEY_SIZE) {
            return std::nullopt;
        }
        
        // Split key into signing and encryption keys
        std::vector<uint8_t> signing_key(key_bytes.begin(), key_bytes.begin() + SIGNING_KEY_SIZE);
        std::vector<uint8_t> encryption_key(key_bytes.begin() + SIGNING_KEY_SIZE, key_bytes.end());
        
        // Decode the encrypted data (base64url)
        std::vector<uint8_t> payload = base64url_decode(encrypted_data);
        
        // Minimum size check
        if (payload.size() < VERSION_SIZE + TIMESTAMP_SIZE + IV_SIZE + HMAC_SIZE) {
            return std::nullopt;
        }
        
        // Extract and verify version
        unsigned char version = payload[0];
        if (version != FERNET_VERSION) {
            return std::nullopt;
        }
        
        // Extract and verify HMAC
        size_t payload_without_hmac_size = payload.size() - HMAC_SIZE;
        std::vector<uint8_t> stored_hmac(payload.end() - HMAC_SIZE, payload.end());
        
        std::vector<uint8_t> calculated_hmac(HMAC_SIZE);
        unsigned int hmac_len;
        
        if (!HMAC(EVP_sha256(), signing_key.data(), signing_key.size(),
                  payload.data(), payload_without_hmac_size, calculated_hmac.data(), &hmac_len)) {
            return std::nullopt;
        }
        
        // Constant-time comparison
        bool hmac_valid = true;
        for (size_t i = 0; i < HMAC_SIZE; ++i) {
            hmac_valid &= (stored_hmac[i] == calculated_hmac[i]);
        }
        
        if (!hmac_valid) {
            return std::nullopt;
        }
        
        // Extract IV and ciphertext
        std::vector<uint8_t> iv(payload.begin() + VERSION_SIZE + TIMESTAMP_SIZE,
                                payload.begin() + VERSION_SIZE + TIMESTAMP_SIZE + IV_SIZE);
        
        std::vector<uint8_t> ciphertext(payload.begin() + VERSION_SIZE + TIMESTAMP_SIZE + IV_SIZE,
                                        payload.end() - HMAC_SIZE);
        
        // Decrypt using AES-128-CBC
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return std::nullopt;
        
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, encryption_key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }
        
        std::vector<uint8_t> plaintext(ciphertext.size() + AES_BLOCK_SIZE);
        int len;
        int plaintext_len;
        
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), 
                            static_cast<int>(ciphertext.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }
        plaintext_len = len;
        
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }
        plaintext_len += len;
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Remove PKCS7 padding
        plaintext.resize(plaintext_len);
        std::vector<uint8_t> unpadded = pkcs7_unpad(plaintext);
        if (unpadded.empty()) {
            return std::nullopt;
        }
        
        return std::string(unpadded.begin(), unpadded.end());
        
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<json> OAuthManager::parse_credentials_json(const std::string& decrypted_data) const {
    try {
        return json::parse(decrypted_data);
    } catch (...) {
        return std::nullopt;
    }
}

api::OAuthCredentials OAuthManager::extract_oauth_credentials(const json& creds_json) const {
    api::OAuthCredentials creds;
    
    // {
    //   "version": 1,
    //   "api_key": null or string,
    //   "default_provider": "console" or "claude_ai",
    //   "oauth_tokens": {
    //     "console": {
    //       "access_token": "...",
    //       "refresh_token": "...",
    //       "expires_at": 1234567890.0,
    //       "account_uuid": "...",
    //       "scopes": [...]
    //     }
    //   }
    // }
    
    if (!creds_json.contains("oauth_tokens") || !creds_json["oauth_tokens"].is_object()) {
        throw std::runtime_error("No OAuth tokens found in credentials");
    }

    const nlohmann::basic_json<> &oauth_tokens = creds_json["oauth_tokens"];
    
    // Try to get the default provider first
    std::string provider = "console";  // Default to console
    if (creds_json.contains("default_provider") && creds_json["default_provider"].is_string()) {
        provider = creds_json["default_provider"];
    }
    
    // Check if we have tokens for this provider
    if (!oauth_tokens.contains(provider)) {
        // Try to find any available provider
        if (oauth_tokens.contains("console")) {
            provider = "console";
        } else if (oauth_tokens.contains("claude_ai")) {
            provider = "claude_ai";
        } else if (!oauth_tokens.empty()) {
            // Use the first available provider
            provider = oauth_tokens.begin().key();
        } else {
            throw std::runtime_error("No OAuth tokens available");
        }
    }

    const nlohmann::basic_json<>& token_data = oauth_tokens[provider];
    
    // Extract token fields
    if (token_data.contains("access_token") && token_data["access_token"].is_string()) {
        creds.access_token = token_data["access_token"];
    } else {
        throw std::runtime_error("Missing access_token");
    }
    
    if (token_data.contains("refresh_token") && token_data["refresh_token"].is_string()) {
        creds.refresh_token = token_data["refresh_token"];
    }
    
    if (token_data.contains("expires_at") && token_data["expires_at"].is_number()) {
        creds.expires_at = token_data["expires_at"];
    }
    
    if (token_data.contains("account_uuid") && token_data["account_uuid"].is_string()) {
        creds.account_uuid = token_data["account_uuid"];
    }
    
    // Log success (commented out to reduce spam during startup)
    // msg("LLM RE: Successfully loaded OAuth credentials for provider: %s\n", provider.c_str());
    
    return creds;
}

bool OAuthManager::needs_refresh() {
    // First check cache
    auto creds = get_cached_credentials();
    if (!creds) {
        // Cache is empty, try to load credentials
        creds = get_credentials();
        if (!creds) {
            return false;
        }
    }
    
    // Check if expired or will expire in the next 5 minutes
    bool needs_it = creds->is_expired(300);
    if (needs_it) {
        msg("LLM RE: OAuth token needs refresh (expired or expires in < 5 min)\n");
    }
    return needs_it;
}

std::optional<api::OAuthCredentials> OAuthManager::refresh_if_needed() {
    // Check if refresh is needed
    if (!needs_refresh()) {
        return get_cached_credentials();
    }
    
    msg("LLM RE: Refreshing OAuth token...\n");
    return force_refresh();
}

std::optional<api::OAuthCredentials> OAuthManager::force_refresh() {
    // Get current credentials
    auto current_creds = get_credentials();
    if (!current_creds) {
        last_error = "No OAuth credentials available to refresh";
        return std::nullopt;
    }
    
    // Check if we have a refresh token
    if (current_creds->refresh_token.empty()) {
        last_error = "No refresh token available";
        return std::nullopt;
    }
    
    try {
        OAuthFlow oauth_flow;
        
        // Attempt to refresh the token
        api::OAuthCredentials new_creds = oauth_flow.refresh_token(
            current_creds->refresh_token,
            current_creds->account_uuid
        );
        
        // Save the updated credentials
        if (!save_credentials(new_creds)) {
            last_error = "Failed to save refreshed credentials";
            return std::nullopt;
        }
        
        // Update cache
        cached_credentials = new_creds;
        cache_time = std::chrono::steady_clock::now();
        
        msg("LLM RE: OAuth token refreshed successfully\n");
        return new_creds;
        
    } catch (const std::exception& e) {
        last_error = std::string("Token refresh failed: ") + e.what();
        msg("LLM RE: OAuth token refresh failed: %s\n", e.what());
        return std::nullopt;
    }
}

} // namespace llm_re