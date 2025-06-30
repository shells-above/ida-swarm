//
// Created by user on 6/29/25.
//

// test_program.cpp
// Compile with: g++ -O2 -o test_program.exe test_program.cpp -lws2_32 (on Windows)
// or: g++ -O2 -o test_program test_program.cpp (on Linux)

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

/*
"What is the valid license key for this program and how is it validated?"
"How does this program check for software updates? What server does it contact and what information does it send?"
"What anti-debugging or security measures does this program implement?"
"Explain the complete update process from start to finish, including all checks and validations performed."
"Are there any security vulnerabilities in how the program handles licenses or updates?"
 */

// Obfuscated strings
const char* encoded_update_host = "\x75\x70\x64\x61\x74\x65\x2e\x65\x78\x61\x6d\x70\x6c\x65\x2e\x63\x6f\x6d"; // "update.example.com"
const char* encoded_update_path = "\x2f\x61\x70\x69\x2f\x76\x31\x2f\x63\x68\x65\x63\x6b"; // "/api/v1/check"
const unsigned char license_key[] = {0x4d, 0x59, 0x2d, 0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 0x2d, 0x4b, 0x45, 0x59, 0x31, 0x32, 0x33}; // "MY-SECRET-KEY123"

// Version info
const int MAJOR_VERSION = 2;
const int MINOR_VERSION = 3;
const int BUILD_NUMBER = 1045;

// Simple XOR decryption
std::string decrypt_string(const char* encrypted, size_t len) {
    std::string result;
    for (size_t i = 0; i < len; i++) {
        result += encrypted[i];
    }
    return result;
}

// License validation
class LicenseValidator {
private:
    bool is_valid;
    std::string stored_key;

    int calculate_checksum(const std::string& key) {
        int sum = 0;
        for (char c : key) {
            sum += static_cast<int>(c);
            sum = (sum * 13) % 256;
        }
        return sum;
    }

public:
    LicenseValidator() : is_valid(false) {}

    bool validate_license(const std::string& user_key) {
        // Compare with hardcoded license
        std::string expected_key(reinterpret_cast<const char*>(license_key), sizeof(license_key));

        if (user_key != expected_key) {
            return false;
        }

        // Additional validation - checksum
        int checksum = calculate_checksum(user_key);
        if (checksum != 0x42) { // Magic checksum value
            return false;
        }

        stored_key = user_key;
        is_valid = true;
        return true;
    }

    bool is_licensed() const {
        return is_valid;
    }

    std::string get_machine_id() {
        // Fake machine ID generation
        return "MACHINE-" + std::to_string(time(nullptr) % 10000);
    }
};

// Network communication
class UpdateChecker {
private:
    std::string server_host;
    std::string update_path;
    int server_port;

    bool init_network() {
#ifdef _WIN32
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
        return true;
#endif
    }

    void cleanup_network() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    std::string build_http_request(const std::string& host, const std::string& path, const std::string& params) {
        std::stringstream request;
        request << "GET " << path << "?" << params << " HTTP/1.1\r\n";
        request << "Host: " << host << "\r\n";
        request << "User-Agent: UpdateClient/" << MAJOR_VERSION << "." << MINOR_VERSION << "\r\n";
        request << "Connection: close\r\n\r\n";
        return request.str();
    }

public:
    UpdateChecker() : server_port(80) {
        server_host = decrypt_string(encoded_update_host, strlen(encoded_update_host));
        update_path = decrypt_string(encoded_update_path, strlen(encoded_update_path));
    }

    struct UpdateInfo {
        bool update_available;
        std::string new_version;
        std::string download_url;
        std::string message;
    };

    UpdateInfo check_for_updates(const std::string& license_key, const std::string& machine_id) {
        UpdateInfo info = {false, "", "", "Failed to check for updates"};

        if (!init_network()) {
            info.message = "Network initialization failed";
            return info;
        }

        // Build query parameters
        std::stringstream params;
        params << "version=" << MAJOR_VERSION << "." << MINOR_VERSION << "." << BUILD_NUMBER;
        params << "&license=" << license_key;
        params << "&machine=" << machine_id;

        // Simulate network check (in real implementation, this would connect)
        // For testing, we'll simulate different responses based on version
        if (BUILD_NUMBER < 2000) {
            info.update_available = true;
            info.new_version = "2.4.2000";
            info.download_url = "https://update.example.com/download/v2.4.2000";
            info.message = "New version available!";
        } else {
            info.update_available = false;
            info.message = "You have the latest version";
        }

        cleanup_network();
        return info;
    }

    bool download_update(const std::string& url) {
        // Simulate download
        std::cout << "Downloading from: " << url << std::endl;

        // Anti-debugging check
        if (is_debugger_present()) {
            std::cout << "Error: Security violation detected" << std::endl;
            return false;
        }

        // Simulate download progress
        for (int i = 0; i <= 100; i += 10) {
            std::cout << "Progress: " << i << "%" << std::endl;
#ifdef _WIN32
            Sleep(100);
#else
            usleep(100000);
#endif
        }

        return true;
    }

private:
    bool is_debugger_present() {
        // Simple anti-debugging check
#ifdef _WIN32
        return IsDebuggerPresent();
#else
        // Linux: check for ptrace
        return false;
#endif
    }
};

// Configuration manager
class ConfigManager {
private:
    struct Config {
        std::string license_key;
        bool auto_update;
        int check_interval_hours;
        time_t last_check;
    };

    Config config;

public:
    ConfigManager() {
        config.auto_update = true;
        config.check_interval_hours = 24;
        config.last_check = 0;
    }

    bool load_config() {
        // Simulate loading from file
        // In real implementation, this would read from a config file
        config.license_key = ""; // Empty by default
        return true;
    }

    bool save_config() {
        // Simulate saving to file
        return true;
    }

    void set_license_key(const std::string& key) {
        config.license_key = key;
        save_config();
    }

    std::string get_license_key() const {
        return config.license_key;
    }

    bool should_check_update() const {
        if (!config.auto_update) return false;

        time_t now = time(nullptr);
        time_t hours_passed = (now - config.last_check) / 3600;

        return hours_passed >= config.check_interval_hours;
    }

    void update_last_check() {
        config.last_check = time(nullptr);
        save_config();
    }
};

// Main application class
class Application {
private:
    LicenseValidator license_validator;
    UpdateChecker update_checker;
    ConfigManager config_manager;

    void print_banner() {
        std::cout << "========================================" << std::endl;
        std::cout << "Software Update Manager v" << MAJOR_VERSION << "." << MINOR_VERSION << "." << BUILD_NUMBER << std::endl;
        std::cout << "========================================" << std::endl;
    }

    void perform_update_check() {
        std::cout << "\nChecking for updates..." << std::endl;

        std::string machine_id = license_validator.get_machine_id();
        std::string license = config_manager.get_license_key();

        UpdateChecker::UpdateInfo info = update_checker.check_for_updates(license, machine_id);

        std::cout << "Status: " << info.message << std::endl;

        if (info.update_available) {
            std::cout << "New version available: " << info.new_version << std::endl;
            std::cout << "Download URL: " << info.download_url << std::endl;

            std::cout << "\nDo you want to download the update? (y/n): ";
            char response;
            std::cin >> response;

            if (response == 'y' || response == 'Y') {
                if (update_checker.download_update(info.download_url)) {
                    std::cout << "Update downloaded successfully!" << std::endl;
                } else {
                    std::cout << "Update download failed!" << std::endl;
                }
            }
        }

        config_manager.update_last_check();
    }

public:
    int run() {
        print_banner();

        // Load configuration
        if (!config_manager.load_config()) {
            std::cout << "Error: Failed to load configuration" << std::endl;
            return 1;
        }

        // Check license
        std::string stored_license = config_manager.get_license_key();
        if (stored_license.empty()) {
            std::cout << "Please enter your license key: ";
            std::string user_key;
            std::cin >> user_key;

            if (!license_validator.validate_license(user_key)) {
                std::cout << "Error: Invalid license key!" << std::endl;
                return 2;
            }

            config_manager.set_license_key(user_key);
            std::cout << "License validated successfully!" << std::endl;
        } else {
            if (!license_validator.validate_license(stored_license)) {
                std::cout << "Error: Stored license is invalid!" << std::endl;
                return 2;
            }
        }

        // Main menu
        while (true) {
            std::cout << "\n--- Main Menu ---" << std::endl;
            std::cout << "1. Check for updates" << std::endl;
            std::cout << "2. Show version info" << std::endl;
            std::cout << "3. Show license info" << std::endl;
            std::cout << "4. Exit" << std::endl;
            std::cout << "Choice: ";

            int choice;
            std::cin >> choice;

            switch (choice) {
                case 1:
                    perform_update_check();
                    break;

                case 2:
                    std::cout << "\nVersion Information:" << std::endl;
                    std::cout << "Major: " << MAJOR_VERSION << std::endl;
                    std::cout << "Minor: " << MINOR_VERSION << std::endl;
                    std::cout << "Build: " << BUILD_NUMBER << std::endl;
                    break;

                case 3:
                    std::cout << "\nLicense Information:" << std::endl;
                    std::cout << "Status: " << (license_validator.is_licensed() ? "Valid" : "Invalid") << std::endl;
                    std::cout << "Machine ID: " << license_validator.get_machine_id() << std::endl;
                    break;

                case 4:
                    std::cout << "Goodbye!" << std::endl;
                    return 0;

                default:
                    std::cout << "Invalid choice!" << std::endl;
            }
        }

        return 0;
    }
};

// Entry point
int main() {
    Application app;
    return app.run();
}