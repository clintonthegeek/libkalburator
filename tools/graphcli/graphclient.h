#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>

struct HttpResponse {
    int status = 0;
    QByteArray body;
    QString error;

    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

HttpResponse httpRequest(const QUrl &url,
                         const QByteArray &method,
                         const QList<QPair<QByteArray, QByteArray>> &headers = {},
                         const QByteArray &body = {});

QString urlEncodePathSegment(const QString &segment);
