#include "KeychainBackend.hpp"

#include <qt6keychain/keychain.h>

#include <QDebug>
#include <QEventLoop>

KeychainBackend::KeychainBackend(const QString& /*organization*/, const QString& application) : service_name_(application)
{
  qDebug() << "KeychainBackend: Using system keychain for secure token storage";
}

void KeychainBackend::store(const QString& key, const QString& value)
{
  QKeychain::WritePasswordJob job(service_name_);
  job.setAutoDelete(false);
  job.setKey(key);
  job.setTextData(value);

  QEventLoop loop;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() != QKeychain::NoError)
  {
    qWarning() << "KeychainBackend: Failed to store credential:" << job.errorString();
  }
}

QString KeychainBackend::retrieve(const QString& key) const
{
  QKeychain::ReadPasswordJob job(service_name_);
  job.setAutoDelete(false);
  job.setKey(key);

  QEventLoop loop;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() == QKeychain::NoError)
  {
    return job.textData();
  }

  if (job.error() != QKeychain::EntryNotFound)
  {
    qWarning() << "KeychainBackend: Failed to retrieve credential:" << job.errorString();
  }

  return QString();
}

void KeychainBackend::remove(const QString& key)
{
  QKeychain::DeletePasswordJob job(service_name_);
  job.setAutoDelete(false);
  job.setKey(key);

  QEventLoop loop;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound)
  {
    qWarning() << "KeychainBackend: Failed to remove credential:" << job.errorString();
  }
}
