#pragma once

#include <string>

namespace llm_re {

/**
 * Manages SSH key pair for remote debugging connections
 *
 * Keys are stored at ~/.idapro/ida_swarm_ssh_key (private) and
 * ~/.idapro/ida_swarm_ssh_key.pub (public) to persist across runs.
 * Keys are generated without passphrase for automated use.
 */
class SSHKeyManager {
public:
    /**
     * Ensure SSH key pair exists, generating if necessary
     *
     * @return true if keys exist or were successfully generated
     */
    static bool ensure_key_pair_exists();

    /**
     * Get path to private key file
     *
     * @return Absolute path to private key (~/.idapro/ida_swarm_ssh_key)
     */
    static std::string get_private_key_path();

    /**
     * Get path to public key file
     *
     * @return Absolute path to public key (~/.idapro/ida_swarm_ssh_key.pub)
     */
    static std::string get_public_key_path();

    /**
     * Read and return public key content for display/copying
     *
     * @return Public key content or empty string on error
     */
    static std::string get_public_key_content();

private:
    /**
     * Generate new RSA key pair using ssh-keygen
     *
     * @return true if generation succeeded
     */
    static bool generate_key_pair();
};

} // namespace llm_re
