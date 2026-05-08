#ifndef AI_API_KEY_STORAGE_HPP
#define AI_API_KEY_STORAGE_HPP

#include "Storage/ISecureStorage.hpp"
#include "Storage/SettingsBackend.hpp"
#if AURORA_USE_KEYCHAIN
#include "Storage/KeychainBackend.hpp"
#endif

#include <QString>

#if AURORA_USE_KEYCHAIN
using AiSecureBackend = KeychainBackend;
#else
using AiSecureBackend = SettingsBackend;
#endif

/**
 * @brief Stores the Gemini (Google AI Studio) API key using the same secure backend as OAuth.
 *
 * Legacy keys in QSettings ("AI/Google_ApiKey") are migrated on first load in AIService.
 */
class AiApiKeyStorage
{
public:
    explicit AiApiKeyStorage(
        const QString& organization = QStringLiteral("AuroraMail"),
        const QString& application = QStringLiteral("AuroraMail"));

    [[nodiscard]] QString load() const;
    void save(const QString& apiKey);
    void clear();

    [[nodiscard]] static constexpr bool isSecureStorageEnabled() noexcept
    {
        return AiSecureBackend::isSecure();
    }

private:
    AiSecureBackend backend_;
    static constexpr const char* STORAGE_KEY = "ai_gemini_api_key";
};

#endif
