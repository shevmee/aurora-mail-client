#include "OAuthRedirectServer.hpp"
#include "AuthPageRenderer.hpp"

#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

namespace {
    // Maximum time to wait for complete HTTP request (ms)
    constexpr int kReadTimeoutMs = 3000;
    
    // Maximum request size to prevent memory exhaustion
    constexpr qint64 kMaxRequestSize = 8192;
}

OAuthRedirectServer::OAuthRedirectServer(QObject* parent)
    : QObject(parent)
    , server_(new QTcpServer(this))
{
    connect(server_, &QTcpServer::newConnection, 
            this, &OAuthRedirectServer::handleNewConnection);
}

bool OAuthRedirectServer::listen(quint16 port)
{
    if (server_->isListening()) {
        close();
    }

    if (!server_->listen(QHostAddress::LocalHost, port)) {
        emit serverError("Failed to start callback server: " + server_->errorString());
        return false;
    }

    qDebug() << "OAuth redirect server listening on port" << server_->serverPort();
    return true;
}

quint16 OAuthRedirectServer::serverPort() const
{
    return server_->serverPort();
}

bool OAuthRedirectServer::isListening() const
{
    return server_->isListening();
}

void OAuthRedirectServer::close()
{
    if (server_->isListening()) {
        server_->close();
    }
}

void OAuthRedirectServer::handleNewConnection()
{
    QTcpSocket* socket = server_->nextPendingConnection();
    if (socket == nullptr) {
        return;
    }

    // Read the complete HTTP request
    // Use waitForReadyRead to handle partial reads properly
    QByteArray requestData;
    
    while (socket->waitForReadyRead(kReadTimeoutMs)) {
        requestData.append(socket->readAll());
        
        // Check if we have a complete HTTP request (ends with \r\n\r\n)
        if (requestData.contains("\r\n\r\n")) {
            break;
        }
        
        // Prevent memory exhaustion from malicious requests
        if (requestData.size() > kMaxRequestSize) {
            // TODO: try to use 413 Request Entity Too Large response
            qWarning() << "Request too large, closing connection";
            socket->close();
            socket->deleteLater();
            return;
        }
    }

    if (requestData.isEmpty()) {
        socket->close();
        socket->deleteLater();
        return;
    }

    processRequest(socket, requestData);
}

void OAuthRedirectServer::processRequest(QTcpSocket* socket, const QByteArray& requestData)
{
    QString request = QString::fromUtf8(requestData);
    
    // Parse the request line: GET /path?query HTTP/1.1
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) {
        socket->close();
        socket->deleteLater();
        return;
    }

    QStringList requestLine = lines[0].split(" ");
    if (requestLine.size() < 2) {
        socket->close();
        socket->deleteLater();
        return;
    }

    QString path = requestLine[1];
    
    // Ignore favicon.ico and other irrelevant requests
    if (path.startsWith("/favicon")) {
        sendResponse(socket, "404 Not Found", QString());
        return;
    }

    qDebug() << "Processing OAuth callback request";

    // Parse query parameters
    QUrl url("http://localhost" + path);
    QUrlQuery query(url);

    QString code = query.queryItemValue("code");
    QString state = query.queryItemValue("state");
    QString error = query.queryItemValue("error");
    QString errorDesc = query.queryItemValue("error_description");

    // Determine response based on result
    QString responseBody;
    QString httpStatus;

    if (!error.isEmpty()) {
        httpStatus = "400 Bad Request";
        responseBody = AuthPageRenderer::renderErrorPage(
            errorDesc.isEmpty() ? error : errorDesc);
    } else if (code.isEmpty()) {
        httpStatus = "400 Bad Request";
        responseBody = AuthPageRenderer::renderMissingCodePage();
    } else {
        httpStatus = "200 OK";
        responseBody = AuthPageRenderer::renderSuccessPage();
    }

    // Send response to browser
    sendResponse(socket, httpStatus, responseBody);

    // Close the server - we only need one callback
    close();

    // Emit appropriate signal
    if (!error.isEmpty()) {
        emit errorReceived(errorDesc.isEmpty() ? error : errorDesc);
    } else if (!code.isEmpty()) {
        emit authCodeReceived(code, state);
    }
}

void OAuthRedirectServer::sendResponse(QTcpSocket* socket, const QString& statusCode, const QString& body)
{
    QByteArray bodyBytes = body.toUtf8();
    
    QString response = QString(
        "HTTP/1.1 %1\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %2\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).arg(statusCode).arg(bodyBytes.size());

    socket->write(response.toUtf8());
    socket->write(bodyBytes);
    socket->flush();
    socket->waitForBytesWritten(1000);
    socket->close();
    socket->deleteLater();
}
