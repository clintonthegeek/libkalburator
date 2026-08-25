#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace Kalburator::Graph {

/// Structured Graph error (FINDINGS O57(j)): the wire `error.code` field is
/// the reliable discriminator — messages may be truncated/unhelpful.
struct GraphError {
    int httpStatus = 0;
    QString code;         // e.g. "ResyncRequired", "ErrorItemNotFound"
    QString message;
    bool networkError = false;

    bool ok() const { return !networkError && httpStatus >= 200 && httpStatus < 300; }
    bool isResyncRequired() const { return code == QLatin1String("ResyncRequired"); }
};

/// Transport-layer HTTP client for Microsoft Graph (EEE Phase 7.C
/// foundation; Stage-D tested). Speaks ONLY the envelope mechanics:
///   - collection walks following absolute @odata.nextLink URLs ($top/$skip)
///   - /delta steps: items page vs @odata.deltaLink fixpoint, and the
///     410 ResyncRequired signal surfaced as a typed flag
///   - Bearer auth header injection
///   - error.code extraction into GraphError
///
/// Resource shapes (event JSON etc.) are NOT this class's concern — they
/// belong to the ms-event edge stages.
class GraphApiClient : public QObject {
    Q_OBJECT
public:
    /// One step of a /delta walk.
    struct DeltaPage {
        QJsonArray items;            // changed/initial items on this page
        QString deltaToken;          // token to present on the NEXT walk
                                     // (extracted from @odata.deltaLink)
        bool complete = false;       // true ⇒ deltaLink present (fixpoint);
                                     // false ⇒ nextLink present (more changes)
        bool resyncRequired = false; // 410 ResyncRequired ⇒ full re-listing
    };

    explicit GraphApiClient(QObject *parent = nullptr);
    ~GraphApiClient() override;

    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);

    /// B2C P0 — transient-failure retries for idempotent GETs (network
    /// errors + 502/503/504), exponential backoff (src/net/backoff.h).
    /// Default 2. Writes are never auto-retried.
    void setTransientRetryAttempts(int attempts);

    using CollectionCallback =
        std::function<void(std::optional<QJsonArray> items, const GraphError &)>;
    /// GET `<path>` (path+query relative to baseUrl) and aggregate every
    /// page by following @odata.nextLink until exhausted.
    void fetchCollection(const QString &path, CollectionCallback done);

    using DeltaCallback = std::function<void(const DeltaPage &, const GraphError &)>;
    /// Perform ONE delta step. Pass `collectionPath` (e.g. "/me/calendar/events")
    /// for the initial call, or the PREVIOUS response's deltaToken to replay.
    /// When `complete` is false the server still had queued changes — call
    /// again with the fresh token until complete (the fixpoint discipline
    /// pinned by tst_mock_graph_server).
    void deltaStep(const QString &collectionPath, const QString &deltaToken,
                   DeltaCallback done);

    using RawCallback =
        std::function<void(int status, const QByteArray &body, bool networkError)>;
    /// Arbitrary-method request against `<path>` (relative) — the backend
    /// write path's primitive (POST/PATCH/DELETE). Absolute URLs also work.
    void rawRequest(const QByteArray &method, const QString &path,
                    const QByteArray &body, RawCallback done);

    // Synchronous conveniences (tests/tools; spin a local event loop).
    std::pair<std::optional<QJsonArray>, GraphError>
    fetchCollectionSync(const QString &path);
    std::pair<DeltaPage, GraphError>
    deltaStepSync(const QString &collectionPath, const QString &deltaToken);

private:
    struct RawReply {
        int status = 0;
        QByteArray body;
        bool networkError = false;
    };
    void get(const QUrl &url,
             std::function<void(const RawReply &)> done);
    void getWithRetry(const QUrl &url, int attempt,
                      std::function<void(const RawReply &)> done);
    /// One page of a collection walk; recurses through nextLink via
    /// heap-owned state (async continuations outlive the caller's frame).
    void getPage(std::shared_ptr<QJsonArray> items,
                 std::shared_ptr<int> pages,
                 const QUrl &url,
                 CollectionCallback done);
    GraphError parseError(const RawReply &r) const;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_token;
    int m_transientRetryAttempts = 2;
};

} // namespace Kalburator::Graph
