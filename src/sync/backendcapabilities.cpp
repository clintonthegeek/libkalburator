#include "backendcapabilities.h"
#include "syncbackend.h"  // For full RecurrenceCapabilities definition
#include <KCalendarCore/Todo>
#include <KCalendarCore/Event>
#include <KCalendarCore/Journal>

// ============================================================================
// IncidenceSupport implementation
// ============================================================================

QStringList IncidenceSupport::supportedComponentTypes() const
{
    QStringList types;
    if (supportsEvents)
        types << QStringLiteral("VEVENT");
    if (supportsTodos)
        types << QStringLiteral("VTODO");
    if (supportsJournals)
        types << QStringLiteral("VJOURNAL");
    return types;
}

// ============================================================================
// PropertySupport implementation
// ============================================================================

QStringList PropertySupport::unsupportedProperties() const
{
    QStringList unsupported;
    if (!supportsPriority)
        unsupported << QStringLiteral("PRIORITY");
    if (!supportsPercentComplete)
        unsupported << QStringLiteral("PERCENT-COMPLETE");
    if (!supportsCategories)
        unsupported << QStringLiteral("CATEGORIES");
    if (!supportsDescription)
        unsupported << QStringLiteral("DESCRIPTION");
    if (!supportsLocation)
        unsupported << QStringLiteral("LOCATION");
    if (!supportsUrl)
        unsupported << QStringLiteral("URL");
    if (!supportsGeo)
        unsupported << QStringLiteral("GEO");
    if (!supportsRelatedTo)
        unsupported << QStringLiteral("RELATED-TO");
    return unsupported;
}

// ============================================================================
// BackendCapabilities implementation
// ============================================================================

bool BackendCapabilities::supportsCalendarType(CalendarType type) const
{
    switch (type) {
    case CalendarType::Event:
        return incidenceSupport.supportsEvents;
    case CalendarType::Todo:
        return incidenceSupport.supportsTodos;
    case CalendarType::Hybrid:
        return incidenceSupport.supportsHybrid;
    }
    return false;
}

QStringList BackendCapabilities::describeLoss(const KCalendarCore::Incidence::Ptr &incidence) const
{
    QStringList losses;

    if (!incidence)
        return losses;

    // Check incidence type support
    if (incidence->type() == KCalendarCore::Incidence::TypeEvent &&
        !incidenceSupport.supportsEvents) {
        losses << QObject::tr("Events are not supported by this backend");
    }
    if (incidence->type() == KCalendarCore::Incidence::TypeTodo &&
        !incidenceSupport.supportsTodos) {
        losses << QObject::tr("Todos are not supported by this backend");
    }
    if (incidence->type() == KCalendarCore::Incidence::TypeJournal &&
        !incidenceSupport.supportsJournals) {
        losses << QObject::tr("Journals are not supported by this backend");
    }

    // Check property support
    if (!propertySupport.supportsPriority && incidence->priority() > 0) {
        losses << QObject::tr("Priority will be lost");
    }

    if (!propertySupport.supportsCategories && !incidence->categories().isEmpty()) {
        losses << QObject::tr("Categories/tags will be lost");
    }

    if (!propertySupport.supportsDescription && !incidence->description().isEmpty()) {
        losses << QObject::tr("Description will be lost");
    }

    if (!propertySupport.supportsLocation && !incidence->location().isEmpty()) {
        losses << QObject::tr("Location will be lost");
    }

    if (!propertySupport.supportsUrl && incidence->url().isValid()) {
        losses << QObject::tr("URL will be lost");
    }

    if (!propertySupport.supportsGeo && incidence->hasGeo()) {
        losses << QObject::tr("Geographic coordinates will be lost");
    }

    // Check RELATED-TO (hierarchy)
    if (!propertySupport.supportsRelatedTo && !incidence->relatedTo().isEmpty()) {
        losses << QObject::tr("Parent relationship will be lost");
    }

    // Check todo-specific properties
    if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
        if (!propertySupport.supportsPercentComplete && todo->percentComplete() > 0) {
            losses << QObject::tr("Percent complete will be lost (converted to checkbox)");
        }
    }

    // Check custom properties that may be backend-specific
    const auto customProps = incidence->customProperties();
    if (!propertySupport.supportsEffort &&
        customProps.contains(QByteArray("X-ORG-EFFORT"))) {
        losses << QObject::tr("Effort estimate will be lost");
    }

    if (!propertySupport.supportsStateSequence &&
        customProps.contains(QByteArray("X-ORG-STATE"))) {
        losses << QObject::tr("Org-mode state sequence will be lost");
    }

    // Check recurrence (delegate to RecurrenceCapabilities)
    if (incidence->recurs()) {
        // Build a temporary SyncBackend-style loss check
        // This is a simplified check; full analysis is in SyncBackend::analyzeRecurrenceLoss
        if (!recurrence.supportsCount && incidence->recurrence()->duration() > 0) {
            losses << QObject::tr("Recurrence count limit will be lost");
        }
        if (!recurrence.supportsUntil && incidence->recurrence()->endDateTime().isValid()) {
            losses << QObject::tr("Recurrence end date will be lost");
        }
        if (!recurrence.supportsExDates && !incidence->recurrence()->exDates().isEmpty()) {
            losses << QObject::tr("Exception dates will be lost");
        }
    }

    return losses;
}

QStringList BackendCapabilities::describeLoss(const KCalendarCore::Incidence::Ptr &incidence,
                                               CalendarType targetType) const
{
    QStringList losses = describeLoss(incidence);

    if (!incidence)
        return losses;

    // Check type constraint if backend has per-calendar restrictions
    if (incidenceSupport.perCalendarRestrictions) {
        bool isEvent = (incidence->type() == KCalendarCore::Incidence::TypeEvent);
        bool isTodo = (incidence->type() == KCalendarCore::Incidence::TypeTodo);

        if (isEvent && targetType == CalendarType::Todo) {
            losses.prepend(QObject::tr("This event cannot be stored in a task-only collection"));
        } else if (isTodo && targetType == CalendarType::Event) {
            losses.prepend(QObject::tr("This task cannot be stored in an event-only collection"));
        }
    }

    return losses;
}

BackendCapabilities BackendCapabilities::localDefaults()
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("local");
    caps.displayName = QObject::tr("Local (iCalendar)");
    caps.description = QObject::tr("Local iCalendar files with full RFC 5545 support");

    // Full incidence support
    caps.incidenceSupport.supportsEvents = true;
    caps.incidenceSupport.supportsTodos = true;
    caps.incidenceSupport.supportsJournals = true;
    caps.incidenceSupport.supportsHybrid = true;
    caps.incidenceSupport.perCalendarRestrictions = false;

    // Full recurrence support (iCalendar spec)
    caps.recurrence.supportsDaily = true;
    caps.recurrence.supportsWeekly = true;
    caps.recurrence.supportsMonthly = true;
    caps.recurrence.supportsYearly = true;
    caps.recurrence.supportsHourly = true;
    caps.recurrence.supportsMinutely = true;
    caps.recurrence.supportsSecondly = true;
    caps.recurrence.supportsByDay = true;
    caps.recurrence.supportsByMonthDay = true;
    caps.recurrence.supportsByYearDay = true;
    caps.recurrence.supportsByWeekNo = true;
    caps.recurrence.supportsByMonth = true;
    caps.recurrence.supportsBySetPos = true;
    caps.recurrence.supportsCount = true;
    caps.recurrence.supportsUntil = true;
    caps.recurrence.maxInterval = 0;
    caps.recurrence.supportsMultipleRRules = true;
    caps.recurrence.supportsExRules = true;
    caps.recurrence.supportsRDates = true;
    caps.recurrence.supportsExDates = true;
    caps.recurrence.backendType = QStringLiteral("local");
    caps.recurrence.displayName = QObject::tr("Local iCalendar");

    // Full property support
    caps.propertySupport.supportsPriority = true;
    caps.propertySupport.supportsPercentComplete = true;
    caps.propertySupport.supportsCategories = true;
    caps.propertySupport.supportsDescription = true;
    caps.propertySupport.supportsLocation = true;
    caps.propertySupport.supportsUrl = true;
    caps.propertySupport.supportsGeo = true;
    caps.propertySupport.supportsCustomProperties = true;
    caps.propertySupport.supportsRelatedTo = true;
    caps.propertySupport.supportsEffort = false;  // Not native iCal
    caps.propertySupport.supportsStateSequence = false;
    caps.propertySupport.supportsClosed = false;
    caps.propertySupport.supportsLogbook = false;

    // Structural capabilities
    caps.structural.supportsHierarchy = true;  // Via RELATED-TO
    caps.structural.supportsTagInheritance = false;
    caps.structural.supportsArchiving = false;
    caps.structural.maxNestingDepth = 0;

    // Calendar CRUD
    caps.calendarCrud.supportsCreate = true;
    caps.calendarCrud.supportsDelete = true;
    caps.calendarCrud.supportsRename = true;
    caps.calendarCrud.supportsColor = true;
    caps.calendarCrud.supportsDescription = true;
    caps.calendarCrud.supportsOrder = true;
    caps.calendarCrud.requiresNetworkDiscovery = false;

    // Sync characteristics
    caps.syncCharacteristics.supportsDeltaSync = false;
    caps.syncCharacteristics.supportsEtags = false;
    caps.syncCharacteristics.supportsBatching = true;
    caps.syncCharacteristics.maxBatchSize = 0;
    caps.syncCharacteristics.requiresFullFetch = true;

    return caps;
}

BackendCapabilities BackendCapabilities::orgmodeDefaults()
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("orgmode");
    caps.displayName = QObject::tr("Org-mode");
    caps.description = QObject::tr("Emacs org-mode files with hierarchical tasks");

    // Full incidence support
    caps.incidenceSupport.supportsEvents = true;
    caps.incidenceSupport.supportsTodos = true;
    caps.incidenceSupport.supportsJournals = false;
    caps.incidenceSupport.supportsHybrid = true;
    caps.incidenceSupport.perCalendarRestrictions = false;

    // Limited recurrence support (simple repeaters only)
    caps.recurrence.supportsDaily = true;
    caps.recurrence.supportsWeekly = true;
    caps.recurrence.supportsMonthly = true;
    caps.recurrence.supportsYearly = true;
    caps.recurrence.supportsHourly = true;
    caps.recurrence.supportsMinutely = false;
    caps.recurrence.supportsSecondly = false;
    caps.recurrence.supportsByDay = false;      // No BYDAY
    caps.recurrence.supportsByMonthDay = false; // No BYMONTHDAY
    caps.recurrence.supportsByYearDay = false;
    caps.recurrence.supportsByWeekNo = false;
    caps.recurrence.supportsByMonth = false;
    caps.recurrence.supportsBySetPos = false;
    caps.recurrence.supportsCount = false;      // No COUNT
    caps.recurrence.supportsUntil = false;      // No UNTIL
    caps.recurrence.maxInterval = 0;
    caps.recurrence.supportsMultipleRRules = false;
    caps.recurrence.supportsExRules = false;
    caps.recurrence.supportsRDates = false;
    caps.recurrence.supportsExDates = false;
    caps.recurrence.backendType = QStringLiteral("orgmode");
    caps.recurrence.displayName = QObject::tr("Org-mode repeaters");

    // Property support (org-mode specific)
    caps.propertySupport.supportsPriority = true;   // [#A], [#B], [#C]
    caps.propertySupport.supportsPercentComplete = false;  // Checkbox only
    caps.propertySupport.supportsCategories = true;  // :tag1:tag2:
    caps.propertySupport.supportsDescription = true;
    caps.propertySupport.supportsLocation = false;
    caps.propertySupport.supportsUrl = true;
    caps.propertySupport.supportsGeo = false;
    caps.propertySupport.supportsCustomProperties = true;  // :PROPERTIES:
    caps.propertySupport.supportsRelatedTo = true;  // Native hierarchy
    caps.propertySupport.supportsEffort = true;     // :Effort:
    caps.propertySupport.supportsStateSequence = true;  // TODO/DONE sequences
    caps.propertySupport.supportsClosed = true;     // CLOSED: timestamp
    caps.propertySupport.supportsLogbook = true;    // :LOGBOOK:

    // Structural capabilities (org-mode strength)
    caps.structural.supportsHierarchy = true;
    caps.structural.supportsTagInheritance = true;
    caps.structural.supportsArchiving = true;
    caps.structural.maxNestingDepth = 0;  // Unlimited

    // Calendar CRUD
    caps.calendarCrud.supportsCreate = true;
    caps.calendarCrud.supportsDelete = true;
    caps.calendarCrud.supportsRename = true;
    caps.calendarCrud.supportsColor = false;  // No native color support
    caps.calendarCrud.supportsDescription = false;
    caps.calendarCrud.supportsOrder = false;
    caps.calendarCrud.requiresNetworkDiscovery = false;

    // Sync characteristics
    caps.syncCharacteristics.supportsDeltaSync = false;
    caps.syncCharacteristics.supportsEtags = false;
    caps.syncCharacteristics.supportsBatching = true;
    caps.syncCharacteristics.maxBatchSize = 0;
    caps.syncCharacteristics.requiresFullFetch = true;

    return caps;
}

BackendCapabilities BackendCapabilities::caldavDefaults()
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("caldav");
    caps.displayName = QObject::tr("CalDAV");
    caps.description = QObject::tr("CalDAV server with per-calendar component restrictions");

    // Incidence support - varies per calendar
    caps.incidenceSupport.supportsEvents = true;
    caps.incidenceSupport.supportsTodos = true;
    caps.incidenceSupport.supportsJournals = false;  // Rare
    caps.incidenceSupport.supportsHybrid = false;    // Usually restricted
    caps.incidenceSupport.perCalendarRestrictions = true;

    // Full recurrence support (server handles iCalendar)
    caps.recurrence.supportsDaily = true;
    caps.recurrence.supportsWeekly = true;
    caps.recurrence.supportsMonthly = true;
    caps.recurrence.supportsYearly = true;
    caps.recurrence.supportsHourly = true;
    caps.recurrence.supportsMinutely = true;
    caps.recurrence.supportsSecondly = true;
    caps.recurrence.supportsByDay = true;
    caps.recurrence.supportsByMonthDay = true;
    caps.recurrence.supportsByYearDay = true;
    caps.recurrence.supportsByWeekNo = true;
    caps.recurrence.supportsByMonth = true;
    caps.recurrence.supportsBySetPos = true;
    caps.recurrence.supportsCount = true;
    caps.recurrence.supportsUntil = true;
    caps.recurrence.maxInterval = 0;
    caps.recurrence.supportsMultipleRRules = true;
    caps.recurrence.supportsExRules = true;
    caps.recurrence.supportsRDates = true;
    caps.recurrence.supportsExDates = true;
    caps.recurrence.backendType = QStringLiteral("caldav");
    caps.recurrence.displayName = QObject::tr("CalDAV Server");

    // Full property support
    caps.propertySupport.supportsPriority = true;
    caps.propertySupport.supportsPercentComplete = true;
    caps.propertySupport.supportsCategories = true;
    caps.propertySupport.supportsDescription = true;
    caps.propertySupport.supportsLocation = true;
    caps.propertySupport.supportsUrl = true;
    caps.propertySupport.supportsGeo = true;
    caps.propertySupport.supportsCustomProperties = true;
    caps.propertySupport.supportsRelatedTo = true;
    caps.propertySupport.supportsEffort = false;
    caps.propertySupport.supportsStateSequence = false;
    caps.propertySupport.supportsClosed = false;
    caps.propertySupport.supportsLogbook = false;

    // Structural capabilities
    caps.structural.supportsHierarchy = true;  // Via RELATED-TO
    caps.structural.supportsTagInheritance = false;
    caps.structural.supportsArchiving = false;
    caps.structural.maxNestingDepth = 0;

    // Calendar CRUD - requires network
    caps.calendarCrud.supportsCreate = true;
    caps.calendarCrud.supportsDelete = true;
    caps.calendarCrud.supportsRename = false;  // Usually not supported
    caps.calendarCrud.supportsColor = true;
    caps.calendarCrud.supportsDescription = true;
    caps.calendarCrud.supportsOrder = false;
    caps.calendarCrud.requiresNetworkDiscovery = true;

    // Sync characteristics
    caps.syncCharacteristics.supportsDeltaSync = true;
    caps.syncCharacteristics.supportsEtags = true;
    caps.syncCharacteristics.supportsBatching = false;  // One request per item
    caps.syncCharacteristics.maxBatchSize = 1;
    caps.syncCharacteristics.requiresFullFetch = false;

    return caps;
}

BackendCapabilities BackendCapabilities::decsyncDefaults()
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("decsync");
    caps.displayName = QObject::tr("DecSync");
    caps.description = QObject::tr("DecSync v2 local sync via Syncthing (no server needed)");

    // DecSync uses directory convention: calendars/ for VEVENT, tasks/ for VTODO.
    // Hybrid is supported transparently: the backend internally maintains both
    // calendars/X and tasks/X collections for a single hybrid calendar ID.
    caps.incidenceSupport.supportsEvents = true;
    caps.incidenceSupport.supportsTodos = true;
    caps.incidenceSupport.supportsJournals = false;
    caps.incidenceSupport.supportsHybrid = true;
    caps.incidenceSupport.perCalendarRestrictions = false;

    // Full recurrence support (iCalendar spec)
    caps.recurrence.supportsDaily = true;
    caps.recurrence.supportsWeekly = true;
    caps.recurrence.supportsMonthly = true;
    caps.recurrence.supportsYearly = true;
    caps.recurrence.supportsHourly = true;
    caps.recurrence.supportsMinutely = true;
    caps.recurrence.supportsSecondly = true;
    caps.recurrence.supportsByDay = true;
    caps.recurrence.supportsByMonthDay = true;
    caps.recurrence.supportsByYearDay = true;
    caps.recurrence.supportsByWeekNo = true;
    caps.recurrence.supportsByMonth = true;
    caps.recurrence.supportsBySetPos = true;
    caps.recurrence.supportsCount = true;
    caps.recurrence.supportsUntil = true;
    caps.recurrence.maxInterval = 0;
    caps.recurrence.supportsMultipleRRules = true;
    caps.recurrence.supportsExRules = true;
    caps.recurrence.supportsRDates = true;
    caps.recurrence.supportsExDates = true;
    caps.recurrence.backendType = QStringLiteral("decsync");
    caps.recurrence.displayName = QObject::tr("DecSync (iCalendar)");

    // Full property support
    caps.propertySupport.supportsPriority = true;
    caps.propertySupport.supportsPercentComplete = true;
    caps.propertySupport.supportsCategories = true;
    caps.propertySupport.supportsDescription = true;
    caps.propertySupport.supportsLocation = true;
    caps.propertySupport.supportsUrl = true;
    caps.propertySupport.supportsGeo = true;
    caps.propertySupport.supportsCustomProperties = true;
    caps.propertySupport.supportsRelatedTo = true;
    caps.propertySupport.supportsEffort = false;
    caps.propertySupport.supportsStateSequence = false;
    caps.propertySupport.supportsClosed = false;
    caps.propertySupport.supportsLogbook = false;

    // Structural capabilities
    caps.structural.supportsHierarchy = true;  // Via RELATED-TO
    caps.structural.supportsTagInheritance = false;
    caps.structural.supportsArchiving = false;
    caps.structural.maxNestingDepth = 0;

    // Calendar CRUD
    caps.calendarCrud.supportsCreate = true;
    caps.calendarCrud.supportsDelete = true;
    caps.calendarCrud.supportsRename = true;
    caps.calendarCrud.supportsColor = true;
    caps.calendarCrud.supportsDescription = false;  // Not in DecSync spec
    caps.calendarCrud.supportsOrder = false;         // Not in DecSync spec
    caps.calendarCrud.requiresNetworkDiscovery = false;

    // Sync characteristics - DecSync uses sequence-based delta sync
    caps.syncCharacteristics.supportsDeltaSync = true;
    caps.syncCharacteristics.supportsEtags = false;
    caps.syncCharacteristics.supportsBatching = true;
    caps.syncCharacteristics.maxBatchSize = 0;
    caps.syncCharacteristics.requiresFullFetch = false;

    return caps;
}

BackendCapabilities BackendCapabilities::akonadiDefaults()
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("akonadi");
    caps.displayName = QObject::tr("Akonadi (KDE PIM)");
    caps.description = QObject::tr("KDE Akonadi PIM framework - accesses calendars from CalDAV, Google, EWS, and other Akonadi resources");

    // Akonadi delegates to underlying resources which support full iCalendar
    caps.incidenceSupport.supportsEvents = true;
    caps.incidenceSupport.supportsTodos = true;
    caps.incidenceSupport.supportsJournals = true;
    caps.incidenceSupport.supportsHybrid = true;
    caps.incidenceSupport.perCalendarRestrictions = true;  // Resources may restrict types

    // Full recurrence support (iCalendar spec via underlying resources)
    caps.recurrence.supportsDaily = true;
    caps.recurrence.supportsWeekly = true;
    caps.recurrence.supportsMonthly = true;
    caps.recurrence.supportsYearly = true;
    caps.recurrence.supportsHourly = true;
    caps.recurrence.supportsMinutely = true;
    caps.recurrence.supportsSecondly = true;
    caps.recurrence.supportsByDay = true;
    caps.recurrence.supportsByMonthDay = true;
    caps.recurrence.supportsByYearDay = true;
    caps.recurrence.supportsByWeekNo = true;
    caps.recurrence.supportsByMonth = true;
    caps.recurrence.supportsBySetPos = true;
    caps.recurrence.supportsCount = true;
    caps.recurrence.supportsUntil = true;
    caps.recurrence.maxInterval = 0;
    caps.recurrence.supportsMultipleRRules = true;
    caps.recurrence.supportsExRules = true;
    caps.recurrence.supportsRDates = true;
    caps.recurrence.supportsExDates = true;
    caps.recurrence.backendType = QStringLiteral("akonadi");
    caps.recurrence.displayName = QObject::tr("Akonadi (iCalendar)");

    // Full property support
    caps.propertySupport.supportsPriority = true;
    caps.propertySupport.supportsPercentComplete = true;
    caps.propertySupport.supportsCategories = true;
    caps.propertySupport.supportsDescription = true;
    caps.propertySupport.supportsLocation = true;
    caps.propertySupport.supportsUrl = true;
    caps.propertySupport.supportsGeo = true;
    caps.propertySupport.supportsCustomProperties = true;
    caps.propertySupport.supportsRelatedTo = true;
    caps.propertySupport.supportsEffort = false;
    caps.propertySupport.supportsStateSequence = false;
    caps.propertySupport.supportsClosed = false;
    caps.propertySupport.supportsLogbook = false;

    // Structural capabilities
    caps.structural.supportsHierarchy = true;  // Via RELATED-TO
    caps.structural.supportsTagInheritance = false;
    caps.structural.supportsArchiving = false;
    caps.structural.maxNestingDepth = 0;

    // Calendar CRUD - Akonadi manages collections via resources
    caps.calendarCrud.supportsCreate = true;
    caps.calendarCrud.supportsDelete = true;
    caps.calendarCrud.supportsRename = true;
    caps.calendarCrud.supportsColor = true;
    caps.calendarCrud.supportsDescription = true;
    caps.calendarCrud.supportsOrder = false;
    caps.calendarCrud.requiresNetworkDiscovery = false;  // Akonadi handles discovery internally

    // Sync characteristics - Akonadi resources handle sync internally
    caps.syncCharacteristics.supportsDeltaSync = true;
    caps.syncCharacteristics.supportsEtags = false;  // Akonadi uses its own change tracking
    caps.syncCharacteristics.supportsBatching = true;
    caps.syncCharacteristics.maxBatchSize = 0;
    caps.syncCharacteristics.requiresFullFetch = false;

    return caps;
}

BackendCapabilities BackendCapabilities::planstanDefaults()
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("planstan");
    caps.displayName = QObject::tr("PlanStan Projects");
    caps.description = QObject::tr("Native PlanStan project files with full planning support");

    caps.incidenceSupport.supportsEvents = false;
    caps.incidenceSupport.supportsTodos = true;
    caps.incidenceSupport.supportsJournals = false;
    caps.incidenceSupport.supportsHybrid = false;
    caps.incidenceSupport.perCalendarRestrictions = false;

    // No recurrence for project tasks
    caps.recurrence.supportsDaily = false;
    caps.recurrence.supportsWeekly = false;
    caps.recurrence.supportsMonthly = false;
    caps.recurrence.supportsYearly = false;
    caps.recurrence.supportsHourly = false;
    caps.recurrence.supportsMinutely = false;
    caps.recurrence.supportsSecondly = false;
    caps.recurrence.supportsByDay = false;
    caps.recurrence.supportsByMonthDay = false;
    caps.recurrence.supportsByYearDay = false;
    caps.recurrence.supportsByWeekNo = false;
    caps.recurrence.supportsByMonth = false;
    caps.recurrence.supportsBySetPos = false;
    caps.recurrence.supportsCount = false;
    caps.recurrence.supportsUntil = false;
    caps.recurrence.maxInterval = 0;
    caps.recurrence.supportsMultipleRRules = false;
    caps.recurrence.supportsExRules = false;
    caps.recurrence.supportsRDates = false;
    caps.recurrence.supportsExDates = false;
    caps.recurrence.backendType = QStringLiteral("planstan");
    caps.recurrence.displayName = QObject::tr("PlanStan Projects");

    caps.propertySupport.supportsPriority = true;
    caps.propertySupport.supportsPercentComplete = true;
    caps.propertySupport.supportsCategories = true;
    caps.propertySupport.supportsDescription = true;
    caps.propertySupport.supportsLocation = false;
    caps.propertySupport.supportsUrl = false;
    caps.propertySupport.supportsGeo = false;
    caps.propertySupport.supportsCustomProperties = true;
    caps.propertySupport.supportsRelatedTo = true;
    caps.propertySupport.supportsEffort = true;
    caps.propertySupport.supportsStateSequence = true;
    caps.propertySupport.supportsClosed = false;
    caps.propertySupport.supportsLogbook = false;

    caps.structural.supportsHierarchy = true;
    caps.structural.supportsTagInheritance = true;
    caps.structural.supportsArchiving = true;
    caps.structural.maxNestingDepth = 0;

    caps.calendarCrud.supportsCreate = true;
    caps.calendarCrud.supportsDelete = true;
    caps.calendarCrud.supportsRename = true;
    caps.calendarCrud.supportsColor = true;
    caps.calendarCrud.supportsDescription = true;
    caps.calendarCrud.supportsOrder = false;
    caps.calendarCrud.requiresNetworkDiscovery = false;

    caps.syncCharacteristics.supportsDeltaSync = false;
    caps.syncCharacteristics.supportsEtags = false;
    caps.syncCharacteristics.supportsBatching = true;
    caps.syncCharacteristics.maxBatchSize = 0;
    caps.syncCharacteristics.requiresFullFetch = true;

    return caps;
}

// ============================================================================
// CalendarCapabilities implementation
// ============================================================================

bool CalendarCapabilities::supportsCalendarType(CalendarType type) const
{
    switch (type) {
    case CalendarType::Event:
        return supportsVEvent;
    case CalendarType::Todo:
        return supportsVTodo;
    case CalendarType::Hybrid:
        return supportsVEvent && supportsVTodo;
    }
    return false;
}

QStringList CalendarCapabilities::supportedComponentTypes() const
{
    QStringList types;
    if (supportsVEvent)
        types << QStringLiteral("VEVENT");
    if (supportsVTodo)
        types << QStringLiteral("VTODO");
    if (supportsVJournal)
        types << QStringLiteral("VJOURNAL");
    return types;
}
