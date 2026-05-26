#include "caldavcapabilitydiscovery.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QAuthenticator>
#include <QDomDocument>
#include <QDateTime>
#include <QDebug>

namespace Kalburator::Sync {

CalDavCapabilityDiscovery::CalDavCapabilityDiscovery(const QUrl &serverUrl,
                                                       const QString &username,
                                                       const QString &password,
                                                       QObject *parent)
    : QObject(parent)
    , m_serverUrl(serverUrl)
    , m_username(username)
    , m_password(password)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Handle authentication
    connect(m_networkManager, &QNetworkAccessManager::authenticationRequired,
            this, [this](QNetworkReply *, QAuthenticator *authenticator) {
        authenticator->setUser(m_username);
        authenticator->setPassword(m_password);
    });
}

CalDavCapabilityDiscovery::~CalDavCapabilityDiscovery() = default;

void CalDavCapabilityDiscovery::start()
{
    if (m_running) {
        qWarning() << "CalDavCapabilityDiscovery: already running";
        return;
    }

    m_running = true;
    m_errorMessage.clear();
    m_capabilities = DiscoveredCapabilities();
    m_calendarUrls.clear();
    m_capabilities.discoveredAt = QDateTime::currentDateTimeUtc();
    m_baseUrl = m_serverUrl;

    emit progressMessage(tr("Discovering server capabilities..."));

    // Manual override: skip both the well-known bootstrap and the principal
    // PROPFIND, walking straight from the user-supplied principal.
    if (!m_principalOverride.isEmpty()) {
        m_principalUrl = m_principalOverride;
        emit progressMessage(tr("Using manual principal: %1").arg(m_principalUrl));
        discoverCalendarHome();
        return;
    }

    // RFC 6764: when the user supplies only the bare host (no DAV path),
    // bootstrap the context path via /.well-known/caldav before walking
    // principals. Servers like NextCloud serve their web UI at the root
    // (which answers 405 to PROPFIND) and the DAV endpoint elsewhere
    // (e.g. /remote.php/dav/), advertised by a redirect from well-known.
    const QString path = m_serverUrl.path();
    if (path.isEmpty() || path == QStringLiteral("/")) {
        resolveContextPath();
    } else {
        discoverPrincipal();
    }
}

void CalDavCapabilityDiscovery::resolveContextPath()
{
    QUrl wellKnown = m_serverUrl;
    wellKnown.setPath(QStringLiteral("/.well-known/caldav"));

    QNetworkRequest request(wellKnown);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/xml; charset=utf-8"));
    request.setRawHeader("Depth", "0");
    // Read the redirect target ourselves rather than letting QNAM follow it:
    // the context path lives in the Location header, and a manual hop avoids
    // any ambiguity about whether the PROPFIND body survives an auto-redirect.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QByteArray body = buildPropfindRequest({
        QStringLiteral("current-user-principal")
    });

    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PROPFIND", body);
    connect(reply, &QNetworkReply::finished, this, &CalDavCapabilityDiscovery::onContextPathReplyFinished);
}

void CalDavCapabilityDiscovery::onContextPathReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        finishWithError(tr("Internal error: invalid reply"));
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
            emit progressMessage(tr("Discovered context path: %1").arg(m_baseUrl.toString()));
            discoverPrincipal();
            return;
        }
    }

    // No usable redirect (server doesn't implement well-known, or returned an
    // error/2xx without Location): fall back to probing the entered URL
    // directly, preserving pre-RFC-6764 behavior for servers that serve DAV
    // at the root.
    m_baseUrl = m_serverUrl;
    discoverPrincipal();
}

void CalDavCapabilityDiscovery::discoverPrincipal()
{
    // PROPFIND on the DAV base to find the current user principal
    QNetworkRequest request(m_baseUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));
    request.setRawHeader("Depth", "0");

    // Build PROPFIND request for current-user-principal
    QByteArray body = buildPropfindRequest({
        QStringLiteral("current-user-principal"),
        QStringLiteral("resourcetype"),
        QStringLiteral("displayname")
    });

    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PROPFIND", body);
    connect(reply, &QNetworkReply::finished, this, &CalDavCapabilityDiscovery::onPrincipalReplyFinished);
}

void CalDavCapabilityDiscovery::onPrincipalReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        finishWithError(tr("Internal error: invalid reply"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        finishWithError(tr("Failed to discover principal: %1").arg(reply->errorString()));
        return;
    }

    QByteArray response = reply->readAll();

    // Try to parse server product from response
    m_capabilities.serverProduct = parseServerProduct(response);

    // Extract current-user-principal href
    m_principalUrl = extractHref(response, QStringLiteral("current-user-principal"));

    if (m_principalUrl.isEmpty()) {
        // Some servers put principal directly at the DAV base
        m_principalUrl = m_baseUrl.toString();
    }

    emit progressMessage(tr("Found principal: %1").arg(m_principalUrl));
    discoverCalendarHome();
}

void CalDavCapabilityDiscovery::discoverCalendarHome()
{
    // PROPFIND on principal to find calendar-home-set
    QUrl principalUrl(m_principalUrl);
    if (principalUrl.isRelative()) {
        principalUrl = m_baseUrl.resolved(QUrl(m_principalUrl));
    }

    QNetworkRequest request(principalUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));
    request.setRawHeader("Depth", "0");

    QByteArray body = buildPropfindRequest({
        QStringLiteral("calendar-home-set")
    });

    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PROPFIND", body);
    connect(reply, &QNetworkReply::finished, this, &CalDavCapabilityDiscovery::onCalendarHomeReplyFinished);
}

void CalDavCapabilityDiscovery::onCalendarHomeReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        finishWithError(tr("Internal error: invalid reply"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        finishWithError(tr("Failed to discover calendar home: %1").arg(reply->errorString()));
        return;
    }

    QByteArray response = reply->readAll();

    // Extract calendar-home-set href
    m_calendarHomeUrl = extractHref(response, QStringLiteral("calendar-home-set"));

    if (m_calendarHomeUrl.isEmpty()) {
        finishWithError(tr("No calendar home found on server"));
        return;
    }

    emit progressMessage(tr("Found calendar home: %1").arg(m_calendarHomeUrl));
    discoverCalendars();
}

void CalDavCapabilityDiscovery::discoverCalendars()
{
    // PROPFIND on calendar-home-set to list calendars
    QUrl homeUrl(m_calendarHomeUrl);
    if (homeUrl.isRelative()) {
        homeUrl = m_baseUrl.resolved(QUrl(m_calendarHomeUrl));
    }

    QNetworkRequest request(homeUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));
    request.setRawHeader("Depth", "1");

    QByteArray body = buildPropfindRequest({
        QStringLiteral("resourcetype"),
        QStringLiteral("displayname"),
        QStringLiteral("supported-calendar-component-set"),
        QStringLiteral("calendar-color"),
        QStringLiteral("max-resource-size"),
        QStringLiteral("current-user-privilege-set")  // For detecting read-only calendars
    });

    QNetworkReply *reply = m_networkManager->sendCustomRequest(request, "PROPFIND", body);
    connect(reply, &QNetworkReply::finished, this, &CalDavCapabilityDiscovery::onCalendarsListReplyFinished);
}

void CalDavCapabilityDiscovery::onCalendarsListReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        finishWithError(tr("Internal error: invalid reply"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        finishWithError(tr("Failed to list calendars: %1").arg(reply->errorString()));
        return;
    }

    QByteArray response = reply->readAll();

    // Parse calendar list and their properties
    QDomDocument doc;
    QString errorMsg;
    int errorLine, errorCol;
    if (!doc.setContent(response, true, &errorMsg, &errorLine, &errorCol)) {
        qWarning() << "CalDavCapabilityDiscovery: XML parse error:" << errorMsg
                   << "at line" << errorLine << "col" << errorCol;
        finishWithError(tr("Failed to parse calendar list response: %1").arg(errorMsg));
        return;
    }

    // Namespace URIs
    const QString NS_DAV = QStringLiteral("DAV:");
    const QString NS_CALDAV = QStringLiteral("urn:ietf:params:xml:ns:caldav");
    const QString NS_APPLE = QStringLiteral("http://apple.com/ns/ical/");

    // Parse each response element using namespace-aware methods
    QDomNodeList responses = doc.elementsByTagNameNS(NS_DAV, QStringLiteral("response"));
    qDebug() << "CalDavCapabilityDiscovery: Found" << responses.count() << "response elements";

    for (int i = 0; i < responses.count(); ++i) {
        QDomElement respElement = responses.at(i).toElement();
        if (respElement.isNull())
            continue;

        // Get href using namespace
        QDomNodeList hrefs = respElement.elementsByTagNameNS(NS_DAV, QStringLiteral("href"));
        if (hrefs.isEmpty())
            continue;

        QString href = hrefs.at(0).toElement().text();

        // Check if this is a calendar (has calendar resourcetype in CalDAV namespace)
        bool isCalendar = false;
        QDomNodeList resourceTypes = respElement.elementsByTagNameNS(NS_DAV, QStringLiteral("resourcetype"));
        for (int j = 0; j < resourceTypes.count(); ++j) {
            QDomElement rtElement = resourceTypes.at(j).toElement();
            // Look for calendar element in CalDAV namespace
            QDomNodeList calElements = rtElement.elementsByTagNameNS(NS_CALDAV, QStringLiteral("calendar"));
            if (!calElements.isEmpty()) {
                isCalendar = true;
                break;
            }
        }

        if (!isCalendar)
            continue;

        // Parse calendar properties
        PerCalendarCapabilities caps;

        // Display name (in DAV namespace)
        QDomNodeList displayNames = respElement.elementsByTagNameNS(NS_DAV, QStringLiteral("displayname"));
        if (!displayNames.isEmpty()) {
            caps.serverDisplayName = displayNames.at(0).toElement().text();
        }

        // Supported component types (in CalDAV namespace)
        caps.supportsVEvent = false;
        caps.supportsVTodo = false;
        caps.supportsVJournal = false;

        QDomNodeList compSets = respElement.elementsByTagNameNS(NS_CALDAV, QStringLiteral("supported-calendar-component-set"));

        if (!compSets.isEmpty()) {
            QDomElement compSetElement = compSets.at(0).toElement();
            // Get comp elements in CalDAV namespace
            QDomNodeList comps = compSetElement.elementsByTagNameNS(NS_CALDAV, QStringLiteral("comp"));
            for (int j = 0; j < comps.count(); ++j) {
                QDomElement compElement = comps.at(j).toElement();
                if (compElement.isNull())
                    continue;

                QString name = compElement.attribute(QStringLiteral("name"));

                if (name.compare(QStringLiteral("VEVENT"), Qt::CaseInsensitive) == 0) {
                    caps.supportsVEvent = true;
                } else if (name.compare(QStringLiteral("VTODO"), Qt::CaseInsensitive) == 0) {
                    caps.supportsVTodo = true;
                } else if (name.compare(QStringLiteral("VJOURNAL"), Qt::CaseInsensitive) == 0) {
                    caps.supportsVJournal = true;
                }
            }
            qDebug() << "CalDavCapabilityDiscovery: Calendar" << caps.serverDisplayName
                     << "components - VEVENT:" << caps.supportsVEvent
                     << "VTODO:" << caps.supportsVTodo;
        } else {
            // No component set specified - per RFC4791 §5.2.3, assume full support
            caps.supportsVEvent = true;
            caps.supportsVTodo = true;
            qDebug() << "CalDavCapabilityDiscovery: Calendar" << caps.serverDisplayName
                     << "- no component set, assuming full support";
        }

        // Calendar color (in Apple namespace)
        QDomNodeList colors = respElement.elementsByTagNameNS(NS_APPLE, QStringLiteral("calendar-color"));
        if (!colors.isEmpty()) {
            QString colorStr = colors.at(0).toElement().text();
            if (!colorStr.isEmpty()) {
                caps.serverColor = QColor(colorStr);
            }
        }

        // Max resource size (in CalDAV namespace)
        QDomNodeList maxSizes = respElement.elementsByTagNameNS(NS_CALDAV, QStringLiteral("max-resource-size"));
        if (!maxSizes.isEmpty()) {
            bool ok;
            int size = maxSizes.at(0).toElement().text().toInt(&ok);
            if (ok) {
                caps.maxResourceSize = size;
            }
        }

        // Current user privilege set (in DAV namespace) - detect read-only calendars
        caps.writable = true;  // Default to writable
        QDomNodeList privSets = respElement.elementsByTagNameNS(NS_DAV, QStringLiteral("current-user-privilege-set"));
        if (!privSets.isEmpty()) {
            // If privilege set is present, check for write privileges
            // We need at least one write-related privilege to consider it writable
            bool hasWritePrivilege = false;
            QDomElement privSetElement = privSets.at(0).toElement();
            QDomNodeList privileges = privSetElement.elementsByTagNameNS(NS_DAV, QStringLiteral("privilege"));

            for (int j = 0; j < privileges.count(); ++j) {
                QDomElement privElement = privileges.at(j).toElement();
                if (privElement.isNull())
                    continue;

                // Check for various write-related privileges in DAV namespace
                // RFC3744 defines: write, write-content, write-properties, bind, unbind
                if (!privElement.elementsByTagNameNS(NS_DAV, QStringLiteral("write")).isEmpty() ||
                    !privElement.elementsByTagNameNS(NS_DAV, QStringLiteral("write-content")).isEmpty() ||
                    !privElement.elementsByTagNameNS(NS_DAV, QStringLiteral("bind")).isEmpty() ||
                    !privElement.elementsByTagNameNS(NS_DAV, QStringLiteral("unbind")).isEmpty()) {
                    hasWritePrivilege = true;
                    break;
                }
            }

            caps.writable = hasWritePrivilege;
            if (!hasWritePrivilege) {
                qDebug() << "CalDavCapabilityDiscovery: Calendar" << caps.serverDisplayName
                         << "is read-only (no write privileges)";
            }
        }
        // If no privilege set returned, we assume writable (server doesn't support ACL reporting)

        // Extract calendar ID from display name or href
        QString calendarId = caps.serverDisplayName;
        if (calendarId.isEmpty()) {
            // Use last path component as ID
            QUrl calUrl(href);
            QString path = calUrl.path();
            if (path.endsWith(QLatin1Char('/'))) {
                path.chop(1);
            }
            calendarId = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
        }

        // Store capabilities
        m_capabilities.perCalendarCapabilities[calendarId] = caps;
        m_calendarUrls.insert(calendarId, href);

        // Track supported component types at server level
        if (caps.supportsVEvent && !m_capabilities.supportedComponentTypes.contains(QStringLiteral("VEVENT"))) {
            m_capabilities.supportedComponentTypes.append(QStringLiteral("VEVENT"));
        }
        if (caps.supportsVTodo && !m_capabilities.supportedComponentTypes.contains(QStringLiteral("VTODO"))) {
            m_capabilities.supportedComponentTypes.append(QStringLiteral("VTODO"));
        }
        if (caps.supportsVJournal && !m_capabilities.supportedComponentTypes.contains(QStringLiteral("VJOURNAL"))) {
            m_capabilities.supportedComponentTypes.append(QStringLiteral("VJOURNAL"));
        }

        emit calendarDiscovered(calendarId, caps);
        emit progressMessage(tr("Discovered calendar: %1").arg(calendarId));
    }

    finishWithSuccess();
}

void CalDavCapabilityDiscovery::onCalendarPropsReplyFinished()
{
    // Currently unused - all props fetched in calendar list
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply) {
        reply->deleteLater();
    }
}

void CalDavCapabilityDiscovery::fetchCalendarProperties(const QString &)
{
    // Currently unused - all props fetched in calendar list
}

QByteArray CalDavCapabilityDiscovery::buildPropfindRequest(const QStringList &properties, int) const
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:propfind xmlns:d=\"DAV:\" "
        "xmlns:cal=\"urn:ietf:params:xml:ns:caldav\" "
        "xmlns:cs=\"http://calendarserver.org/ns/\" "
        "xmlns:x1=\"http://apple.com/ns/ical/\">\n"
        "  <d:prop>\n");

    for (const QString &prop : properties) {
        if (prop == QStringLiteral("current-user-principal")) {
            xml += QStringLiteral("    <d:current-user-principal/>\n");
        } else if (prop == QStringLiteral("resourcetype")) {
            xml += QStringLiteral("    <d:resourcetype/>\n");
        } else if (prop == QStringLiteral("displayname")) {
            xml += QStringLiteral("    <d:displayname/>\n");
        } else if (prop == QStringLiteral("calendar-home-set")) {
            xml += QStringLiteral("    <cal:calendar-home-set/>\n");
        } else if (prop == QStringLiteral("supported-calendar-component-set")) {
            xml += QStringLiteral("    <cal:supported-calendar-component-set/>\n");
        } else if (prop == QStringLiteral("calendar-color")) {
            xml += QStringLiteral("    <x1:calendar-color/>\n");
        } else if (prop == QStringLiteral("max-resource-size")) {
            xml += QStringLiteral("    <cal:max-resource-size/>\n");
        } else if (prop == QStringLiteral("current-user-privilege-set")) {
            xml += QStringLiteral("    <d:current-user-privilege-set/>\n");
        }
    }

    xml += QStringLiteral(
        "  </d:prop>\n"
        "</d:propfind>\n");

    return xml.toUtf8();
}

QString CalDavCapabilityDiscovery::extractHref(const QByteArray &response, const QString &element) const
{
    QDomDocument doc;
    if (!doc.setContent(response)) {
        return QString();
    }

    // Find the element (try with various namespace prefixes)
    QStringList prefixes = { QString(), QStringLiteral("d:"), QStringLiteral("D:"),
                              QStringLiteral("cal:"), QStringLiteral("C:") };

    for (const QString &prefix : prefixes) {
        QDomNodeList nodes = doc.elementsByTagName(prefix + element);
        if (!nodes.isEmpty()) {
            QDomElement elem = nodes.at(0).toElement();
            // Look for href child
            QDomElement hrefElem = elem.firstChildElement(QStringLiteral("href"));
            if (hrefElem.isNull()) {
                hrefElem = elem.firstChildElement(QStringLiteral("d:href"));
            }
            if (hrefElem.isNull()) {
                hrefElem = elem.firstChildElement(QStringLiteral("D:href"));
            }
            if (!hrefElem.isNull()) {
                return hrefElem.text();
            }
            // Maybe the element itself contains the href as text
            QString text = elem.text().trimmed();
            if (!text.isEmpty() && text.startsWith(QLatin1Char('/'))) {
                return text;
            }
        }
    }

    return QString();
}

QStringList CalDavCapabilityDiscovery::extractCalendarUrls(const QByteArray &) const
{
    // Currently unused - parsing done in onCalendarsListReplyFinished
    return QStringList();
}

PerCalendarCapabilities CalDavCapabilityDiscovery::parseCalendarProperties(
    const QByteArray &, const QString &) const
{
    // Currently unused - parsing done in onCalendarsListReplyFinished
    return PerCalendarCapabilities();
}

QString CalDavCapabilityDiscovery::parseServerProduct(const QByteArray &response) const
{
    // Try to detect server from response headers or content
    QString responseStr = QString::fromUtf8(response);

    if (responseStr.contains(QStringLiteral("Nextcloud"), Qt::CaseInsensitive)) {
        return QStringLiteral("Nextcloud");
    }
    if (responseStr.contains(QStringLiteral("Radicale"), Qt::CaseInsensitive)) {
        return QStringLiteral("Radicale");
    }
    if (responseStr.contains(QStringLiteral("DAViCal"), Qt::CaseInsensitive)) {
        return QStringLiteral("DAViCal");
    }
    if (responseStr.contains(QStringLiteral("Baïkal"), Qt::CaseInsensitive) ||
        responseStr.contains(QStringLiteral("Baikal"), Qt::CaseInsensitive)) {
        return QStringLiteral("Baïkal");
    }
    if (responseStr.contains(QStringLiteral("Apple"), Qt::CaseInsensitive)) {
        return QStringLiteral("Apple Calendar Server");
    }
    if (responseStr.contains(QStringLiteral("Google"), Qt::CaseInsensitive)) {
        return QStringLiteral("Google Calendar");
    }

    return QString();
}

void CalDavCapabilityDiscovery::finishWithError(const QString &error)
{
    m_running = false;
    m_errorMessage = error;
    qWarning() << "CalDavCapabilityDiscovery error:" << error;
    emit finished(false);
}

void CalDavCapabilityDiscovery::finishWithSuccess()
{
    m_running = false;

    // Check if server supports calendar creation (based on what we found)
    m_capabilities.supportsCalendarCreation = true;  // Assume yes unless proven otherwise

    qDebug() << "CalDavCapabilityDiscovery: discovered"
             << m_capabilities.perCalendarCapabilities.count() << "calendars"
             << "from" << m_capabilities.serverProduct;

    emit finished(true);
}


} // namespace Kalburator::Sync
