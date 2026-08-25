#pragma once

// B2C P0 — shared blocking HTTP primitive (consolidates the three
// verbatim-duplicate copies that lived in tools/graphcli/graphclient,
// tools/googlecli/googleclient, and tests/graph/mockgraphserver).
//
// BLOCKING by design: spins a nested QEventLoop with a hard watchdog.
// For CLIs, test drivers, and tooling only — library backends must use
// the async clients (GraphApiClient et al.) on the host's event loop.

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

namespace Kalburator::Net {

struct HttpResponse {
    int status = 0;
    QByteArray body;
    QString error;

    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

/// Synchronous request. Network failure surfaces as status 0 + error set.
HttpResponse httpRequest(
    const QUrl &url,
    const QByteArray &method,
    const QList<QPair<QByteArray, QByteArray>> &headers = {},
    const QByteArray &body = {});

QString urlEncodePathSegment(const QString &segment);

} // namespace Kalburator::Net
