#pragma once

// B2C P0 — lab-only helpers that stay OUT of the library: machine-local
// path discovery, client-id scraping from the gitignored info file, and
// error printing. The library transport lives in src/graph/.

#include <QByteArray>
#include <QString>

QString msgraphDir();
QString readClientId(const QString &dir);
void printGraphError(const QString &label, int status, const QByteArray &body);
