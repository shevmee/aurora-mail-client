#ifndef OAUTHREDIRECTSERVER_HPP
#define OAUTHREDIRECTSERVER_HPP

#include <QObject>
#include <QTcpServer>

class QTcpSocket;

/**
 * @class OAuthRedirectServer
 * @brief Local HTTP server for handling OAuth redirect callbacks.
 *
 * This class encapsulates the low-level HTTP server logic for OAuth:
 * - Listens on a local port for the OAuth redirect
 * - Parses HTTP requests and extracts authorization code/error
 * - Sends appropriate HTML responses to the browser
 * - Ignores irrelevant requests (favicon.ico, etc.)
 */
class OAuthRedirectServer : public QObject
{
    Q_OBJECT

public:
    explicit OAuthRedirectServer(QObject* parent = nullptr);
    ~OAuthRedirectServer() override = default;

    // Non-copyable (QObject semantics)
    Q_DISABLE_COPY_MOVE(OAuthRedirectServer)

    /**
     * @brief Starts listening on a local port.
     * @param port Port to listen on (0 = auto-assign ephemeral port).
     * @return True if server started successfully.
     */
    bool listen(quint16 port = 0);

    /**
     * @brief Gets the port the server is listening on.
     * @return Port number, or 0 if not listening.
     */
    [[nodiscard]] quint16 serverPort() const;

    /**
     * @brief Checks if the server is currently listening.
     */
    [[nodiscard]] bool isListening() const;

    /**
     * @brief Stops the server and closes all connections.
     */
    void close();

signals:
    /**
     * @brief Emitted when an authorization code is received.
     * @param code The authorization code from the OAuth provider.
     * @param state The state parameter for CSRF verification.
     */
    void authCodeReceived(const QString& code, const QString& state);

    /**
     * @brief Emitted when an error is received from the OAuth provider.
     * @param errorDescription Human-readable error description.
     */
    void errorReceived(const QString& errorDescription);

    /**
     * @brief Emitted when the server fails to start or encounters an error.
     * @param error Error message.
     */
    void serverError(const QString& error);

private slots:
    void handleNewConnection();

private:
    /**
     * @brief Processes a complete HTTP request.
     * @param socket The socket with the request data.
     * @param requestData The raw HTTP request.
     */
    void processRequest(QTcpSocket* socket, const QByteArray& requestData);

    /**
     * @brief Sends an HTTP response and closes the socket.
     * @param socket Target socket.
     * @param statusCode HTTP status code (e.g., "200 OK").
     * @param body HTML response body.
     */
    void sendResponse(QTcpSocket* socket, const QString& statusCode, const QString& body);

    QTcpServer* server_{nullptr};
};

#endif // OAUTHREDIRECTSERVER_HPP
