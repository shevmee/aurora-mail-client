#ifndef NETWORK_TEXT_BROWSER_HPP
#define NETWORK_TEXT_BROWSER_HPP

#include <QBuffer>
#include <QImage>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTextBrowser>
#include <QUrl>

/**
 * @brief A QTextBrowser subclass that can load external images from URLs.
 *
 * Standard QTextBrowser doesn't load external resources for security reasons.
 * This class overrides loadResource() to fetch images from the network.
 */
class NetworkTextBrowser : public QTextBrowser
{
  Q_OBJECT

 public:
  explicit NetworkTextBrowser(QWidget* parent = nullptr);
  ~NetworkTextBrowser() override = default;

 protected:
  /**
   * @brief Override to load external images from URLs.
   */
  QVariant loadResource(int type, const QUrl& name) override;

 private slots:
  void onImageDownloaded(QNetworkReply* reply);

 private:
  QNetworkAccessManager* m_networkManager;
  QMap<QUrl, QByteArray> m_imageCache;
  QSet<QUrl> m_pendingImages;
  QSet<QUrl> m_currentlyLoading;  // Prevent re-entrant calls
};

#endif  // NETWORK_TEXT_BROWSER_HPP
