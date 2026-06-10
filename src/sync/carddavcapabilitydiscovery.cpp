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
        m_promise.reset();
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
        m_promise.reset();
    }

    m_principalHref.clear();
    m_homeHref.clear();
    m_addressbookUrls.clear();
    m_baseUrl = m_serverRoot;

    m_promise = std::make_unique<QPromise<QList<CollectionInfo>>>();
    m_promise->start();

    // Manual override: skip both the well-known bootstrap and the principal
    // PROPFIND, walking straight from the user-supplied principal.
    if (!m_principalOverride.isEmpty()) {
        m_principalHref = m_principalOverride;
        stepDiscoverHomeSet();
        return m_promise->future();
    }

    // RFC 6764: when only the bare host is supplied (no DAV path), bootstrap
    // the context path via /.well-known/carddav before walking principals.
    // NextCloud and similar serve their web UI at the root (405 to PROPFIND)
    // and the DAV endpoint elsewhere (e.g. /remote.php/dav/), advertised by a
    // redirect from well-known.
    const QString path = m_serverRoot.path();
    if (path.isEmpty() || path == QStringLiteral("/")) {
        stepResolveContextPath();
    } else {
        stepDiscoverPrincipal();
    }

    return m_promise->future();
}

void CardDavCapabilityDiscovery::stepResolveContextPath()
{
    QUrl wellKnown = m_serverRoot;
    wellKnown.setPath(QStringLiteral("/.well-known/carddav"));

    QNetworkRequest req(wellKnown);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/xml; charset=utf-8"));
    req.setRawHeader("Depth", "0");
    // Read the redirect target ourselves rather than letting QNAM follow it:
    // the context path lives in the Location header, and a manual hop avoids
    // any ambiguity about whether the PROPFIND body survives an auto-redirect.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);

    const QByteArray body = buildPropfindXml(
        { QStringLiteral("current-user-principal") }, {});

    QNetworkReply *reply = m_nam->sendCustomRequest(req, "PROPFIND", body);
    QObject::connect(reply, &QNetworkReply::finished,
                     this, &CardDavCapabilityDiscovery::onContextPathReplyFinished);
}

void CardDavCapabilityDiscovery::onContextPathReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        resolveWithError(tr("Internal error: null reply for well-known PROPFIND"));
        return;
    }
    reply->deleteLater();

    const QUrl wellKnownUrl = reply->request().url();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 301 || status == 302 || status == 307 || status == 308) {
        const QByteArray location = reply->rawHeader("Location");
        if (!location.isEmpty()) {
            // Resolve relative against the well-known URL so both absolute
            // (NextCloud) and relative Location values work.
            m_baseUrl = wellKnownUrl.resolved(QUrl(QString::fromUtf8(location)));
            stepDiscoverPrincipal();
            return;
        }
    }

    // No usable redirect (server doesn't implement well-known, or returned an
    // error/2xx without Location): fall back to probing the entered URL
    // directly, preserving pre-RFC-6764 behavior.
    m_baseUrl = m_serverRoot;
    stepDiscoverPrincipal();
}

// ---------------------------------------------------------------------------
// Step 1 — PROPFIND server root for current-user-principal
// ---------------------------------------------------------------------------

void CardDavCapabilityDiscovery::stepDiscoverPrincipal()
{
    QNetworkRequest req(m_baseUrl);
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
        // Some servers expose the principal at the DAV base itself.
        m_principalHref = m_baseUrl.toString();
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
        principalUrl = m_baseUrl.resolved(principalUrl);
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
        homeUrl = m_baseUrl.resolved(homeUrl);
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
    QDomDocument::ParseResult result = doc.setContent(
        xmlData, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (!result) {
        qWarning() << "CardDavCapabilityDiscovery: XML parse error:"
                   << result.errorMessage;
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
                absoluteHref = m_baseUrl.resolved(u).toString();
        }

        CollectionInfo ci;
        ci.id   = id;
        ci.name = displayName;
        ci.type = QStringLiteral("contacts");
        ci.contentTypes << QStringLiteral("VCARD");

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
        m_promise.reset();
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
        m_promise.reset();
    }
}

} // namespace Kalburator::Sync
