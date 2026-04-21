#include "crashjournal.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>

namespace Kalburator::Sync {

Q_LOGGING_CATEGORY(lcCrashJournal, "planstan.crashjournal")

CrashJournal::CrashJournal(const QString &directory, const QString &suffix)
    : m_directory(directory)
    , m_suffix(suffix)
{
}

QString CrashJournal::journalPath(const QString &entityId) const
{
    return m_directory + QLatin1Char('/') + entityId + m_suffix;
}

void CrashJournal::append(const QString &entityId, const QJsonObject &entry)
{
    QFile file(journalPath(entityId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        qCWarning(lcCrashJournal) << "Failed to open journal for"
                                  << entityId << ":" << file.errorString();
        return;
    }

    QJsonDocument doc(entry);
    file.write(doc.toJson(QJsonDocument::Compact));
    file.write("\n");
    file.flush();
}

void CrashJournal::truncate(const QString &entityId)
{
    const QString path = journalPath(entityId);
    if (QFile::exists(path))
        QFile::remove(path);
}

bool CrashJournal::hasJournal(const QString &entityId) const
{
    const QString path = journalPath(entityId);
    QFileInfo info(path);
    return info.exists() && info.size() > 0;
}

QStringList CrashJournal::entitiesWithJournals() const
{
    QStringList result;
    const QString pattern = QLatin1Char('*') + m_suffix;
    QDirIterator it(m_directory, {pattern}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        QString name = it.fileName();
        if (name.endsWith(m_suffix)) {
            if (QFileInfo(it.filePath()).size() > 0) {
                result.append(name.chopped(m_suffix.size()));
            }
        }
    }
    return result;
}

int CrashJournal::replay(const QString &entityId,
                         const std::function<void(const QJsonObject &)> &handler) const
{
    const QString path = journalPath(entityId);
    QFile file(path);
    if (!file.exists() || file.size() == 0)
        return 0;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcCrashJournal) << "Failed to open journal for replay:" << path;
        return 0;
    }

    int count = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcCrashJournal) << "Skipping malformed journal entry:"
                                      << parseError.errorString();
            continue;
        }

        handler(doc.object());
        ++count;
    }

    return count;
}


} // namespace Kalburator::Sync
