#pragma once

// B2C P1 — transport-layer HTTP client for Google Calendar API v3.
// Envelope ONLY (proposal invariant 5): bearer injection, pageToken
// pagination, sync-token surfacing, typed errors. Resource shapes belong
// to the google-event edge stages; auth belongs to googleauth.h.
//
// Google wire facts honored here (vendor-rest-api-wire-notes §1/§2):
//   - collections paginate via `items[]` + `nextPageToken` (request param
//     `pageToken=`), NOT Graph's value[]/@odata.nextLink
//   - listings surface `nextSyncToken` on the final page; presenting an
//     expired token yields HTTP 410 Gone (full-resync signal)
//   - error bodies are {error:{code,message,errors:[{reason,...}]}} — the
//     `reason` field is the reliable discriminator (O57(j) analogue)

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace Kalburator::Google {

/// Structured Google API error. `reason` (e.g. "gone", "notFound",
/// "rateLimitExceeded") discriminates; messages may be unhelpful.
struct GoogleError {
    int httpStatus = 0;
    QString reason;
    QString message;
    bool networkError = false;

    bool ok() const { return !networkError && httpStatus >= 200 && httpStatus < 300; }
    /// HTTP 410 Gone — the sync token expired; caller must full-resync.
    bool isGone() const { return httpStatus == 410; }
};

class GoogleApiClient : public QObject {
    Q_OBJECT
public:
    explicit GoogleApiClient(QObject *parent = nullptr);
    ~GoogleApiClient() override;

    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);

    /// Transient-failure retries for idempotent GETs (network errors +
    /// 502/503/504), exponential backoff (src/net/backoff.h). Default 2.
    /// Writes are never auto-retried.
    void setTransientRetryAttempts(int attempts);

    using CollectionCallback = std::function<
        void(std::optional<QJsonArray> items,
             const QString &nextSyncToken,
             const GoogleError &)>;
    /// GET `<path>` (path+query relative to baseUrl) and aggregate every
    /// page by following `nextPageToken`. `nextSyncToken` carries the
    /// final page's incremental-listing token (empty when the response has
    /// none — e.g. queries incompatible with sync tokens).
    void fetchCollection(const QString &path, CollectionCallback done);

    using RawCallback =
        std::function<void(int status, const QByteArray &body, bool networkError)>;
    /// Arbitrary-method request against `<path>` (relative) — the write
    /// path primitive (POST/PATCH/DELETE). Absolute URLs also work.
    void rawRequest(const QByteArray &method, const QString &path,
                    const QByteArray &body, RawCallback done);

    // Synchronous conveniences (tests/tools; spin a local event loop).
    struct CollectionResult {
        std::optional<QJsonArray> items;
        QString nextSyncToken;
        GoogleError error;
    };
    CollectionResult fetchCollectionSync(const QString &path);

private:
    struct RawReply {
        int status = 0;
        QByteArray body;
        bool networkError = false;
    };
    void get(const QUrl &url, std::function<void(const RawReply &)> done);
    void getWithRetry(const QUrl &url, int attempt,
                      std::function<void(const RawReply &)> done);
    void getPage(std::shared_ptr<QJsonArray> items,
                 std::shared_ptr<int> pages,
                 const QUrl &url, CollectionCallback done);
    GoogleError parseError(const RawReply &r) const;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_token;
    int m_transientRetryAttempts = 2;
};

} // namespace Kalburator::Google
