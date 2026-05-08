#include "SettingsBackend.hpp"

#include <QDebug>

SettingsBackend::SettingsBackend(const QString& organization, const QString& application)
    : organization_(organization),
      application_(application)
{
  qWarning() << "SettingsBackend: Using QSettings for token storage (INSECURE - development only)";
}

void SettingsBackend::store(const QString& key, const QString& value)
{
  QSettings settings(organization_, application_);
  settings.setValue(QStringLiteral("tokens/%1").arg(key), value);
}

QString SettingsBackend::retrieve(const QString& key) const
{
  QSettings settings(organization_, application_);
  return settings.value(QStringLiteral("tokens/%1").arg(key)).toString();
}

void SettingsBackend::remove(const QString& key)
{
  QSettings settings(organization_, application_);
  settings.remove(QStringLiteral("tokens/%1").arg(key));
}
