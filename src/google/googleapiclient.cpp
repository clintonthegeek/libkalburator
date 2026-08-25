#include "googleapiclient.h"
#include "backoff.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::Google {

namespace Net = Kalburator::Net;

namespace {
constexpr int kMaxPages = 100;
} // namespace

GoogleApiClient::GoogleApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

GoogleApiClient::~GoogleApiClient() = default;

void GoogleApiClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl;
}

void GoogleApiClient::setAccessToken(const QString &token)
{
    m_token = token;
}

void GoogleApiClient::setTransientRetryAttempts(int attempts)
{
    m_transientRetryAttempts = qMax(0, attempts);
}

void GoogleApiClient::getWithRetry(const QUrl &url, int attempt,
                                   std::function<void(const RawReply &)> done)
{
    get(url, [this, url, attempt, done = std::move(done)](const RawReply &r) {
        if (attempt < m_transientRetryAttempts
            && Net::isTransientFailure(r.status, r.networkError)) {
            const int delay = Net::retryDelayMsecs(attempt);
            // Heap-held continuation state (O62).
            QTimer::singleShot(delay, this, [this, url, attempt, done] {
                getWithRetry(url, attempt + 1, done);
            });
            return;
        }
        done(r);
    });
}

void GoogleApiClient::get(const QUrl &url,
                          std::function<void(const RawReply &)> done)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_token.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             "Bearer " + m_token.toUtf8());

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, done = std::move(done)] {
                RawReply r;
                r.status = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError && r.status == 0)
                    r.networkError = true;
                r.body = reply->readAll();
                reply->deleteLater();
                done(r);
            });
}

GoogleError GoogleApiClient::parseError(const RawReply &r) const
{
    GoogleError e;
    e.httpStatus = r.status;
    e.networkError = r.networkError;
    const QJsonDocument doc = QJsonDocument::fromJson(r.body);
    if (doc.isObject()) {
        // Google shape: {error:{code,message,errors:[{reason,...}]}} — the
        // first errors[].reason is the discriminator; fall back to top code.
        const QJsonObject err = doc.object().value(QStringLiteral("error")).toObject();
        e.message = err.value(QStringLiteral("message")).toString();
        e.reason = err.value(QStringLiteral("errors")).toArray()
                       .at(0).toObject()
                       .value(QStringLiteral("reason")).toString();
        if (e.reason.isEmpty())
            e.reason = err.value(QStringLiteral("code")).toString();
    }
    return e;
}

void GoogleApiClient::fetchCollection(const QString &path, CollectionCallback done)
{
    getPage(std::make_shared<QJsonArray>(), std::make_shared<int>(0),
            QUrl(m_baseUrl + path), std::move(done));
}

void GoogleApiClient::getPage(std::shared_ptr<QJsonArray> items,
                              std::shared_ptr<int> pages,
                              const QUrl &url, CollectionCallback done)
{
    getWithRetry(url, 1, [this, url, items, pages, done = std::move(done)](const RawReply &r) {
        if (!(r.status >= 200 && r.status < 300)) {
            done(std::nullopt, QString(), parseError(r));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(r.body);
        if (!doc.isObject()) {
            GoogleError e;
            e.httpStatus = r.status;
            e.message = QStringLiteral("non-object collection page");
            done(std::nullopt, QString(), e);
            return;
        }
        const QJsonObject obj = doc.object();
        for (const auto &v : obj.value(QStringLiteral("items")).toArray())
            items->append(v);

        const QString pageToken =
            obj.value(QStringLiteral("nextPageToken")).toString();
        if (!pageToken.isEmpty()) {
            if (++(*pages) > kMaxPages) {
                GoogleError e;
                e.httpStatus = r.status;
                e.message = QStringLiteral("nextPageToken loop guard tripped");
                done(std::nullopt, QString(), e);
                return;
            }
            QUrl next = url;
            QUrlQuery query(next.query());
            query.removeQueryItem(QStringLiteral("pageToken"));
            query.addQueryItem(QStringLiteral("pageToken"), pageToken);
            next.setQuery(query);
            getPage(items, pages, next, std::move(done));
            return;
            return;
        }
        GoogleError ok;
        ok.httpStatus = r.status;
        done(*items,
             obj.value(QStringLiteral("nextSyncToken")).toString(),
             ok);
    });
}

void GoogleApiClient::rawRequest(const QByteArray &method, const QString &path,
                                 const QByteArray &body, RawCallback done)
{
    const bool absolute = path.startsWith(QLatin1String("http://"))
        || path.startsWith(QLatin1String("https://"));
    QNetworkRequest request(absolute ? QUrl(path) : QUrl(m_baseUrl + path));
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_token.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             "Bearer " + m_token.toUtf8());
    if (!body.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json"));

    QNetworkReply *reply = m_nam->sendCustomRequest(request, method, body);
    connect(reply, &QNetworkReply::finished, this,
            [reply, done = std::move(done)] {
                const int status = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool networkError =
                    reply->error() != QNetworkReply::NoError && status == 0;
                const QByteArray bodyBytes = reply->readAll();
                reply->deleteLater();
                done(status, bodyBytes, networkError);
            });
}

GoogleApiClient::CollectionResult
GoogleApiClient::fetchCollectionSync(const QString &path)
{
    QEventLoop loop;
    CollectionResult result;
    fetchCollection(path, [&](std::optional<QJsonArray> items,
                              const QString &nextSyncToken,
                              const GoogleError &e) {
        result.items = std::move(items);
        result.nextSyncToken = nextSyncToken;
        result.error = e;
        loop.quit();
    });
    loop.exec();
    return result;
}

} // namespace Kalburator::Google
