#ifndef KALBURATOR_TESTS_FAKECALDAVSERVER_H
#define KALBURATOR_TESTS_FAKECALDAVSERVER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

/**
 * @brief Minimal fake CalDAV server fixture for CalDavProvider tests.
 *
 * Listens on QHostAddress::LocalHost on a test-allocated random free port.
 * Handles the three PROPFIND requests CalDavCapabilityDiscovery walks,
 * plus CalDAV CRUD operations needed for E2E sync tests:
 *
 *   PROPFIND "/"                          -> current-user-principal href
 *   PROPFIND "/principals/users/testuser/" -> calendar-home-set href
 *   PROPFIND "/calendars/testuser/"       -> calendar list (Depth 1)
 *   REPORT  "/calendars/testuser/<cal>/"  -> calendar-query (ETag list)
 *                                            or calendar-multiget (full data)
 *   PUT     "/calendars/testuser/<cal>/<uid>.ics" -> store event
 *   MKCALENDAR <collection href>          -> 201 (or 405 if it exists)
 *   PROPPATCH  <collection href>          -> 207 if known, else 404
 *   DELETE     <collection href>          -> 204 if known, else 404
 *                                            (item DELETE unchanged)
 *
 * The default calendar is "Personal" at "/calendars/testuser/personal/".
 *
 * Configurable failure modes for negative tests:
 *   - setReturn401(true) : every request gets 401 Unauthorized
 *   - setReturn500(true) : every request gets 500 Internal Server Error
 *   - setCalendars(...)  : control which calendars are reported
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

    /// Number of well-formed requests received for @p method (e.g. "PROPFIND",
    /// "REPORT", "PUT") since the last startListening(). Lets tests assert
    /// request shape — e.g. that a primed loadCalendars issues zero additional
    /// PROPFINDs beyond the connect-time discovery walk.
    int requestCount(const QByteArray &method) const;

    void setReturn401(bool on)   { m_return401 = on; }
    void setReturn500(bool on)   { m_return500 = on; }

    /// Emulate a NextCloud-style deployment (RFC 6764 well-known discovery):
    ///   - the DAV endpoints live under @p contextPath (e.g. "/remote.php/dav")
    ///   - GET/PROPFIND "/.well-known/caldav" returns 301 -> "<contextPath>/"
    ///   - PROPFIND on the bare root "/" returns 405 (it is the web UI, not DAV)
    /// All principal/home/calendar hrefs are emitted under @p contextPath.
    /// Pass an empty string to restore the default root-served behavior.
    void setContextPath(const QString &contextPath) { m_contextPath = contextPath; }

    /// Each pair is (displayName, href). Default is one calendar
    /// "Personal" at "/calendars/testuser/personal/".
    void setCalendars(const QList<QPair<QString, QString>> &cals);

    /// Mark the listed calendar hrefs as read-only. The calendar-list PROPFIND
    /// then advertises a <current-user-privilege-set> containing only <read/>
    /// for those collections (no write/write-content/bind/unbind), so
    /// CalDavCapabilityDiscovery reports them as writable=false. Calendars not
    /// listed here emit no privilege-set and default to writable. hrefs must
    /// match those set via setCalendars() (e.g. "/calendars/testuser/work/").
    void setReadOnlyCalendars(const QStringList &hrefs) { m_readOnlyHrefs = hrefs; }

    /// Override the <cal:supported-calendar-component-set> advertised for one
    /// calendar href (e.g. {"VTODO"} for a tasks-only list). Calendars without
    /// an override keep the default VEVENT-only set. href must match one set
    /// via setCalendars().
    void setCalendarComponents(const QString &href, const QStringList &components)
    {
        m_componentsByHref.insert(href, components);
    }

    /// Pre-populate a calendar collection with iCal event blobs.
    /// Each blob must be a full VCALENDAR containing a UID property.
    /// collectionHref must match one of the hrefs set via setCalendars()
    /// (e.g. "/calendars/testuser/personal/").
    void setSeedEvents(const QString &collectionHref,
                       const QList<QByteArray> &events);

    /// Returns true if an event with the given UID exists in the collection.
    bool hasEvent(const QString &collectionHref, const QString &uid) const;

    /// Returns the raw iCal blobs currently stored for a collection.
    QList<QByteArray> storedEvents(const QString &collectionHref) const;

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    struct IcsRecord {
        QByteArray data;
        QString    etag;
    };

    void handleRequest(QTcpSocket *socket, const QByteArray &fullRequest);
    void handleReport(QTcpSocket *socket, const QString &path,
                      const QByteArray &body);
    void handlePut(QTcpSocket *socket, const QString &path,
                   const QByteArray &body);
    void handleDelete(QTcpSocket *socket, const QString &path);
    void handleMkCalendar(QTcpSocket *socket, const QString &path);
    void handleProppatch(QTcpSocket *socket, const QString &path);
    bool isKnownCollection(const QString &href) const;
    void writeResponse(QTcpSocket *socket,
                       int statusCode,
                       const QByteArray &reasonPhrase,
                       const QByteArray &body,
                       const QByteArray &extraHeaders = QByteArray());

    QString xmlForPrincipal() const;
    QString xmlForHome() const;
    QString xmlForCalendars() const;
    QByteArray xmlForCalendarQuery(const QString &collectionHref) const;
    QByteArray xmlForCalendarMultiget(const QString &collectionHref,
                                      const QList<QString> &hrefs) const;

    static QString uidFromIcs(const QByteArray &ics);
    static QString uidFromPath(const QString &path);
    static QString makeEtag(const QByteArray &data);
    static QList<QString> parseHrefsFromBody(const QByteArray &body);

    bool m_return401 = false;
    bool m_return500 = false;
    QString m_contextPath;  // empty => DAV served at root; else NextCloud-style
    QList<QPair<QString, QString>> m_calendars;
    QSet<QString> m_createdCollections;  // hrefs created via MKCALENDAR
    QStringList m_readOnlyHrefs;  // hrefs advertised with read-only privilege-set
    QHash<QString, QStringList> m_componentsByHref;  // component-set overrides
    QHash<QByteArray, int> m_requestCounts;  // method -> count, reset on startListening()

    /// Keyed by collectionHref (e.g. "/calendars/testuser/personal/")
    /// then by UID.
    QHash<QString, QHash<QString, IcsRecord>> m_store;
};

#endif // KALBURATOR_TESTS_FAKECALDAVSERVER_H
