#include "calendarmetadatamanager.h"

#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QTextStream>
#include <QDebug>

namespace Kalburator::Sync {

// Vdir metadata file names (no extensions per spec)
static const QString COLOR_FILE = QStringLiteral("color");
static const QString DISPLAYNAME_FILE = QStringLiteral("displayname");
static const QString DESCRIPTION_FILE = QStringLiteral("description");
static const QString ORDER_FILE = QStringLiteral("order");

CalendarMetadataManager::CalendarMetadataManager(const QString &calendarPath)
    : m_calendarPath(calendarPath)
{
}

bool CalendarMetadataManager::isValid() const
{
    return !m_calendarPath.isEmpty() && QDir(m_calendarPath).exists();
}

// ============================================================================
// Color metadata
// ============================================================================

QString CalendarMetadataManager::colorFilePath(const QString &calendarPath)
{
    return QDir(calendarPath).filePath(COLOR_FILE);
}

bool CalendarMetadataManager::hasColor() const
{
    return QFile::exists(colorFilePath(m_calendarPath));
}

QColor CalendarMetadataManager::color() const
{
    QString content = readFile(colorFilePath(m_calendarPath));
    if (content.isEmpty()) {
        return QColor();
    }

    // Parse #RRGGBB format
    content = content.trimmed();
    if (content.startsWith('#') && content.length() == 7) {
        return QColor(content);
    }

    qWarning() << "CalendarMetadataManager: Invalid color format:" << content;
    return QColor();
}

bool CalendarMetadataManager::setColor(const QColor &color)
{
    if (!color.isValid()) {
        return removeColor();
    }

    // Format as #RRGGBB
    QString hexColor = color.name(QColor::HexRgb).toUpper();
    return atomicWrite(colorFilePath(m_calendarPath), hexColor.toUtf8());
}

bool CalendarMetadataManager::removeColor()
{
    return removeFile(colorFilePath(m_calendarPath));
}

// ============================================================================
// Display name metadata
// ============================================================================

QString CalendarMetadataManager::displayNameFilePath(const QString &calendarPath)
{
    return QDir(calendarPath).filePath(DISPLAYNAME_FILE);
}

bool CalendarMetadataManager::hasDisplayName() const
{
    return QFile::exists(displayNameFilePath(m_calendarPath));
}

QString CalendarMetadataManager::displayName() const
{
    return readFile(displayNameFilePath(m_calendarPath)).trimmed();
}

bool CalendarMetadataManager::setDisplayName(const QString &name)
{
    if (name.isEmpty()) {
        return removeDisplayName();
    }

    return atomicWrite(displayNameFilePath(m_calendarPath), name.toUtf8());
}

bool CalendarMetadataManager::removeDisplayName()
{
    return removeFile(displayNameFilePath(m_calendarPath));
}

// ============================================================================
// Description metadata
// ============================================================================

QString CalendarMetadataManager::descriptionFilePath(const QString &calendarPath)
{
    return QDir(calendarPath).filePath(DESCRIPTION_FILE);
}

bool CalendarMetadataManager::hasDescription() const
{
    return QFile::exists(descriptionFilePath(m_calendarPath));
}

QString CalendarMetadataManager::description() const
{
    return readFile(descriptionFilePath(m_calendarPath)).trimmed();
}

bool CalendarMetadataManager::setDescription(const QString &desc)
{
    if (desc.isEmpty()) {
        return removeDescription();
    }

    return atomicWrite(descriptionFilePath(m_calendarPath), desc.toUtf8());
}

bool CalendarMetadataManager::removeDescription()
{
    return removeFile(descriptionFilePath(m_calendarPath));
}

// ============================================================================
// Order metadata
// ============================================================================

QString CalendarMetadataManager::orderFilePath(const QString &calendarPath)
{
    return QDir(calendarPath).filePath(ORDER_FILE);
}

bool CalendarMetadataManager::hasOrder() const
{
    return QFile::exists(orderFilePath(m_calendarPath));
}

int CalendarMetadataManager::order() const
{
    QString content = readFile(orderFilePath(m_calendarPath));
    if (content.isEmpty()) {
        return 0;
    }

    bool ok = false;
    int value = content.trimmed().toInt(&ok);
    if (!ok) {
        qWarning() << "CalendarMetadataManager: Invalid order format:" << content;
        return 0;
    }

    return value;
}

bool CalendarMetadataManager::setOrder(int order)
{
    return atomicWrite(orderFilePath(m_calendarPath),
                       QString::number(order).toUtf8());
}

bool CalendarMetadataManager::removeOrder()
{
    return removeFile(orderFilePath(m_calendarPath));
}

// ============================================================================
// Private helpers
// ============================================================================

bool CalendarMetadataManager::atomicWrite(const QString &filePath, const QByteArray &data)
{
    // QSaveFile provides atomic writes (temp file + rename)
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "CalendarMetadataManager: Failed to open for writing:" << filePath;
        return false;
    }

    if (file.write(data) != data.size()) {
        qWarning() << "CalendarMetadataManager: Failed to write data to:" << filePath;
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        qWarning() << "CalendarMetadataManager: Failed to commit write to:" << filePath;
        return false;
    }

    return true;
}

bool CalendarMetadataManager::removeFile(const QString &filePath)
{
    if (!QFile::exists(filePath)) {
        return true;  // Already doesn't exist
    }

    if (!QFile::remove(filePath)) {
        qWarning() << "CalendarMetadataManager: Failed to remove:" << filePath;
        return false;
    }

    return true;
}

QString CalendarMetadataManager::readFile(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.exists()) {
        return QString();
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "CalendarMetadataManager: Failed to open for reading:" << filePath;
        return QString();
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return stream.readAll();
}


} // namespace Kalburator::Sync
