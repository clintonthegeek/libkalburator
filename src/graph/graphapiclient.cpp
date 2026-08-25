#include "graphapiclient.h"
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

namespace Kalburator::Graph {

namespace Net = Kalburator::Net;

namespace {
// Safety valve against server nextLink loops (Graph never paginates beyond
// this for a single collection walk in practice).
constexpr int kMaxPages = 100;
} // namespace

GraphApiClient::GraphApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

GraphApiClient::~GraphApiClient() = default;

void GraphApiClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl;
}

void GraphApiClient::setAccessToken(const QString &token)
{
    m_token = token;
}

void GraphApiClient::setTransientRetryAttempts(int attempts)
{
    m_transientRetryAttempts = qMax(0, attempts);
}

void GraphApiClient::getWithRetry(const QUrl &url, int attempt,
                                  std::function<void(const RawReply &)> done)
{
    get(url, [this, url, attempt, done = std::move(done)](const RawReply &r) {
        if (attempt < m_transientRetryAttempts
            && Net::isTransientFailure(r.status, r.networkError)) {
            const int delay = Net::retryDelayMsecs(attempt);
            // Heap-held continuation state (O62): url/attempt/done live in
            // the single-shot's closure, not on this frame.
            QTimer::singleShot(delay, this, [this, url, attempt, done] {
                getWithRetry(url, attempt + 1, done);
            });
            return;
        }
        done(r);
    });
}

void GraphApiClient::get(const QUrl &url,
                         std::function<void(const RawReply &)> done)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(60'000);
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

GraphError GraphApiClient::parseError(const RawReply &r) const
{
    GraphError e;
    e.httpStatus = r.status;
    e.networkError = r.networkError;
    const QJsonDocument doc = QJsonDocument::fromJson(r.body);
    if (doc.isObject()) {
        const QJsonObject err = doc.object().value(QStringLiteral("error")).toObject();
        e.code = err.value(QStringLiteral("code")).toString();
        e.message = err.value(QStringLiteral("message")).toString();
    }
    return e;
}

void GraphApiClient::fetchCollection(const QString &path, CollectionCallback done)
{
    getPage(std::make_shared<QJsonArray>(), std::make_shared<int>(0),
            QUrl(m_baseUrl + path), std::move(done));
}

void GraphApiClient::getPage(std::shared_ptr<QJsonArray> items,
                             std::shared_ptr<int> pages,
                             const QUrl &url, CollectionCallback done)
{
    getWithRetry(url, 1, [this, items, pages, done = std::move(done)](const RawReply &r) {
        if (!(r.status >= 200 && r.status < 300)) {
            done(std::nullopt, parseError(r));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(r.body);
        if (!doc.isObject()) {
            GraphError e;
            e.httpStatus = r.status;
            e.message = QStringLiteral("non-object collection page");
            done(std::nullopt, e);
            return;
        }
        const QJsonObject obj = doc.object();
        for (const auto &v : obj.value(QStringLiteral("value")).toArray())
            items->append(v);

        const QString nextLink =
            obj.value(QStringLiteral("@odata.nextLink")).toString();
        if (!nextLink.isEmpty()) {
            if (++(*pages) > kMaxPages) {
                GraphError e;
                e.httpStatus = r.status;
                e.message = QStringLiteral("nextLink loop guard tripped");
                done(std::nullopt, e);
                return;
            }
            getPage(items, pages, QUrl(nextLink), std::move(done));
            return;
        }
        GraphError ok;
        ok.httpStatus = r.status;
        done(*items, ok);
    });
}

void GraphApiClient::deltaStep(const QString &collectionPath,
                               const QString &deltaToken, DeltaCallback done)
{
    // Initial call: "<collection>/delta". Replay: present the token via the
    // $deltatoken query parameter on the SAME /delta route (real Graph's
    // deltaLink URLs carry exactly this parameter; the mock accepts both).
    QString path = collectionPath;
    if (!path.endsWith(QLatin1String("/delta")))
        path += QLatin1String("/delta");
    if (!deltaToken.isEmpty())
        path += QStringLiteral("?$deltatoken=") + deltaToken;

    // B2C P0/P1 consistency: delta steps are idempotent GETs — they get
    // the same transient-failure retries as collection page fetches.
    getWithRetry(QUrl(m_baseUrl + path), 1,
                 [this, done = std::move(done)](
                                     const RawReply &r) mutable {
        DeltaPage page;
        if (r.status == 410) {
            const GraphError e = parseError(r);
            if (e.isResyncRequired()) {
                page.resyncRequired = true;
                done(page, e);
                return;
            }
        }
        if (!(r.status >= 200 && r.status < 300)) {
            done(page, parseError(r));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(r.body);
        if (!doc.isObject()) {
            GraphError e;
            e.httpStatus = r.status;
            e.message = QStringLiteral("non-object delta page");
            done(page, e);
            return;
        }
        const QJsonObject obj = doc.object();
        page.items = obj.value(QStringLiteral("value")).toArray();

        const QString deltaLink =
            obj.value(QStringLiteral("@odata.deltaLink")).toString();
        const QString nextLink =
            obj.value(QStringLiteral("@odata.nextLink")).toString();
        if (!deltaLink.isEmpty()) {
            page.complete = true;
            const QUrlQuery query(QUrl(deltaLink).query());
            page.deltaToken =
                query.queryItemValue(QStringLiteral("$deltatoken"));
        } else if (!nextLink.isEmpty()) {
            // More queued changes follow immediately — surface the token so
            // the caller can keep stepping to the fixpoint.
            const QUrlQuery query(QUrl(nextLink).query());
            page.deltaToken =
                query.queryItemValue(QStringLiteral("$deltatoken"));
        }
        GraphError ok;
        ok.httpStatus = r.status;
        done(page, ok);
    });
}

void GraphApiClient::rawRequest(const QByteArray &method, const QString &path,
                                const QByteArray &body, RawCallback done)
{
    const bool absolute = path.startsWith(QLatin1String("http://"))
        || path.startsWith(QLatin1String("https://"));
    QNetworkRequest request(absolute ? QUrl(path) : QUrl(m_baseUrl + path));
    request.setTransferTimeout(60'000);
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

std::pair<std::optional<QJsonArray>, GraphError>
GraphApiClient::fetchCollectionSync(const QString &path)
{
    QEventLoop loop;
    std::pair<std::optional<QJsonArray>, GraphError> result;
    fetchCollection(path, [&](std::optional<QJsonArray> items,
                             const GraphError &e) {
        result = { std::move(items), e };
        loop.quit();
    });
    loop.exec();
    return result;
}

std::pair<GraphApiClient::DeltaPage, GraphError>
GraphApiClient::deltaStepSync(const QString &collectionPath,
                              const QString &deltaToken)
{
    QEventLoop loop;
    std::pair<DeltaPage, GraphError> result;
    deltaStep(collectionPath, deltaToken,
              [&](const DeltaPage &page, const GraphError &e) {
                  result = { page, e };
                  loop.quit();
              });
    loop.exec();
    return result;
}

} // namespace Kalburator::Graph
