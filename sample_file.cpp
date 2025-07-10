#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Custom structs that the agent will need to identify and recreate
typedef struct {
    uint32_t user_id;
    char username[32];
    uint8_t access_level;
    uint16_t flags;
} User;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t serial;
    uint8_t key[16];
    uint32_t checksum;
} License;

typedef struct {
    User* user;
    License* license;
    uint32_t session_id;
    uint8_t is_valid;
    uint32_t timestamp;
} Session;

// Simple obfuscation functions
uint32_t rotate_left(uint32_t value, int shift) {
    return (value << shift) | (value >> (32 - shift));
}

uint32_t simple_hash(const char* data, size_t len) {
    uint32_t hash = 0x1337BEEF;
    for (size_t i = 0; i < len; i++) {
        hash = rotate_left(hash, 5) ^ data[i];
        hash *= 0x9E3779B1;
    }
    return hash;
}

// License validation functions
int validate_magic(License* lic) {
    return (lic->magic == 0xDEADBEEF);
}

int validate_version(License* lic) {
    return (lic->version >= 0x0100 && lic->version <= 0x0300);
}

uint32_t calculate_checksum(License* lic) {
    uint32_t sum = lic->magic ^ lic->version ^ lic->serial;
    for (int i = 0; i < 16; i++) {
        sum = rotate_left(sum, 3) + lic->key[i];
    }
    return sum;
}

int validate_checksum(License* lic) {
    return (calculate_checksum(lic) == lic->checksum);
}

// Key transformation function
void transform_key(uint8_t* key, uint32_t seed) {
    for (int i = 0; i < 16; i++) {
        seed = seed * 1103515245 + 12345;
        key[i] ^= (seed >> 16) & 0xFF;
    }
}

// User validation functions
int check_user_permissions(User* user, uint8_t required_level) {
    if (user->access_level < required_level) {
        return 0;
    }

    // Check specific flags
    if ((user->flags & 0x0001) == 0) {  // Active flag
        return 0;
    }

    if (user->flags & 0x0100) {  // Banned flag
        return 0;
    }

    return 1;
}

int validate_username(User* user) {
    size_t len = strlen(user->username);
    if (len < 4 || len > 20) {
        return 0;
    }

    // Username must start with letter
    if (!((user->username[0] >= 'a' && user->username[0] <= 'z') ||
          (user->username[0] >= 'A' && user->username[0] <= 'Z'))) {
        return 0;
    }

    return 1;
}

// Session management functions
Session* create_session(User* user, License* license) {
    Session* session = (Session*)malloc(sizeof(Session));
    if (!session) return NULL;

    session->user = user;
    session->license = license;
    session->session_id = simple_hash((char*)user, sizeof(User));
    session->is_valid = 0;
    session->timestamp = 0x12345678;  // Mock timestamp

    return session;
}

void destroy_session(Session* session) {
    if (session) {
        session->is_valid = 0;
        session->session_id = 0;
        free(session);
    }
}

// Complex validation that uses multiple functions
int perform_full_validation(Session* session) {
    if (!session || !session->user || !session->license) {
        return 0;
    }

    // Step 1: Validate user
    if (!validate_username(session->user)) {
        return 0;
    }

    if (!check_user_permissions(session->user, 2)) {
        return 0;
    }

    // Step 2: Validate license structure
    if (!validate_magic(session->license)) {
        return 0;
    }

    if (!validate_version(session->license)) {
        return 0;
    }

    // Step 3: Transform and validate key
    uint8_t temp_key[16];
    memcpy(temp_key, session->license->key, 16);
    transform_key(temp_key, session->user->user_id);

    // Check if transformed key matches expected pattern
    uint32_t key_sum = 0;
    for (int i = 0; i < 16; i++) {
        key_sum += temp_key[i];
    }

    if (key_sum < 0x400 || key_sum > 0x800) {
        return 0;
    }

    // Step 4: Validate checksum
    if (!validate_checksum(session->license)) {
        return 0;
    }

    // Step 5: Cross-reference user ID with license serial
    uint32_t expected_serial = simple_hash(session->user->username,
                                         strlen(session->user->username));
    if ((session->license->serial & 0xFFFF0000) != (expected_serial & 0xFFFF0000)) {
        return 0;
    }

    // All checks passed
    session->is_valid = 1;
    return 1;
}

// Indirect function call through function pointer
typedef int (*validator_func)(Session*);

int execute_validation(Session* session, validator_func validator) {
    if (!validator) {
        return 0;
    }
    return validator(session);
}

// Main function that ties everything together
int main(int argc, char* argv[]) {
    // Create test user
    User user = {
        .user_id = 0x1337,
        .username = "testuser",
        .access_level = 3,
        .flags = 0x0001  // Active flag
    };

    // Create test license
    License license = {
        .magic = 0xDEADBEEF,
        .version = 0x0200,
        .serial = 0x7B3E0000,  // Matches hash of "testuser"
        .key = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50},
        .checksum = 0  // Will be calculated
    };

    // Calculate correct checksum
    license.checksum = calculate_checksum(&license);

    // Create session
    Session* session = create_session(&user, &license);
    if (!session) {
        printf("Failed to create session\n");
        return 1;
    }

    // Perform validation using function pointer
    validator_func validator = perform_full_validation;
    int result = execute_validation(session, validator);

    if (result && session->is_valid) {
        printf("SUCCESS: Session validated successfully!\n");
        printf("Session ID: 0x%08X\n", session->session_id);
        printf("User: %s (Level %d)\n", session->user->username,
               session->user->access_level);
    } else {
        printf("FAILURE: Session validation failed!\n");
    }

    // Cleanup
    destroy_session(session);

    return result ? 0 : 1;
}