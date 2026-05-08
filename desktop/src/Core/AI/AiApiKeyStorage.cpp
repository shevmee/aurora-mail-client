#include "AiApiKeyStorage.hpp"

AiApiKeyStorage::AiApiKeyStorage(const QString& organization, const QString& application)
    : backend_(organization, application)
{
}

QString AiApiKeyStorage::load() const
{
  return backend_.retrieve(QString::fromLatin1(STORAGE_KEY));
}

void AiApiKeyStorage::save(const QString& apiKey)
{
  backend_.store(QString::fromLatin1(STORAGE_KEY), apiKey);
}

void AiApiKeyStorage::clear()
{
  backend_.remove(QString::fromLatin1(STORAGE_KEY));
}
