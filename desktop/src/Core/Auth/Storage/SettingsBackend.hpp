#ifndef SETTINGSBACKEND_HPP
#define SETTINGSBACKEND_HPP

#include <QSettings>
#include <QString>

#include "ISecureStorage.hpp"

/**
 * @class SettingsBackend
 * @brief QSettings-based storage backend for development.
 *
 * @warning NOT SECURE for production use! Stores secrets in plain text.
 * Use only for development when QtKeychain is not available.
 */
class SettingsBackend
{
 public:
  /**
   * @brief Constructs backend with organization and application identifiers.
   * @param organization Organization name for QSettings.
   * @param application Application name for QSettings.
   */
  explicit SettingsBackend(
      const QString& organization = QStringLiteral("AuroraMail"),
      const QString& application = QStringLiteral("OAuth"));

  ~SettingsBackend() = default;

  // Non-copyable, movable (manages settings resources)
  Q_DISABLE_COPY(SettingsBackend)
  SettingsBackend(SettingsBackend&&) noexcept = default;
  SettingsBackend& operator=(SettingsBackend&&) noexcept = default;

  /**
   * @brief Stores a value in QSettings.
   * @param key Unique key identifier.
   * @param value Value to store (stored in PLAIN TEXT).
   */
  void store(const QString& key, const QString& value);

  /**
   * @brief Retrieves a value from QSettings.
   * @param key Unique key identifier.
   * @return Stored value or empty string if not found.
   */
  [[nodiscard]] QString retrieve(const QString& key) const;

  /**
   * @brief Removes a value from QSettings.
   * @param key Unique key identifier.
   */
  void remove(const QString& key);

  /**
   * @brief Returns whether this backend provides secure storage.
   * @return Always false - QSettings is NOT secure.
   */
  [[nodiscard]] static constexpr bool isSecure() noexcept
  {
    return false;
  }

 private:
  QString organization_;
  QString application_;
};

// Verify concept satisfaction
static_assert(SecureStorageConcept<SettingsBackend>, "SettingsBackend must satisfy SecureStorageConcept");

#endif  // SETTINGSBACKEND_HPP
