#pragma once

// B2C P0 — lab-only helpers that stay OUT of the library: machine-local
// path discovery, credential scraping from the gitignored info file, and
// error printing. Library transport lives in src/google/.

#include <QByteArray>
#include <QString>

#include <googleauth.h>

using Kalburator::Google::ClientCredentials;

QString googledir();
ClientCredentials readClientCredentials(const QString &dir);
void printGoogleError(const QString &label, int status, const QByteArray &body);
