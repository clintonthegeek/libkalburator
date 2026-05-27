#include "akonadirevisionstore.h"
#include <QSettings>
namespace Kalburator::Sync {
AkonadiRevisionStore::AkonadiRevisionStore(const QString &filePath)
    : m_filePath(filePath) {}
QString AkonadiRevisionStore::token(const QString &collectionId) const {
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("revisions"));
    return s.value(collectionId).toString();
}
void AkonadiRevisionStore::setToken(const QString &collectionId, const QString &token) {
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("revisions"));
    s.setValue(collectionId, token);
}
}
