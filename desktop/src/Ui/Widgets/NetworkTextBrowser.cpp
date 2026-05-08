#include "NetworkTextBrowser.hpp"

#include <QDebug>
#include <QScrollBar>
#include <QTimer>

NetworkTextBrowser::NetworkTextBrowser(QWidget* parent)
    : QTextBrowser(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
  // Enable external links to open in browser
  setOpenExternalLinks(true);

  // Connect network manager
  connect(m_networkManager, &QNetworkAccessManager::finished, this, &NetworkTextBrowser::onImageDownloaded);
}

QVariant NetworkTextBrowser::loadResource(int type, const QUrl& name)
{
  // Only handle image resources
  if (type != QTextDocument::ImageResource)
  {
    return QTextBrowser::loadResource(type, name);
  }

  // Prevent infinite recursion - if we're already loading this URL, return empty
  if (m_currentlyLoading.contains(name))
  {
    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    return placeholder;
  }

  // Check our cache first (don't call document()->resource() to avoid recursion)
  if (m_imageCache.contains(name))
  {
    QImage image;
    if (image.loadFromData(m_imageCache[name]))
    {
      return image;
    }
  }

  // Only load http/https URLs
  if (name.scheme() == "http" || name.scheme() == "https")
  {
    // Avoid duplicate requests
    if (!m_pendingImages.contains(name))
    {
      m_pendingImages.insert(name);

      QNetworkRequest request(name);
      request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
      request.setHeader(
          QNetworkRequest::UserAgentHeader,
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 "
          "Safari/537.36");

      // Some servers require Accept header
      request.setRawHeader("Accept", "image/webp,image/apng,image/*,*/*;q=0.8");

      m_networkManager->get(request);
    }

    // Return a small placeholder image
    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    return placeholder;
  }

  // For data: URLs, let the base class handle them
  if (name.scheme() == "data")
  {
    return QTextBrowser::loadResource(type, name);
  }

  // For other schemes (file:, qrc:), use default handling with recursion guard
  m_currentlyLoading.insert(name);
  QVariant result = QTextBrowser::loadResource(type, name);
  m_currentlyLoading.remove(name);
  return result;
}

void NetworkTextBrowser::onImageDownloaded(QNetworkReply* reply)
{
  reply->deleteLater();

  QUrl url = reply->url();
  m_pendingImages.remove(url);

  if (reply->error() != QNetworkReply::NoError)
  {
    qDebug() << "Failed to load image:" << url.toString().left(100) << "-" << reply->errorString();
    return;
  }

  QByteArray data = reply->readAll();
  if (data.isEmpty())
  {
    qDebug() << "Empty image data from:" << url.toString().left(100);
    return;
  }

  // Load and validate image
  QImage image;
  if (!image.loadFromData(data))
  {
    qDebug() << "Failed to decode image from:" << url.toString().left(100);
    return;
  }

  // Cache the image data
  m_imageCache[url] = data;

  // Add to document resources so it won't be re-requested
  document()->addResource(QTextDocument::ImageResource, url, image);

  // Remember scroll position
  int scrollPos = verticalScrollBar()->value();

  // Trigger a layout update without re-parsing HTML
  // This forces Qt to re-render with the newly loaded images
  document()->markContentsDirty(0, document()->characterCount());
  viewport()->update();

  // Restore scroll position
  verticalScrollBar()->setValue(scrollPos);
}
