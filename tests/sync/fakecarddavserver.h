#ifndef KALBURATOR_TESTS_FAKECARDDAVSERVER_H
#define KALBURATOR_TESTS_FAKECARDDAVSERVER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

/**
 * @brief Minimal fake CardDAV server fixture for CardDavProvider tests.
 *
 * Listens on QHostAddress::LocalHost on a test-allocated random free port.
 * Handles the discovery PROPFIND chain and CRUD operations on vCards:
 *
 *   PROPFIND "/"                              -> current-user-principal href
 *   PROPFIND "/principals/users/<u>/"         -> addressbook-home-set href
 *   PROPFIND "/addressbooks/<u>/"  (Depth:1)  -> addressbook list
 *   GET  "/addressbooks/<u>/<book>/<uid>.vcf" -> in-memory vCard or 404
 *   PUT  "/addressbooks/<u>/<book>/<uid>.vcf" -> store vCard, return 201/204
 *   DELETE "/addressbooks/<u>/<book>/<uid>.vcf" -> remove vCard (honors If-Match)
 *
 * Configurable failure modes for negative tests:
 *   - setReturn401(true) : every request gets 401 Unauthorized
 *   - setReturn500(true) : every request gets 500 Internal Server Error
 *
 * The server consumes the full HTTP request (parsing Content-Length to
 * detect end-of-body), then writes a single response and closes the
 * connection. This is sufficient for the single-request-per-connection
 * pattern used by CardDAV discovery and CRUD flows.
 */
class FakeCardDavServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit FakeCardDavServer(QObject *parent = nullptr);
    ~FakeCardDavServer() override;

    /// Bind to localhost on a random free port. Returns true on success.
    bool startListening();

    /// e.g. "http://127.0.0.1:<port>/"
    QUrl baseUrl() const;

    void setReturn401(bool on) { m_return401 = on; }
    void setReturn500(bool on) { m_return500 = on; }

    /// Emulate a NextCloud-style deployment (RFC 6764 well-known discovery):
    ///   - the DAV endpoints live under @p contextPath (e.g. "/remote.php/dav")
    ///   - GET/PROPFIND "/.well-known/carddav" returns 301 -> "<contextPath>/"
    ///   - PROPFIND on the bare root "/" returns 405 (it is the web UI, not DAV)
    /// All principal/home/addressbook hrefs are emitted under @p contextPath.
    /// Pass an empty string to restore the default root-served behavior.
    void setContextPath(const QString &contextPath) { m_contextPath = contextPath; }

    /**
     * @brief Insert an artificial delay (ms) before sending any response.
     *
     * Used by cancellation tests: schedule cancel() to fire while the backend
     * is blocked in QEventLoop::exec() waiting for this delayed response.
     * A value of 0 (default) means respond immediately.
     */
    void setResponseDelayMs(int ms) { m_responseDelayMs = ms; }

    /// Each pair is (collectionId, displayName). Default is one addressbook
    /// "Personal" with id "personal".
    void setAddressbooks(const QList<QPair<QString, QString>> &books);

    /// Pre-populate an addressbook with vCard blobs. Each blob must contain
    /// a UID field; the server derives the resource filename from the UID.
    /// collectionId must match one of the ids set via setAddressbooks().
    void setSeedRecords(const QString &collectionId,
                        const QList<QByteArray> &vcards);

    /// Force-regenerate the ETag for an existing record without changing its data.
    /// Used by stale-ETag tests to make the backend's cached ETag go stale.
    /// Returns false if the record is not found.
    bool bumpEtag(const QString &collectionId, const QString &uid);

    /// Returns true if the given addressbook collectionId contains a record
    /// with the given uid. Used by E2E tests to assert palm→server propagation.
    bool hasContact(const QString &collectionId, const QString &uid) const;

    /// Returns all stored raw vCard blobs for the given addressbook.
    QList<QByteArray> storedRecords(const QString &collectionId) const;

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    struct VCardRecord {
        QByteArray data;
        QString    etag; ///< quoted ETag string, e.g. "\"abc123\""
    };

    void handleRequest(QTcpSocket *socket, const QByteArray &fullRequest);

    void writeResponse(QTcpSocket *socket,
                       int statusCode,
                       const QByteArray &reasonPhrase,
                       const QByteArray &body,
                       const QByteArray &extraHeaders = QByteArray());

    QString xmlForPrincipal() const;
    QString xmlForHome() const;
    QString xmlForAddressbooks() const;
    QString xmlForCards(const QString &collectionId) const;

    void handleGet(QTcpSocket *socket, const QString &path);
    void handlePut(QTcpSocket *socket, const QString &path,
                   const QByteArray &body,
                   const QByteArray &ifMatch = QByteArray(),
                   const QByteArray &ifNoneMatch = QByteArray());
    void handleDelete(QTcpSocket *socket, const QString &path,
                      const QByteArray &ifMatch);

    /// Extract a header value (case-insensitive name) from the raw request.
    static QByteArray extractHeader(const QByteArray &rawHeaders,
                                    const QByteArray &name);

    /// Generate a short ETag for the given data.
    static QString makeEtag(const QByteArray &data);

    /// Derive resource uid from path "/addressbooks/<u>/<book>/<uid>.vcf".
    /// Returns empty string if path does not match.
    static QString uidFromPath(const QString &path);

    /// Map collectionId -> (uid -> VCardRecord)
    QHash<QString, QHash<QString, VCardRecord>> m_store;

    /// Ordered list of (collectionId, displayName).
    QList<QPair<QString, QString>> m_addressbooks;

    bool m_return401 = false;
    bool m_return500 = false;
    QString m_contextPath;  ///< empty => DAV served at root; else NextCloud-style
    int  m_responseDelayMs = 0; ///< Artificial response delay for cancellation tests.
};

#endif // KALBURATOR_TESTS_FAKECARDDAVSERVER_H
