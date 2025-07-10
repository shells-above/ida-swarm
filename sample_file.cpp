#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// Forward declarations for complex interdependencies
struct CryptoContext;
struct ValidationNode;

// Custom structs that the agent will need to identify and recreate
typedef struct {
    uint32_t user_id;
    char username[32];
    uint8_t access_level;
    uint16_t flags;
} User;

// Linked list node for audit trail
typedef struct AuditEntry {
    uint32_t timestamp;
    uint8_t action_type;
    uint32_t user_id;
    char details[64];
    struct AuditEntry* next;
    struct AuditEntry* prev;
} AuditEntry;

// Function pointer table (like a vtable)
typedef struct {
    int (*init)(void* ctx);
    int (*process)(void* ctx, void* data);
    int (*validate)(void* ctx);
    void (*cleanup)(void* ctx);
} OperationTable;

// Crypto context with nested structs
typedef struct CryptoContext {
    uint8_t iv[16];
    uint8_t key[32];
    struct {
        uint32_t rounds;
        uint32_t mode;
        uint8_t padding;
    } config;
    OperationTable* ops;
} CryptoContext;

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

// State machine states
typedef enum {
    STATE_INIT = 0,
    STATE_AUTH,
    STATE_VALIDATE,
    STATE_PROCESS,
    STATE_COMPLETE,
    STATE_ERROR
} SystemState;

// Complex validation tree node
typedef struct ValidationNode {
    uint8_t node_type;
    uint8_t priority;
    int (*validator)(void* data);
    struct ValidationNode* left;
    struct ValidationNode* right;
    struct ValidationNode* parent;
} ValidationNode;

// Global state machine
typedef struct {
    SystemState current_state;
    SystemState previous_state;
    uint32_t transition_count;
    AuditEntry* audit_head;
    AuditEntry* audit_tail;
    ValidationNode* validation_tree;
    CryptoContext* crypto;
} GlobalContext;

// Anti-debugging checks
static inline int check_debugger() {
    // Simple timing check
    clock_t start = clock();
    volatile int dummy = 0;
    for (int i = 0; i < 1000000; i++) {
        dummy += i;
    }
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    return (elapsed > 0.1); // Suspiciously slow
}

// Obfuscated constants
#define MAGIC_SEED (0x1337BEEF ^ 0xDEADBEEF)
#define ROT_CONST(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define OBFS_CONST(x) ((x) ^ 0xA5A5A5A5)

// Audit trail management
AuditEntry* create_audit_entry(uint8_t action, uint32_t user_id, const char* details) {
    AuditEntry* entry = (AuditEntry*)calloc(1, sizeof(AuditEntry));
    if (!entry) return NULL;

    entry->timestamp = time(NULL) ^ MAGIC_SEED;
    entry->action_type = action;
    entry->user_id = user_id;
    strncpy(entry->details, details, 63);
    entry->details[63] = '\0';

    return entry;
}

void add_audit_entry(GlobalContext* ctx, AuditEntry* entry) {
    if (!ctx || !entry) return;

    entry->next = NULL;
    entry->prev = ctx->audit_tail;

    if (ctx->audit_tail) {
        ctx->audit_tail->next = entry;
    } else {
        ctx->audit_head = entry;
    }
    ctx->audit_tail = entry;
}

// Crypto operations using function pointer table
int crypto_init(void* ctx) {
    CryptoContext* crypto = (CryptoContext*)ctx;
    if (!crypto) return 0;

    // Initialize with pseudo-random data
    for (int i = 0; i < 16; i++) {
        crypto->iv[i] = (i * 0x31) ^ 0xAA;
    }
    crypto->config.rounds = 10;
    crypto->config.mode = 1; // CBC mode
    crypto->config.padding = 16;

    return 1;
}

int crypto_process(void* ctx, void* data) {
    CryptoContext* crypto = (CryptoContext*)ctx;
    uint8_t* buffer = (uint8_t*)data;
    if (!crypto || !buffer) return 0;

    // Simple XOR cipher with IV
    for (int i = 0; i < 16; i++) {
        buffer[i] ^= crypto->iv[i];
        buffer[i] = ROT_CONST(buffer[i], 3);
        buffer[i] ^= crypto->key[i % 32];
    }

    return 1;
}

int crypto_validate(void* ctx) {
    CryptoContext* crypto = (CryptoContext*)ctx;
    if (!crypto) return 0;

    // Validate crypto configuration
    return (crypto->config.rounds >= 8 &&
            crypto->config.rounds <= 16 &&
            crypto->config.mode > 0 &&
            crypto->config.padding == 16);
}

void crypto_cleanup(void* ctx) {
    CryptoContext* crypto = (CryptoContext*)ctx;
    if (!crypto) return;

    memset(crypto->key, 0, 32);
    memset(crypto->iv, 0, 16);
}

// Static operation table
static OperationTable crypto_ops = {
    .init = crypto_init,
    .process = crypto_process,
    .validate = crypto_validate,
    .cleanup = crypto_cleanup
};

// Complex hash with multiple rounds
uint32_t complex_hash(const uint8_t* data, size_t len, uint32_t seed) {
    uint32_t hash = seed;
    uint32_t secondary = OBFS_CONST(0xCAFEBABE);

    for (size_t i = 0; i < len; i++) {
        hash = ROT_CONST(hash, 5) ^ data[i];
        hash *= 0x9E3779B1;
        secondary = ROT_CONST(secondary, 7) + hash;

        // Mix every 4 bytes
        if ((i & 3) == 3) {
            hash ^= secondary;
            secondary = ROT_CONST(hash, 13);
        }
    }

    // Final mixing
    hash ^= secondary;
    hash = ROT_CONST(hash, 17);
    hash *= OBFS_CONST(0x85EBCA6B);
    hash ^= (hash >> 16);

    return hash;
}

// State machine transitions with validation
int transition_state(GlobalContext* ctx, SystemState new_state) {
    if (!ctx) return 0;

    // Validate state transition
    switch (ctx->current_state) {
        case STATE_INIT:
            if (new_state != STATE_AUTH) return 0;
            break;
        case STATE_AUTH:
            if (new_state != STATE_VALIDATE && new_state != STATE_ERROR) return 0;
            break;
        case STATE_VALIDATE:
            if (new_state != STATE_PROCESS && new_state != STATE_ERROR) return 0;
            break;
        case STATE_PROCESS:
            if (new_state != STATE_COMPLETE && new_state != STATE_ERROR) return 0;
            break;
        case STATE_COMPLETE:
        case STATE_ERROR:
            if (new_state != STATE_INIT) return 0;
            break;
        default:
            return 0;
    }

    ctx->previous_state = ctx->current_state;
    ctx->current_state = new_state;
    ctx->transition_count++;

    // Audit the transition
    char audit_msg[64];
    snprintf(audit_msg, 64, "State: %d -> %d", ctx->previous_state, new_state);
    AuditEntry* entry = create_audit_entry(0x10, 0, audit_msg);
    add_audit_entry(ctx, entry);

    return 1;
}

// Validation tree operations
int validate_checksum_tree(void* data) {
    License* lic = (License*)data;
    if (!lic) return 0;

    uint32_t sum = lic->magic ^ lic->version ^ lic->serial;
    for (int i = 0; i < 16; i++) {
        sum = ROT_CONST(sum, 3) + lic->key[i];
    }
    return (sum == lic->checksum);
}

int validate_magic_tree(void* data) {
    License* lic = (License*)data;
    return (lic && lic->magic == OBFS_CONST(0x7B424BB5)); // Obfuscated 0xDEADBEEF
}

int validate_version_tree(void* data) {
    License* lic = (License*)data;
    return (lic && lic->version >= 0x0100 && lic->version <= 0x0300);
}

// Build validation tree
ValidationNode* build_validation_tree() {
    ValidationNode* root = (ValidationNode*)calloc(1, sizeof(ValidationNode));
    ValidationNode* left = (ValidationNode*)calloc(1, sizeof(ValidationNode));
    ValidationNode* right = (ValidationNode*)calloc(1, sizeof(ValidationNode));

    if (!root || !left || !right) {
        free(root); free(left); free(right);
        return NULL;
    }

    // Root checks magic
    root->node_type = 1;
    root->priority = 10;
    root->validator = validate_magic_tree;

    // Left child checks version
    left->node_type = 2;
    left->priority = 5;
    left->validator = validate_version_tree;
    left->parent = root;
    root->left = left;

    // Right child checks checksum
    right->node_type = 3;
    right->priority = 8;
    right->validator = validate_checksum_tree;
    right->parent = root;
    root->right = right;

    return root;
}

// Traverse and execute validation tree
int execute_validation_tree(ValidationNode* node, void* data) {
    if (!node) return 1;

    // In-order traversal with validation
    if (!execute_validation_tree(node->left, data)) return 0;

    if (node->validator && !node->validator(data)) {
        return 0;
    }

    if (!execute_validation_tree(node->right, data)) return 0;

    return 1;
}

// Advanced key transformation with multiple algorithms
void transform_key_advanced(uint8_t* key, uint32_t seed, int algorithm) {
    switch (algorithm) {
        case 0: // Linear congruential generator
            for (int i = 0; i < 16; i++) {
                seed = seed * 1103515245 + 12345;
                key[i] ^= (seed >> 16) & 0xFF;
            }
            break;

        case 1: // XORshift
            for (int i = 0; i < 16; i++) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                key[i] ^= seed & 0xFF;
            }
            break;

        case 2: // Custom PRNG
            for (int i = 0; i < 16; i++) {
                seed = ROT_CONST(seed, 7) * 0x45D9F3B;
                seed ^= MAGIC_SEED;
                key[i] = (key[i] + (seed & 0xFF)) & 0xFF;
            }
            break;

        default: // Fallback
            for (int i = 0; i < 16; i++) {
                key[i] ^= (seed >> (i % 4) * 8) & 0xFF;
            }
    }
}

// Dispatch table for operations
typedef enum {
    OP_VALIDATE_USER = 0,
    OP_VALIDATE_LICENSE,
    OP_TRANSFORM_KEY,
    OP_CHECK_SIGNATURE,
    OP_VERIFY_TIMESTAMP,
    OP_MAX
} OperationType;

// Operation dispatcher using switch
int dispatch_operation(GlobalContext* ctx, OperationType op, void* data) {
    if (!ctx || op >= OP_MAX) return 0;

    int result = 0;

    // Add anti-debugging check
    if (check_debugger()) {
        add_audit_entry(ctx, create_audit_entry(0xFF, 0, "Debugger detected"));
        return 0;
    }

    switch (op) {
        case OP_VALIDATE_USER: {
            User* user = (User*)data;
            if (!user) break;

            // Complex user validation
            size_t len = strlen(user->username);
            if (len < 4 || len > 20) break;

            uint32_t name_hash = complex_hash((uint8_t*)user->username, len, user->user_id);
            result = (name_hash & 0xF0000000) == 0x70000000;

            if (result) {
                result = (user->access_level >= 2) &&
                        (user->flags & 0x0001) &&
                        !(user->flags & 0x0100);
            }
            break;
        }

        case OP_VALIDATE_LICENSE: {
            License* lic = (License*)data;
            if (!lic) break;

            result = execute_validation_tree(ctx->validation_tree, lic);
            break;
        }

        case OP_TRANSFORM_KEY: {
            Session* session = (Session*)data;
            if (!session || !session->license || !session->user) break;

            uint8_t temp_key[16];
            memcpy(temp_key, session->license->key, 16);

            // Use user ID to select algorithm
            int algo = session->user->user_id % 3;
            transform_key_advanced(temp_key, session->user->user_id, algo);

            // Validate transformed key
            uint32_t key_sum = 0;
            for (int i = 0; i < 16; i++) {
                key_sum += temp_key[i];
            }

            result = (key_sum >= 0x400 && key_sum <= 0x800);
            break;
        }

        case OP_CHECK_SIGNATURE: {
            Session* session = (Session*)data;
            if (!session || !session->license || !session->user) break;

            uint32_t expected = complex_hash((uint8_t*)session->user->username,
                                           strlen(session->user->username),
                                           MAGIC_SEED);
            result = ((session->license->serial & 0xFFFF0000) == (expected & 0xFFFF0000));
            break;
        }

        case OP_VERIFY_TIMESTAMP: {
            Session* session = (Session*)data;
            if (!session) break;

            // Mock timestamp validation
            uint32_t current = time(NULL);
            result = (session->timestamp < current &&
                     (current - session->timestamp) < 86400); // 24 hours
            break;
        }

        default:
            break;
    }

    // Log operation result
    char log_msg[64];
    snprintf(log_msg, 64, "Op %d: %s", op, result ? "SUCCESS" : "FAILED");
    add_audit_entry(ctx, create_audit_entry(op, 0, log_msg));

    return result;
}

// Initialize global context
GlobalContext* init_global_context() {
    GlobalContext* ctx = (GlobalContext*)calloc(1, sizeof(GlobalContext));
    if (!ctx) return NULL;

    ctx->current_state = STATE_INIT;
    ctx->previous_state = STATE_INIT;
    ctx->transition_count = 0;

    // Initialize crypto context
    ctx->crypto = (CryptoContext*)calloc(1, sizeof(CryptoContext));
    if (ctx->crypto) {
        ctx->crypto->ops = &crypto_ops;
        ctx->crypto->ops->init(ctx->crypto);

        // Initialize crypto key
        for (int i = 0; i < 32; i++) {
            ctx->crypto->key[i] = (i * 0x17) ^ 0x55;
        }
    }

    // Build validation tree
    ctx->validation_tree = build_validation_tree();

    return ctx;
}

// Complex validation with state machine
int perform_stateful_validation(GlobalContext* ctx, Session* session) {
    if (!ctx || !session) return 0;

    // Must be in INIT state
    if (ctx->current_state != STATE_INIT) {
        return 0;
    }

    // Transition to AUTH
    if (!transition_state(ctx, STATE_AUTH)) {
        return 0;
    }

    // Validate user
    if (!dispatch_operation(ctx, OP_VALIDATE_USER, session->user)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Transition to VALIDATE
    if (!transition_state(ctx, STATE_VALIDATE)) {
        return 0;
    }

    // Validate license structure
    if (!dispatch_operation(ctx, OP_VALIDATE_LICENSE, session->license)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Encrypt session data for validation
    uint8_t session_data[16];
    memcpy(session_data, &session->session_id, 4);
    memcpy(session_data + 4, &session->user->user_id, 4);
    memcpy(session_data + 8, &session->timestamp, 4);
    memset(session_data + 12, 0, 4);

    if (!ctx->crypto->ops->process(ctx->crypto, session_data)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Transition to PROCESS
    if (!transition_state(ctx, STATE_PROCESS)) {
        return 0;
    }

    // Process key transformation
    if (!dispatch_operation(ctx, OP_TRANSFORM_KEY, session)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Check signature
    if (!dispatch_operation(ctx, OP_CHECK_SIGNATURE, session)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Verify timestamp
    if (!dispatch_operation(ctx, OP_VERIFY_TIMESTAMP, session)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Final crypto validation
    if (!ctx->crypto->ops->validate(ctx->crypto)) {
        transition_state(ctx, STATE_ERROR);
        return 0;
    }

    // Success - transition to COMPLETE
    if (!transition_state(ctx, STATE_COMPLETE)) {
        return 0;
    }

    session->is_valid = 1;
    return 1;
}

// Cleanup global context
void cleanup_global_context(GlobalContext* ctx) {
    if (!ctx) return;

    // Clean crypto
    if (ctx->crypto) {
        ctx->crypto->ops->cleanup(ctx->crypto);
        free(ctx->crypto);
    }

    // Clean audit trail
    AuditEntry* current = ctx->audit_head;
    while (current) {
        AuditEntry* next = current->next;
        free(current);
        current = next;
    }

    // Clean validation tree
    if (ctx->validation_tree) {
        free(ctx->validation_tree->left);
        free(ctx->validation_tree->right);
        free(ctx->validation_tree);
    }

    free(ctx);
}

// Complex indirect call through multiple function pointers
typedef int (*validation_chain_t)(GlobalContext*, Session*);

int execute_validation_chain(GlobalContext* ctx, Session* session,
                           validation_chain_t* chain, int chain_length) {
    if (!ctx || !session || !chain || chain_length <= 0) {
        return 0;
    }

    for (int i = 0; i < chain_length; i++) {
        if (!chain[i]) {
            return 0;
        }

        if (!chain[i](ctx, session)) {
            return 0;
        }
    }

    return 1;
}

// Main function that ties everything together
int main(int argc, char* argv[]) {
    int result = 0;

    // Initialize global context
    GlobalContext* ctx = init_global_context();
    if (!ctx) {
        printf("Failed to initialize context\n");
        return 1;
    }

    // Create test user with specific patterns
    User user = {
        .user_id = 0x1337,
        .username = "testuser",
        .access_level = 3,
        .flags = 0x0001  // Active flag
    };

    // Create test license with obfuscated values
    License license = {
        .magic = OBFS_CONST(0x7B424BB5),  // Obfuscated 0xDEADBEEF
        .version = 0x0200,
        .serial = 0x7B3E0000,  // Will be validated against username hash
        .key = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50},
        .checksum = 0  // Will be calculated
    };

    // Calculate correct checksum using obfuscated function
    license.checksum = license.magic ^ license.version ^ license.serial;
    for (int i = 0; i < 16; i++) {
        license.checksum = ROT_CONST(license.checksum, 3) + license.key[i];
    }

    // Create session with timestamp
    Session* session = (Session*)calloc(1, sizeof(Session));
    if (!session) {
        printf("Failed to create session\n");
        cleanup_global_context(ctx);
        return 1;
    }

    session->user = &user;
    session->license = &license;
    session->session_id = complex_hash((uint8_t*)&user, sizeof(User), MAGIC_SEED);
    session->is_valid = 0;
    session->timestamp = time(NULL) - 3600; // 1 hour ago

    // Set up validation chain with multiple validators
    validation_chain_t validators[] = {
        perform_stateful_validation,
        // Could add more validators here
    };

    // Execute validation through indirect calls
    result = execute_validation_chain(ctx, session, validators,
                                    sizeof(validators) / sizeof(validators[0]));

    // Print results based on state
    if (result && session->is_valid && ctx->current_state == STATE_COMPLETE) {
        printf("SUCCESS: Full validation completed!\n");
        printf("Session ID: 0x%08X\n", session->session_id);
        printf("User: %s (Level %d)\n", session->user->username,
               session->user->access_level);
        printf("State transitions: %d\n", ctx->transition_count);

        // Dump audit trail
        printf("\nAudit Trail:\n");
        AuditEntry* entry = ctx->audit_head;
        int count = 0;
        while (entry && count < 10) {
            printf("  [%d] Action 0x%02X: %s\n", count++, entry->action_type, entry->details);
            entry = entry->next;
        }
    } else {
        printf("FAILURE: Validation failed!\n");
        printf("Current state: %d\n", ctx->current_state);
        printf("Transitions: %d\n", ctx->transition_count);

        // Check specific failure points
        if (ctx->current_state == STATE_ERROR) {
            printf("Error occurred during validation\n");

            // Print last audit entries
            AuditEntry* entry = ctx->audit_tail;
            int count = 0;
            while (entry && count < 3) {
                printf("  Recent: %s\n", entry->details);
                entry = entry->prev;
                count++;
            }
        }
    }

    // Cleanup
    free(session);
    cleanup_global_context(ctx);

    return result ? 0 : 1;
}