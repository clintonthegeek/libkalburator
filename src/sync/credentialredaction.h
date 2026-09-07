#pragma once

#include <QUrl>

namespace Kalburator::Sync {
QString redactCredentials(const QUrl &url);
QString redactCredentials(const QString &text);
} // namespace Kalburator::Sync
