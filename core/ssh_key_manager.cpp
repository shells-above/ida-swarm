#include "common.h"
#include "ssh_key_manager.h"
#include "logger.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <pwd.h>
#endif

namespace llm_re {

std::string SSHKeyManager::get_private_key_path() {
    char key_path[QMAXPATH];
    qstrncpy(key_path, get_user_idadir(), sizeof(key_path));
    qstrncat(key_path, "/ida_swarm_ssh_key", sizeof(key_path));
    return std::string(key_path);
}

std::string SSHKeyManager::get_public_key_path() {
    char key_path[QMAXPATH];
    qstrncpy(key_path, get_user_idadir(), sizeof(key_path));
    qstrncat(key_path, "/ida_swarm_ssh_key.pub", sizeof(key_path));
    return std::string(key_path);
}

bool SSHKeyManager::ensure_key_pair_exists() {
    std::string private_key = get_private_key_path();
    std::string public_key = get_public_key_path();

    // Check if keys already exist
    if (std::filesystem::exists(private_key) && std::filesystem::exists(public_key)) {
        LOG("SSHKeyManager: SSH key pair already exists at %s\n", private_key.c_str());
        return true;
    }

    LOG("SSHKeyManager: SSH key pair not found, generating...\n");
    return generate_key_pair();
}

bool SSHKeyManager::generate_key_pair() {
    std::string private_key = get_private_key_path();
    std::string public_key = get_public_key_path();

    // IDA user directory already exists (managed by IDA)
    // No need to create directory

    // Build ssh-keygen command
    // -t rsa: RSA key type
    // -b 4096: 4096-bit key
    // -f path: output file path
    // -N "": empty passphrase
    std::string command = "ssh-keygen -t rsa -b 4096 -f \"" + private_key +
                         "\" -N \"\" 2>&1";

    LOG("SSHKeyManager: Running: %s\n", command.c_str());

#ifdef _WIN32
    // Use _popen on Windows
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif

    if (!pipe) {
        LOG("SSHKeyManager: Failed to execute ssh-keygen\n");
        return false;
    }

    // Read output
    char buffer[256];
    std::string output;
#undef fgets  // Temporarily undefine IDA's macro to use standard fgets
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
#define fgets dont_use_fgets  // Restore IDA's protection

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif

    if (status != 0) {
        LOG("SSHKeyManager: ssh-keygen failed with status %d: %s\n", status, output.c_str());
        return false;
    }

    // Verify keys were created
    if (!std::filesystem::exists(private_key) || !std::filesystem::exists(public_key)) {
        LOG("SSHKeyManager: Key files not found after generation\n");
        return false;
    }

#ifndef _WIN32
    // Set restrictive permissions on private key (Unix only)
    chmod(private_key.c_str(), S_IRUSR | S_IWUSR);  // 600
#endif

    LOG("SSHKeyManager: Successfully generated SSH key pair\n");
    LOG("SSHKeyManager: Private key: %s\n", private_key.c_str());
    LOG("SSHKeyManager: Public key: %s\n", public_key.c_str());

    return true;
}

std::string SSHKeyManager::get_public_key_content() {
    std::string public_key_path = get_public_key_path();

    if (!std::filesystem::exists(public_key_path)) {
        LOG("SSHKeyManager: Public key file not found at %s\n", public_key_path.c_str());
        return "";
    }

    std::ifstream file(public_key_path);
    if (!file) {
        LOG("SSHKeyManager: Failed to open public key file\n");
        return "";
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    return content;
}

} // namespace llm_re
