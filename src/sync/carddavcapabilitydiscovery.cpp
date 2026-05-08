#include "carddavcapabilitydiscovery.h"

#include <QAuthenticator>
#include <QDebug>
#include <QDomDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>

namespace Kalburator::Sync {

// ---------------------------------------------------------------------------
// Namespace URIs
// ---------------------------------------------------------------------------
static const QString NS_DAV     = QStringLiteral("DAV:");
static const QString NS_CARDDAV = QStringLiteral("urn:ietf:params:xml:ns:carddav");

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

CardDavCapabilityDiscovery::CardDavCapabilityDiscovery(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    QObject::connect(m_nam, &QNetworkAccessManager::authenticationRequired,
                     this, [this](QNetworkReply *, QAuthenticator *auth) {
        auth->setUser(m_username);
        auth->setPassword(m_password);
    });
}

CardDavCapabilityDiscovery::~CardDavCapabilityDiscovery()
{
    // If a discovery is still in flight, resolve the promise with an empty
    // list so the caller's future completes and doesn't block forever.
    if (m_promise) {
        m_promise->addResult(QList<CollectionInfo>{});
        m_promise->finish();
        delete m_promise;
        m_promise = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CardDavCapabilityDiscovery::setCredentials(const QUrl &serverRoot,
                                                const QString &username,
                                                const QString &password)
{
    m_serverRoot = serverRoot;
    m_username   = username;
    m_password   = password;
}

QFuture<QList<CollectionInfo>> CardDavCapabilityDiscovery::discover()
{
    // Discard any previous in-flight promise.
    if (m_promise) {
        m_promise->addResult(QList<CollectionInfo>{});
        m_promise->finish();
        delete m_promise;
        m_promise = nullptr;
    }

    m_principalHref.clear();
    m_homeHref.clear();
    m_addressbookUrls.clear();

    m_promise = new QPromise<QList<CollectionInfo>>();
    m_promise->start();

    stepDiscoverPrincipal();

    return m_promise->future();
}

// ---------------------------------------------------------------------------
// Step 1 — PROPFIND server root for current-user-principal
// ---------------------------------------------------------------------------

void CardDavCapabilityDiscovery::stepDiscoverPrincipal()
{
    QNetworkRequest req(m_serverRoot);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/xml; charset=utf-8"));
    req.setRawHeader("Depth", "0");

    const QByteArray body = buildPropfindXml(
        { QStringLiteral("current-user-principal") },
        {}
    );

    QNetworkReply *reply = m_nam->sendCustomRequest(req, "PROPFIND", body);
    QObject::connect(reply, &QNetworkReply::finished,
                     this, &CardDavCapabilityDiscovery::onPrincipalReplyFinished);
}

void CardDavCapabilityDiscovery::onPrincipalReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        resolveWithError(tr("Internal error: null reply for principal PROPFIND"));
        return;
    }
    reply->deleteLater();

    // Non-2xx → emit error, resolve with empty list.
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status != 0 && status / 100 != 2 && status != 207)) {
        resolveWithError(
            tr("CardDAV principal PROPFIND failed (HTTP %1): %2")
                .arg(status)
                .arg(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();
    m_principalHref = extractHref(body,
                                  QStringLiteral("current-user-principal"),
                                  NS_DAV);

    if (m_principalHref.isEmpty()) {
        // Some servers expose the principal at the root itself.
        m_principalHref = m_serverRoot.toString();
    }

    stepDiscoverHomeSet();
}

// ---------------------------------------------------------------------------
// Step 2 — PROPFIND principal for addressbook-home-set
// ---------------------------------------------------------------------------

void CardDavCapabilityDiscovery::stepDiscoverHomeSet()
{
    QUrl principalUrl(m_principalHref);
    if (principalUrl.isRelative()) {
        principalUrl = m_serverRoot.resolved(principalUrl);
    }

    QNetworkRequest req(principalUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/xml; charset=utf-8"));
    req.setRawHeader("Depth", "0");

    const QByteArray body = buildPropfindXml(
        {},
        { QStringLiteral("addressbook-home-set") }
    );

    QNetworkReply *reply = m_nam->sendCustomRequest(req, "PROPFIND", body);
    QObject::connect(reply, &QNetworkReply::finished,
                     this, &CardDavCapabilityDiscovery::onHomeSetReplyFinished);
}

void CardDavCapabilityDiscovery::onHomeSetReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        resolveWithError(tr("Internal error: null reply for home-set PROPFIND"));
        return;
    }
    reply->deleteLater();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status != 0 && status / 100 != 2 && status != 207)) {
        resolveWithError(
            tr("CardDAV home-set PROPFIND failed (HTTP %1): %2")
                .arg(status)
                .arg(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();
    m_homeHref = extractHref(body,
                             QStringLiteral("addressbook-home-set"),
                             NS_CARDDAV);

    if (m_homeHref.isEmpty()) {
        // No home set — valid response, zero addressbooks.
        resolveWithSuccess({});
        return;
    }

    stepDiscoverAddressbooks();
}

// ---------------------------------------------------------------------------
// Step 3 — PROPFIND home-set at Depth:1 for addressbook collections
// ---------------------------------------------------------------------------

void CardDavCapabilityDiscovery::stepDiscoverAddressbooks()
{
    QUrl homeUrl(m_homeHref);
    if (homeUrl.isRelative()) {
        homeUrl = m_serverRoot.resolved(homeUrl);
    }

    QNetworkRequest req(homeUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/xml; charset=utf-8"));
    req.setRawHeader("Depth", "1");

    const QByteArray body = buildPropfindXml(
        { QStringLiteral("displayname"), QStringLiteral("resourcetype") },
        { QStringLiteral("addressbook-description") }
    );

    QNetworkReply *reply = m_nam->sendCustomRequest(req, "PROPFIND", body);
    QObject::connect(reply, &QNetworkReply::finished,
                     this, &CardDavCapabilityDiscovery::onAddressbooksReplyFinished);
}

void CardDavCapabilityDiscovery::onAddressbooksReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        resolveWithError(tr("Internal error: null reply for addressbook-list PROPFIND"));
        return;
    }
    reply->deleteLater();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status != 0 && status / 100 != 2 && status != 207)) {
        resolveWithError(
            tr("CardDAV addressbook-list PROPFIND failed (HTTP %1): %2")
                .arg(status)
                .arg(reply->errorString()));
        return;
    }

    const QByteArray xmlData = reply->readAll();

    QDomDocument doc;
    QString parseError;
    int parseErrorLine, parseErrorCol;
    QDomDocument::ParseResult result = doc.setContent(
        xmlData, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (!result) {
        qWarning() << "CardDavCapabilityDiscovery: XML parse error:"
                   << "error string from ParseResult";
        resolveWithError(tr("Failed to parse addressbook list"));
        return;
    }

    QList<CollectionInfo> books;

    const QDomNodeList responses =
        doc.elementsByTagNameNS(NS_DAV, QStringLiteral("response"));

    for (int i = 0; i < responses.count(); ++i) {
        const QDomElement respEl = responses.at(i).toElement();
        if (respEl.isNull())
            continue;

        // Href for this resource.
        const QDomNodeList hrefs =
            respEl.elementsByTagNameNS(NS_DAV, QStringLiteral("href"));
        if (hrefs.isEmpty())
            continue;
        const QString href = hrefs.at(0).toElement().text().trimmed();

        // Must have <CARDDAV:addressbook> in resourcetype.
        bool isAddressbook = false;
        const QDomNodeList rtNodes =
            respEl.elementsByTagNameNS(NS_DAV, QStringLiteral("resourcetype"));
        for (int j = 0; j < rtNodes.count(); ++j) {
            const QDomElement rtEl = rtNodes.at(j).toElement();
            if (!rtEl.elementsByTagNameNS(NS_CARDDAV,
                                          QStringLiteral("addressbook")).isEmpty()) {
                isAddressbook = true;
                break;
            }
        }
        if (!isAddressbook)
            continue;

        // Display name.
        QString displayName;
        const QDomNodeList dnNodes =
            respEl.elementsByTagNameNS(NS_DAV, QStringLiteral("displayname"));
        if (!dnNodes.isEmpty())
            displayName = dnNodes.at(0).toElement().text().trimmed();

        // Stable id = last non-empty path segment of the href.
        QString id;
        {
            QString path = QUrl(href).path();
            if (path.endsWith(QLatin1Char('/')))
                path.chop(1);
            const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
            id = (lastSlash >= 0) ? path.mid(lastSlash + 1) : path;
        }
        if (id.isEmpty())
            id = href; // fallback — should not happen with well-formed servers

        if (displayName.isEmpty())
            displayName = id;

        // Resolve href to absolute URL.
        QString absoluteHref = href;
        {
            QUrl u(href);
            if (u.isRelative())
                absoluteHref = m_serverRoot.resolved(u).toString();
        }

        CollectionInfo ci;
        ci.id   = id;
        ci.name = displayName;
        ci.type = QStringLiteral("contacts");

        books.append(ci);
        m_addressbookUrls.insert(id, absoluteHref);
    }

    resolveWithSuccess(books);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QByteArray CardDavCapabilityDiscovery::buildPropfindXml(
    const QStringList &davProps,
    const QStringList &carddavProps) const
{
    QString xml =
        QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<d:propfind xmlns:d=\"DAV:\""
            " xmlns:card=\"urn:ietf:params:xml:ns:carddav\""
            " xmlns:cs=\"http://calendarserver.org/ns/\">\n"
            "  <d:prop>\n");

    for (const QString &prop : davProps) {
        xml += QStringLiteral("    <d:%1/>\n").arg(prop);
    }
    for (const QString &prop : carddavProps) {
        xml += QStringLiteral("    <card:%1/>\n").arg(prop);
    }

    xml += QStringLiteral(
        "  </d:prop>\n"
        "</d:propfind>\n");

    return xml.toUtf8();
}

QString CardDavCapabilityDiscovery::extractHref(
    const QByteArray &xmlData,
    const QString &elementLocalName,
    const QString &elementNamespaceUri) const
{
    QDomDocument doc;
    QDomDocument::ParseResult result = doc.setContent(
        xmlData, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (!result)
        return QString();

    // Find the target element in its namespace.
    const QDomNodeList nodes =
        doc.elementsByTagNameNS(elementNamespaceUri, elementLocalName);

    for (int i = 0; i < nodes.count(); ++i) {
        const QDomElement el = nodes.at(i).toElement();
        if (el.isNull())
            continue;

        // Look for a <DAV:href> child first.
        const QDomNodeList hrefNodes =
            el.elementsByTagNameNS(NS_DAV, QStringLiteral("href"));
        if (!hrefNodes.isEmpty()) {
            const QString text = hrefNodes.at(0).toElement().text().trimmed();
            if (!text.isEmpty())
                return text;
        }

        // Some servers put the path as element text directly.
        const QString text = el.text().trimmed();
        if (!text.isEmpty() && text.startsWith(QLatin1Char('/')))
            return text;
    }

    return QString();
}

void CardDavCapabilityDiscovery::resolveWithError(const QString &msg)
{
    qWarning() << "CardDavCapabilityDiscovery:" << msg;
    emit error(msg);

    if (m_promise) {
        m_promise->addResult(QList<CollectionInfo>{});
        m_promise->finish();
        delete m_promise;
        m_promise = nullptr;
    }
}

void CardDavCapabilityDiscovery::resolveWithSuccess(
    const QList<CollectionInfo> &books)
{
    qDebug() << "CardDavCapabilityDiscovery: discovered"
             << books.size() << "addressbook(s)";

    if (m_promise) {
        m_promise->addResult(books);
        m_promise->finish();
        delete m_promise;
        m_promise = nullptr;
    }
}

} // namespace Kalburator::Sync
