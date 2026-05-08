#include "logicalcalendarbuilder.h"
#include <QUuid>
#include <QSet>
#include <QDebug>

namespace Kalburator::Sync {

LogicalCalendarBuilder::LogicalCalendarBuilder()
{
}

void LogicalCalendarBuilder::setBackendCapabilities(const QString &backendId,
                                                     const BackendCapabilities &caps)
{
    m_capabilities[backendId] = caps;
}

void LogicalCalendarBuilder::addDiscoveredCalendars(const QString &backendId,
                                                     const QList<DiscoveredCalendar> &calendars)
{
    m_discovered[backendId].append(calendars);
}

void LogicalCalendarBuilder::setExistingCalendars(const QList<LogicalCalendar> &existing)
{
    m_existing = existing;
}

void LogicalCalendarBuilder::setPrimaryBackendId(const QString &backendId)
{
    m_primaryBackendId = backendId;
}

void LogicalCalendarBuilder::setSyncBackendOrder(const QStringList &backendIds)
{
    m_syncBackendOrder = backendIds;
}

QList<LogicalCalendar> LogicalCalendarBuilder::autoMatch()
{
    m_warnings.clear();
    QList<LogicalCalendar> result;

    // Group all discovered calendars by normalized name
    QMap<QString, QList<DiscoveredCalendar>> byName;
    for (auto it = m_discovered.begin(); it != m_discovered.end(); ++it) {
        for (const auto &cal : it.value()) {
            QString normalized = normalizeNameForMatching(cal.name);
            byName[normalized].append(cal);
        }
    }

    // Create LogicalCalendar for each unique name
    for (auto it = byName.begin(); it != byName.end(); ++it) {
        const QString &normalizedName = it.key();
        const QList<DiscoveredCalendar> &calendars = it.value();

        // Skip if already exists
        bool exists = false;
        for (const auto &lc : m_existing) {
            if (normalizeNameForMatching(lc.displayName) == normalizedName) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        // Create new LogicalCalendar
        LogicalCalendar logCal;
        logCal.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        logCal.displayName = calendars.first().name;  // Use first discovered name
        logCal.enabled = true;
        logCal.visible = true;

        // Determine type from intersection of all backends' support
        logCal.type = determineCalendarType(calendars);

        // Use color from first calendar that has one
        for (const auto &cal : calendars) {
            if (cal.color.isValid()) {
                logCal.color = cal.color;
                break;
            }
        }

        // Create bindings for each backend that has this calendar
        QSet<QString> boundBackends;

        // Primary binding from primary backend (if discovered there)
        for (const auto &cal : calendars) {
            if (cal.backendId == m_primaryBackendId) {
                logCal.bindings.append(
                    createBindingFromDiscovery(cal, BackendRole::Primary));
                boundBackends.insert(cal.backendId);
                break;
            }
        }

        // If not found on primary, create pending binding
        if (!boundBackends.contains(m_primaryBackendId) && !m_primaryBackendId.isEmpty()) {
            logCal.bindings.append(
                createPendingBinding(m_primaryBackendId,
                                    normalizeCalendarId(logCal.displayName),
                                    BackendRole::Primary));
            boundBackends.insert(m_primaryBackendId);
        }

        // Secondary/Tertiary/etc. bindings from sync backends
        int roleIndex = 0;
        for (const QString &syncBackendId : m_syncBackendOrder) {
            if (boundBackends.contains(syncBackendId)) continue;

            BackendRole role = roleForBackendIndex(roleIndex++);

            // Check if discovered on this backend
            bool found = false;
            for (const auto &cal : calendars) {
                if (cal.backendId == syncBackendId) {
                    logCal.bindings.append(
                        createBindingFromDiscovery(cal, role));
                    boundBackends.insert(syncBackendId);
                    found = true;
                    break;
                }
            }

            // If not found, create pending binding
            if (!found) {
                logCal.bindings.append(
                    createPendingBinding(syncBackendId,
                                        normalizeCalendarId(logCal.displayName),
                                        role));
            }
        }

        // Enable sync if we have secondary+ bindings
        logCal.syncEnabled = (logCal.syncBindings().size() > 0);

        result.append(logCal);
    }

    return result;
}

LogicalCalendar LogicalCalendarBuilder::createCalendar(const QString &displayName,
                                                        CalendarType type,
                                                        const QList<CalendarBackendBinding> &bindings)
{
    LogicalCalendar logCal;
    logCal.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    logCal.displayName = displayName;
    logCal.type = type;
    logCal.bindings = bindings;
    logCal.enabled = true;
    logCal.visible = true;
    // Enable sync if we have secondary+ bindings
    logCal.syncEnabled = (logCal.syncBindings().size() > 0);

    // Validate all bindings
    for (const auto &binding : bindings) {
        QString warning = validateBinding(binding, type);
        if (!warning.isEmpty()) {
            m_warnings.append(warning);
        }
    }

    return logCal;
}

CalendarBackendBinding LogicalCalendarBuilder::createBindingFromDiscovery(
    const DiscoveredCalendar &discovered,
    BackendRole role)
{
    CalendarBackendBinding binding;
    binding.backendId = discovered.backendId;
    binding.calendarId = discovered.calendarId;
    binding.role = role;
    binding.enabled = true;
    binding.needsCreation = false;  // Already exists!

    // Copy ALL metadata from discovery
    binding.metadata = discovered.metadata;

    // Determine calendar type for validation
    CalendarType calType = CalendarType::Hybrid;
    if (discovered.supportsVEvent && !discovered.supportsVTodo) {
        calType = CalendarType::Event;
    } else if (!discovered.supportsVEvent && discovered.supportsVTodo) {
        calType = CalendarType::Todo;
    }

    // Validate and collect warnings
    QString warning = validateBinding(binding, calType);
    if (!warning.isEmpty()) {
        m_warnings.append(warning);
    }

    return binding;
}

CalendarBackendBinding LogicalCalendarBuilder::createPendingBinding(
    const QString &backendId,
    const QString &calendarId,
    BackendRole role)
{
    CalendarBackendBinding binding;
    binding.backendId = backendId;
    binding.calendarId = calendarId;
    binding.role = role;
    binding.enabled = true;
    binding.needsCreation = true;

    // Metadata will be populated by backend during creation
    // (e.g., RemoteCalendarBackend::prepareCreationMetadata)

    return binding;
}

QStringList LogicalCalendarBuilder::validationWarnings() const
{
    return m_warnings;
}

QString LogicalCalendarBuilder::validateBinding(const CalendarBackendBinding &binding,
                                                 CalendarType calendarType) const
{
    if (!m_capabilities.contains(binding.backendId)) {
        return QString();  // No capabilities registered, can't validate
    }

    const BackendCapabilities &caps = m_capabilities[binding.backendId];

    // Check calendar type support
    if (!caps.supportsCalendarType(calendarType)) {
        QString typeStr;
        switch (calendarType) {
            case CalendarType::Event: typeStr = QStringLiteral("events"); break;
            case CalendarType::Todo: typeStr = QStringLiteral("todos"); break;
            case CalendarType::Hybrid: typeStr = QStringLiteral("mixed events/todos"); break;
        }
        return QStringLiteral("Backend '%1' may not fully support %2")
            .arg(caps.displayName, typeStr);
    }

    return QString();  // Valid
}

QString LogicalCalendarBuilder::normalizeCalendarId(const QString &displayName)
{
    // Convert "Work Calendar" -> "work-calendar"
    QString result = displayName.toLower();
    result.replace(QLatin1Char(' '), QLatin1Char('-'));

    // Remove any characters that aren't alphanumeric or hyphen
    QString cleaned;
    for (const QChar &c : result) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            cleaned += c;
        }
    }

    return cleaned.isEmpty() ? QStringLiteral("calendar") : cleaned;
}

bool LogicalCalendarBuilder::isCalendarIdUsed(const QString &calendarId,
                                               const QString &backendId) const
{
    for (const auto &lc : m_existing) {
        for (const auto &binding : lc.bindings) {
            if (binding.backendId == backendId && binding.calendarId == calendarId) {
                return true;
            }
        }
    }
    return false;
}

void LogicalCalendarBuilder::clear()
{
    m_capabilities.clear();
    m_discovered.clear();
    m_existing.clear();
    m_primaryBackendId.clear();
    m_syncBackendOrder.clear();
    m_warnings.clear();
}

QString LogicalCalendarBuilder::normalizeNameForMatching(const QString &name) const
{
    // Case-insensitive, trimmed
    return name.trimmed().toLower();
}

BackendRole LogicalCalendarBuilder::roleForBackendIndex(int index) const
{
    switch (index) {
        case 0: return BackendRole::Sync1;
        case 1: return BackendRole::Sync2;
        case 2: return BackendRole::Sync3;
        case 3: return BackendRole::Sync4;
        default: {
            // Support Sync5, Sync6, etc. dynamically
            int roleValue = index + 1;  // index 0 -> Sync1 (value 1)
            return static_cast<BackendRole>(roleValue);
        }
    }
}

CalendarType LogicalCalendarBuilder::determineCalendarType(const QList<DiscoveredCalendar> &calendars) const
{
    if (calendars.isEmpty()) {
        return CalendarType::Hybrid;
    }

    bool allSupportEvents = true;
    bool allSupportTodos = true;

    for (const auto &cal : calendars) {
        if (!cal.supportsVEvent) allSupportEvents = false;
        if (!cal.supportsVTodo) allSupportTodos = false;
    }

    if (allSupportEvents && allSupportTodos) {
        return CalendarType::Hybrid;
    } else if (allSupportEvents) {
        return CalendarType::Event;
    } else if (allSupportTodos) {
        return CalendarType::Todo;
    }

    // Mixed support - default to hybrid and let user adjust
    return CalendarType::Hybrid;
}


} // namespace Kalburator::Sync
