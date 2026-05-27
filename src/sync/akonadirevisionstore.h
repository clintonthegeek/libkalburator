#ifndef KALBURATOR_SYNC_AKONADIREVISIONSTORE_H
#define KALBURATOR_SYNC_AKONADIREVISIONSTORE_H
#include <QString>
namespace Kalburator::Sync {
/// Persists per-collection revision tokens across process restarts.
/// CTagStore analogue for Akonadi's synthesized change-detection token.
class AkonadiRevisionStore {
public:
    explicit AkonadiRevisionStore(const QString &filePath);
    QString token(const QString &collectionId) const;
    void    setToken(const QString &collectionId, const QString &token);
private:
    QString m_filePath;
};
}
#endif
