#ifndef KEYCHAINBACKEND_HPP
#define KEYCHAINBACKEND_HPP

#include "ISecureStorage.hpp"

#include <QString>

/**
 * @class KeychainBackend
 * @brief QtKeychain-based secure storage backend.
 *
 * Uses platform-specific secure storage:
 * - macOS: Keychain Services
 * - Windows: Windows Credential Store  
 * - Linux: libsecret (GNOME) / KWallet (KDE)
 *
 * @note Only available when AURORA_USE_KEYCHAIN=ON.
 */
class KeychainBackend
{
public:
    /**
     * @brief Constructs backend with service identifier.
     * @param organization Organization name (unused, for API compatibility).
     * @param application Application/service name for keychain.
     */
    explicit KeychainBackend(const QString& organization = QStringLiteral("AuroraMail"),
                             const QString& application = QStringLiteral("AuroraMail"));

    ~KeychainBackend() = default;

    // Non-copyable, movable (manages keychain resources)
    Q_DISABLE_COPY(KeychainBackend)
    KeychainBackend(KeychainBackend&&) noexcept = default;
    KeychainBackend& operator=(KeychainBackend&&) noexcept = default;

    /**
     * @brief Stores a value securely in the system keychain.
     * @param key Unique key identifier.
     * @param value Secret value to store.
     */
    void store(const QString& key, const QString& value);

    /**
     * @brief Retrieves a value from the system keychain.
     * @param key Unique key identifier.
     * @return Stored value or empty string if not found.
     */
    [[nodiscard]] QString retrieve(const QString& key) const;

    /**
     * @brief Removes a value from the system keychain.
     * @param key Unique key identifier.
     */
    void remove(const QString& key);

    /**
     * @brief Returns whether this backend provides secure storage.
     * @return Always true - Keychain IS secure.
     */
    [[nodiscard]] static constexpr bool isSecure() noexcept { return true; }

private:
    QString service_name_;
};

// Verify concept satisfaction
static_assert(SecureStorageConcept<KeychainBackend>, 
              "KeychainBackend must satisfy SecureStorageConcept");

#endif // KEYCHAINBACKEND_HPP
