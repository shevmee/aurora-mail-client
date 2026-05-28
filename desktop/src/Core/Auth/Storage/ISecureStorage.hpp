#ifndef ISECURESTORAGE_HPP
#define ISECURESTORAGE_HPP

#include <QString>
#include <concepts>

/**
 * @concept SecureStorageConcept
 * @brief Defines the required interface for secure storage backends.
 *
 * Any class satisfying this concept can be used as a TokenStorage backend.
 * This enables compile-time polymorphism without virtual function overhead.
 */
template<typename T>
concept SecureStorageConcept = requires(
  T storage,
  const T constStorage,
  const QString& key,
  const QString& value
) {
  // Store a secret value
  { storage.store(key, value) } -> std::same_as<void>;

  // Retrieve a secret value
  { constStorage.retrieve(key) } -> std::same_as<QString>;

  // Remove a secret
  { storage.remove(key) } -> std::same_as<void>;

  // Check if storage is secure (compile-time constant)
  { T::isSecure() } -> std::same_as<bool>;
};

#endif  // ISECURESTORAGE_HPP
