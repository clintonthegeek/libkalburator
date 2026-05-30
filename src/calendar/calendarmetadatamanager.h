#ifndef CALENDARMETADATAMANAGER_H
#define CALENDARMETADATAMANAGER_H

#include <QString>
#include <QColor>
#include <QDir>

namespace Kalburator::Sync {

/**
 * @brief Manages VDirSyncer-compatible calendar metadata files.
 *
 * This class reads and writes calendar metadata in the Vdir storage format,
 * which is compatible with VDirSyncer and CalDAV storage specifications.
 *
 * Metadata files stored in the calendar folder (no file extensions):
 * - color: ASCII-encoded hex-RGB "#RRGGBB" (e.g., "#FF0000" for red)
 * - displayname: UTF-8 encoded label
 * - description: UTF-8 encoded description
 * - order: Integer for relative calendar ordering
 *
 * All writes are atomic (write to temp file, then rename) per vdir spec.
 */
class CalendarMetadataManager
{
public:
    explicit CalendarMetadataManager(const QString &calendarPath);

    // Color metadata
    bool hasColor() const;
    QColor color() const;
    bool setColor(const QColor &color);
    bool removeColor();

    // Display name metadata
    bool hasDisplayName() const;
    QString displayName() const;
    bool setDisplayName(const QString &name);
    bool removeDisplayName();

    // Description metadata
    bool hasDescription() const;
    QString description() const;
    bool setDescription(const QString &desc);
    bool removeDescription();

    // Order metadata (for relative calendar ordering)
    bool hasOrder() const;
    int order() const;
    bool setOrder(int order);
    bool removeOrder();

    // Utility
    QString calendarPath() const { return m_calendarPath; }
    bool isValid() const;

    // Static helpers for path construction
    static QString colorFilePath(const QString &calendarPath);
    static QString displayNameFilePath(const QString &calendarPath);
    static QString descriptionFilePath(const QString &calendarPath);
    static QString orderFilePath(const QString &calendarPath);

private:
    QString m_calendarPath;

    // Atomic write helper (writes to temp, then renames)
    bool atomicWrite(const QString &filePath, const QByteArray &data);
    bool removeFile(const QString &filePath);
    QString readFile(const QString &filePath) const;
};

} // namespace Kalburator::Sync

#endif // CALENDARMETADATAMANAGER_H
