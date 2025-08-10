#include "oauth_authorizer.h"
#include "oauth_manager.h"
#include "../client/client.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <cstdlib>
#elif _WIN32
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace claude::auth {

namespace {
    // CURL write callback
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t totalSize = size * nmemb;
        userp->append((char*)contents, totalSize);
        return totalSize;
    }
}

OAuthAuthorizer::OAuthAuthorizer() {
}

OAuthAuthorizer::~OAuthAuthorizer() {
    stopCallbackServer();
}

bool OAuthAuthorizer::authorize() {
    if (is_authorizing_) {
        last_error_ = "Authorization already in progress";
        return false;
    }
    
    is_authorizing_ = true;
    
    // Generate PKCE parameters
    pkce_params_.code_verifier = generateCodeVerifier();
    pkce_params_.code_challenge = generateCodeChallenge(pkce_params_.code_verifier);
    pkce_params_.state = generateState();
    
    // Start callback server
    if (!startCallbackServer()) {
        is_authorizing_ = false;
        return false;
    }
    
    // Build authorization URL
    std::string auth_url = buildAuthorizationUrl(pkce_params_);
    
    // Open browser
    if (!openBrowser(auth_url)) {
        stopCallbackServer();
        is_authorizing_ = false;
        return false;
    }
    
    // Wait for authorization code
    std::string code = waitForAuthCode();
    
    // Stop server
    stopCallbackServer();
    
    if (code.empty()) {
        last_error_ = "No authorization code received (timeout or user cancelled)";
        is_authorizing_ = false;
        return false;
    }
    
    // Exchange code for tokens
    auto creds = exchangeCodeForTokens(code);
    if (!creds) {
        is_authorizing_ = false;
        return false;
    }
    
    // Save credentials
    if (!saveCredentials(*creds)) {
        last_error_ = "Failed to save credentials";
        is_authorizing_ = false;
        return false;
    }
    
    is_authorizing_ = false;
    return true;
}

std::string OAuthAuthorizer::generateCodeVerifier() {
    // Generate 128 random bytes for code verifier
    std::vector<uint8_t> random_bytes(96);
    RAND_bytes(random_bytes.data(), 96);
    return base64UrlEncode(random_bytes);
}

std::string OAuthAuthorizer::generateCodeChallenge(const std::string& verifier) {
    // SHA256 hash of verifier
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(verifier.c_str()), 
           verifier.length(), hash);
    
    std::vector<uint8_t> hash_vec(hash, hash + SHA256_DIGEST_LENGTH);
    return base64UrlEncode(hash_vec);
}

std::string OAuthAuthorizer::generateState() {
    // Generate random state parameter
    std::vector<uint8_t> random_bytes(32);
    RAND_bytes(random_bytes.data(), 32);
    return base64UrlEncode(random_bytes);
}

std::string OAuthAuthorizer::base64UrlEncode(const std::vector<uint8_t>& data) {
    // Base64 encode
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data.data(), data.size());
    BIO_flush(bio);
    
    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);
    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);
    
    // Convert to base64url
    for (char& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    // Remove padding
    result.erase(result.find_last_not_of('=') + 1);
    
    return result;
}

bool OAuthAuthorizer::startCallbackServer() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        last_error_ = "Failed to initialize Winsock";
        return false;
    }
#endif
    
    // Create socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        last_error_ = "Failed to create socket";
        return false;
    }
    
    // Allow reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    // Bind to port
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(OAUTH_REDIRECT_PORT);
    
    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        last_error_ = "Failed to bind to port " + std::to_string(OAUTH_REDIRECT_PORT) +
                     " (is another instance running?)";
#ifdef _WIN32
        closesocket(server_socket_);
        WSACleanup();
#else
        close(server_socket_);
#endif
        server_socket_ = -1;
        return false;
    }
    
    // Listen
    if (listen(server_socket_, 5) < 0) {
        last_error_ = "Failed to listen on socket";
#ifdef _WIN32
        closesocket(server_socket_);
        WSACleanup();
#else
        close(server_socket_);
#endif
        server_socket_ = -1;
        return false;
    }
    
    // Start server thread
    server_running_ = true;
    server_thread_ = std::thread(&OAuthAuthorizer::runServer, this);
    
    return true;
}

void OAuthAuthorizer::stopCallbackServer() {
    if (!server_running_) return;
    
    server_running_ = false;
    
    // Close socket to interrupt accept()
    if (server_socket_ != -1) {
#ifdef _WIN32
        closesocket(server_socket_);
#else
        close(server_socket_);
#endif
        server_socket_ = -1;
    }
    
    // Wait for thread to finish
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
}

void OAuthAuthorizer::runServer() {
    while (server_running_ && server_socket_ != -1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket_, 
                                  reinterpret_cast<struct sockaddr*>(&client_addr), 
                                  &client_len);
        
        if (client_socket < 0) {
            if (server_running_) {
                // Real error, not shutdown
                continue;
            }
            break;
        }
        
        // Handle request
        handleRequest(client_socket);
        
#ifdef _WIN32
        closesocket(client_socket);
#else
        close(client_socket);
#endif
    }
}

void OAuthAuthorizer::handleRequest(int client_socket) {
    // Read request
    char buffer[4096] = {0};
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        return;
    }
    
    std::string request(buffer);
    
    // Parse first line
    std::istringstream stream(request);
    std::string method, path, version;
    stream >> method >> path >> version;
    
    // Check if it's the callback
    if (method == "GET" && path.find("/callback") == 0) {
        // Extract query parameters
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            std::string query = path.substr(query_pos + 1);
            auto params = parseQueryString(query);
            
            std::string code = params["code"];
            std::string state = params["state"];
            
            if (!code.empty() && state == pkce_params_.state) {
                // Store the auth code
                {
                    std::lock_guard<std::mutex> lock(auth_mutex_);
                    auth_code_ = code;
                }
                auth_cv_.notify_all();
                
                // Send success response with redirect
                std::string response = "HTTP/1.1 302 Found\r\n";
                response += "Location: " + std::string(OAUTH_SUCCESS_URL) + "\r\n";
                response += "Content-Length: 0\r\n";
                response += "\r\n";
                
                send(client_socket, response.c_str(), response.length(), 0);
                return;
            }
        }
    }
    
    // Send 404 for other requests
    std::string response = "HTTP/1.1 404 Not Found\r\n";
    response += "Content-Type: text/plain\r\n";
    response += "Content-Length: 9\r\n";
    response += "\r\n";
    response += "Not Found";
    
    send(client_socket, response.c_str(), response.length(), 0);
}

std::map<std::string, std::string> OAuthAuthorizer::parseQueryString(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream stream(query);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            
            // Basic URL decode for value
            size_t percent_pos = 0;
            while ((percent_pos = value.find('%', percent_pos)) != std::string::npos) {
                if (percent_pos + 2 < value.length()) {
                    std::string hex = value.substr(percent_pos + 1, 2);
                    char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
                    value.replace(percent_pos, 3, 1, decoded);
                }
                percent_pos++;
            }
            
            params[key] = value;
        }
    }
    
    return params;
}

std::string OAuthAuthorizer::waitForAuthCode() {
    std::unique_lock<std::mutex> lock(auth_mutex_);
    auto timeout = std::chrono::seconds(TIMEOUT_SECONDS);
    
    if (auth_cv_.wait_for(lock, timeout, [this]() { return !auth_code_.empty(); })) {
        return auth_code_;
    }
    
    return "";
}

std::string OAuthAuthorizer::buildAuthorizationUrl(const PKCEParams& params) {
    std::ostringstream url;
    url << OAUTH_AUTH_URL << "?";
    url << "client_id=" << OAUTH_CLIENT_ID;
    url << "&response_type=code";
    url << "&redirect_uri=" << urlEncode("http://localhost:" + std::to_string(OAUTH_REDIRECT_PORT) + "/callback");
    url << "&scope=" << urlEncode("user:profile user:inference");
    url << "&code_challenge=" << params.code_challenge;
    url << "&code_challenge_method=S256";
    url << "&state=" << params.state;
    url << "&code=true";
    
    return url.str();
}

bool OAuthAuthorizer::openBrowser(const std::string& url) {
#ifdef __APPLE__
    std::string command = "open \"" + url + "\"";
    int result = system(command.c_str());
    return result == 0;
#elif _WIN32
    HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), 
                                    nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#else
    std::string command = "xdg-open \"" + url + "\" 2>/dev/null";
    int result = system(command.c_str());
    return result == 0;
#endif
}

std::optional<OAuthCredentials> OAuthAuthorizer::exchangeCodeForTokens(const std::string& code) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        last_error_ = "Failed to initialize CURL";
        return std::nullopt;
    }
    
    // Prepare request data
    json request_data = {
        {"grant_type", "authorization_code"},
        {"code", code},
        {"redirect_uri", "http://localhost:" + std::to_string(OAUTH_REDIRECT_PORT) + "/callback"},
        {"client_id", OAUTH_CLIENT_ID},
        {"code_verifier", pkce_params_.code_verifier},
        {"state", pkce_params_.state}
    };
    
    std::string request_body = request_data.dump();
    std::string response_body;
    
    // Set up CURL
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("User-Agent: " + std::string(USER_AGENT)).c_str());
    
    curl_easy_setopt(curl, CURLOPT_URL, OAUTH_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request_body.length());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        last_error_ = "CURL error: " + std::string(curl_easy_strerror(res));
        return std::nullopt;
    }
    
    if (http_code != 200) {
        last_error_ = "Token exchange failed with HTTP " + std::to_string(http_code) + ": " + response_body;
        return std::nullopt;
    }
    
    // Parse response
    try {
        json response = json::parse(response_body);
        
        OAuthCredentials creds;
        creds.access_token = response["access_token"];
        creds.refresh_token = response.value("refresh_token", "");
        
        // Calculate expiry time
        auto now = std::chrono::system_clock::now();
        auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        creds.expires_at = now_seconds + response["expires_in"].get<int>();
        
        // Extract account UUID if present
        if (response.contains("account") && response["account"].contains("uuid")) {
            creds.account_uuid = response["account"]["uuid"];
        }
        
        return creds;
        
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse token response: " + std::string(e.what());
        return std::nullopt;
    }
}

bool OAuthAuthorizer::saveCredentials(const OAuthCredentials& creds) {
    // Use OAuthManager to save credentials in encrypted format
    OAuthManager oauth_manager;
    
    if (!oauth_manager.save_credentials(creds)) {
        last_error_ = "Failed to save credentials: " + oauth_manager.get_last_error();
        return false;
    }
    
    return true;
}

std::string OAuthAuthorizer::urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

} // namespace claude::auth