#ifndef KALBURATOR_TESTS_FAKECALDAVSERVER_H
#define KALBURATOR_TESTS_FAKECALDAVSERVER_H

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

/**
 * @brief Minimal fake CalDAV server fixture for CalDavProvider tests.
 *
 * Listens on QHostAddress::LocalHost on a test-allocated random free port.
 * Handles the three PROPFIND requests CalDavCapabilityDiscovery walks:
 *
 *   - PROPFIND "/"                          -> current-user-principal href
 *   - PROPFIND "/principals/users/testuser/" -> calendar-home-set href
 *   - PROPFIND "/calendars/testuser/"       -> calendar list (Depth 1)
 *
 * Configurable failure modes for negative tests:
 *   - setReturn401(true) : every request gets 401 Unauthorized
 *   - setReturn500(true) : every request gets 500 Internal Server Error
 *   - setCalendars(...)  : control which calendars are reported (an
 *                          empty list yields a multistatus with no
 *                          calendar responses)
 *
 * The server consumes the full HTTP request (parsing Content-Length to
 * detect end-of-body), then writes a single response and closes the
 * connection. PROPFIND request bodies are < 1KB so this is sufficient
 * for the discovery flow.
 */
class FakeCalDavServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit FakeCalDavServer(QObject *parent = nullptr);
    ~FakeCalDavServer() override;

    /// Bind to localhost on a random free port. Returns true on success.
    bool startListening();

    /// e.g. "http://127.0.0.1:<port>/"
    QUrl baseUrl() const;

    void setReturn401(bool on)   { m_return401 = on; }
    void setReturn500(bool on)   { m_return500 = on; }

    /// Each pair is (displayName, href). Default is one calendar
    /// "Personal" at "/calendars/testuser/personal/".
    void setCalendars(const QList<QPair<QString, QString>> &cals);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    void handleRequest(QTcpSocket *socket, const QByteArray &fullRequest);
    void writeResponse(QTcpSocket *socket,
                       int statusCode,
                       const QByteArray &reasonPhrase,
                       const QByteArray &body);

    QString xmlForPrincipal() const;
    QString xmlForHome() const;
    QString xmlForCalendars() const;

    bool m_return401 = false;
    bool m_return500 = false;
    QList<QPair<QString, QString>> m_calendars;
};

#endif // KALBURATOR_TESTS_FAKECALDAVSERVER_H
