#include "labpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

QString msgraphDir()
{
    const QString envDir = qEnvironmentVariable("KALBURATOR_MSGRAPH_DIR");
    if (!envDir.isEmpty())
        return envDir;

    QDir dir = QDir::current();
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath("msgraph");
        if (QFileInfo::exists(candidate + "/GraphCLIinfo.md"))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return QDir::current().filePath("msgraph");
}

QString readClientId(const QString &dir)
{
    const QString envId = qEnvironmentVariable("KALBURATOR_GRAPH_CLIENT_ID");
    if (!envId.isEmpty())
        return envId;

    QFile file(dir + "/GraphCLIinfo.md");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "Cannot read " << dir
                            << "/GraphCLIinfo.md (set KALBURATOR_GRAPH_CLIENT_ID or KALBURATOR_MSGRAPH_DIR)\n";
        return {};
    }
    static const QRegularExpression re("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
    const QString text = QString::fromUtf8(file.readAll());
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        QTextStream(stderr) << "No Application ID found in GraphCLIinfo.md\n";
        return {};
    }
    return match.captured();
}

void printGraphError(const QString &label, int status, const QByteArray &body)
{
    QTextStream(stderr) << label << " failed";
    if (status > 0)
        QTextStream(stderr) << " (HTTP " << status << ')';
    QTextStream(stderr) << '\n';
    if (!body.isEmpty())
        QTextStream(stderr) << QString::fromUtf8(body) << '\n';
}
