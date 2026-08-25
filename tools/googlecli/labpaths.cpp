#include "labpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

QString googledir()
{
    const QString envDir = qEnvironmentVariable("KALBURATOR_GOOGLE_DIR");
    if (!envDir.isEmpty())
        return envDir;

    QDir dir = QDir::current();
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath("google");
        if (QFileInfo::exists(candidate + "/GoogleAuthinfo.md"))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return QDir::current().filePath("google");
}

ClientCredentials readClientCredentials(const QString &dir)
{
    ClientCredentials creds;

    creds.clientId = qEnvironmentVariable("KALBURATOR_GOOGLE_CLIENT_ID");
    creds.clientSecret = qEnvironmentVariable("KALBURATOR_GOOGLE_CLIENT_SECRET");
    if (creds.valid())
        return creds;

    QFile file(dir + "/GoogleAuthinfo.md");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "Cannot read " << dir
                            << "/GoogleAuthinfo.md (set KALBURATOR_GOOGLE_CLIENT_ID"
                            << " and KALBURATOR_GOOGLE_CLIENT_SECRET, or KALBURATOR_GOOGLE_DIR)\n";
        return {};
    }
    static const QRegularExpression idRe("Client ID:\\s*([^\\s]+)");
    static const QRegularExpression secretRe("Client Secret:\\s*([^\\s]+)");
    const QString text = QString::fromUtf8(file.readAll());
    const auto idMatch = idRe.match(text);
    const auto secretMatch = secretRe.match(text);
    if (idMatch.hasMatch())
        creds.clientId = idMatch.captured(1);
    if (secretMatch.hasMatch())
        creds.clientSecret = secretMatch.captured(1);
    if (!creds.valid())
        QTextStream(stderr) << "GoogleAuthinfo.md lacks 'Client ID:' / 'Client Secret:' lines\n";
    return creds;
}

void printGoogleError(const QString &label, int status, const QByteArray &body)
{
    QTextStream(stderr) << label << " failed: HTTP " << status << "\n"
                        << QString::fromUtf8(body) << '\n';
}
