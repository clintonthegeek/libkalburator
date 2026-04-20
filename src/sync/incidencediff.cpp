#include "incidencediff.h"

#include <QRegularExpression>
#include <QTimeZone>
#include "ilocalesource.h"
#include <QDebug>

#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Recurrence>

// ============================================================================
// Property Name Mappings
// ============================================================================

QString IncidenceDiff::propertyDisplayName(const QString &propertyName)
{
    static const QMap<QString, QString> displayNames = {
        // Essential
        {"SUMMARY", QObject::tr("Summary")},
        {"DTSTART", QObject::tr("Start Time")},
        {"DTEND", QObject::tr("End Time")},
        {"DUE", QObject::tr("Due Date")},
        {"DURATION", QObject::tr("Duration")},

        // DateTime
        {"RRULE", QObject::tr("Recurrence Rule")},
        {"RDATE", QObject::tr("Recurrence Dates")},
        {"EXDATE", QObject::tr("Exception Dates")},
        {"RECURRENCE-ID", QObject::tr("Recurrence ID")},

        // Descriptive
        {"DESCRIPTION", QObject::tr("Description")},
        {"LOCATION", QObject::tr("Location")},
        {"URL", QObject::tr("URL")},
        {"CATEGORIES", QObject::tr("Categories")},
        {"COMMENT", QObject::tr("Comment")},
        {"GEO", QObject::tr("Location (GPS)")},

        // Status
        {"STATUS", QObject::tr("Status")},
        {"PRIORITY", QObject::tr("Priority")},
        {"PERCENT-COMPLETE", QObject::tr("Percent Complete")},
        {"COMPLETED", QObject::tr("Completed")},
        {"CLASS", QObject::tr("Classification")},

        // Organizational
        {"ORGANIZER", QObject::tr("Organizer")},
        {"ATTENDEE", QObject::tr("Attendees")},
        {"RELATED-TO", QObject::tr("Related To")},
        {"CONTACT", QObject::tr("Contact")},

        // Other
        {"UID", QObject::tr("Unique ID")},
        {"DTSTAMP", QObject::tr("Timestamp")},
        {"CREATED", QObject::tr("Created")},
        {"LAST-MODIFIED", QObject::tr("Last Modified")},
        {"SEQUENCE", QObject::tr("Sequence")},
        {"TRANSP", QObject::tr("Transparency")},
        {"RESOURCES", QObject::tr("Resources")},
        {"ATTACH", QObject::tr("Attachments")},
        {"VALARM", QObject::tr("Alarms")},
    };

    return displayNames.value(propertyName.toUpper(), propertyName);
}

IncidenceDiff::PropertyCategory IncidenceDiff::propertyCategory(const QString &propertyName)
{
    QString upper = propertyName.toUpper();

    // Essential
    if (upper == "SUMMARY" || upper == "DTSTART" || upper == "DTEND" || upper == "DUE") {
        return Essential;
    }

    // DateTime
    if (upper == "RRULE" || upper == "RDATE" || upper == "EXDATE" ||
        upper == "DURATION" || upper == "RECURRENCE-ID") {
        return DateTime;
    }

    // Descriptive
    if (upper == "DESCRIPTION" || upper == "LOCATION" || upper == "URL" ||
        upper == "CATEGORIES" || upper == "COMMENT" || upper == "GEO") {
        return Descriptive;
    }

    // Status
    if (upper == "STATUS" || upper == "PRIORITY" || upper == "PERCENT-COMPLETE" ||
        upper == "COMPLETED" || upper == "CLASS") {
        return Status;
    }

    // Organizational
    if (upper == "ORGANIZER" || upper == "ATTENDEE" || upper == "RELATED-TO" ||
        upper == "CONTACT") {
        return Organizational;
    }

    return Other;
}

int IncidenceDiff::propertySortPriority(const QString &propertyName)
{
    static const QMap<QString, int> priorities = {
        {"SUMMARY", 0},
        {"DTSTART", 1},
        {"DTEND", 2},
        {"DUE", 3},
        {"LOCATION", 10},
        {"DESCRIPTION", 11},
        {"CATEGORIES", 12},
        {"RRULE", 20},
        {"EXDATE", 21},
        {"STATUS", 30},
        {"PRIORITY", 31},
        {"PERCENT-COMPLETE", 32},
        {"COMPLETED", 33},
        {"ORGANIZER", 40},
        {"ATTENDEE", 41},
        {"RELATED-TO", 42},
        {"URL", 50},
        {"COMMENT", 51},
        {"UID", 100},
        {"DTSTAMP", 101},
        {"CREATED", 102},
        {"LAST-MODIFIED", 103},
        {"SEQUENCE", 104},
    };

    return priorities.value(propertyName.toUpper(), 99);
}

// ============================================================================
// Value Formatting
// ============================================================================

QString IncidenceDiff::formatDateTime(const QString &value)
{
    // Parse iCal datetime formats:
    // 20250120T150000 (local)
    // 20250120T150000Z (UTC)
    // TZID=America/New_York:20250120T150000 (with timezone)

    QString dateStr = value;
    QString tzInfo;

    // Check for TZID parameter
    if (dateStr.contains(':')) {
        QStringList parts = dateStr.split(':');
        if (parts.size() == 2) {
            if (parts[0].startsWith("TZID=")) {
                tzInfo = parts[0].mid(5);
            }
            dateStr = parts[1];
        }
    }

    // Parse the date/time
    QDateTime dt;
    if (dateStr.contains('T')) {
        // Date-time format
        QString format = dateStr.endsWith('Z')
            ? QStringLiteral("yyyyMMdd'T'HHmmss'Z'")
            : QStringLiteral("yyyyMMdd'T'HHmmss");
        dt = QDateTime::fromString(dateStr, format);
        if (dateStr.endsWith('Z')) {
            dt.setTimeZone(QTimeZone::utc());
        } else if (!tzInfo.isEmpty()) {
            dt.setTimeZone(QTimeZone(tzInfo.toUtf8()));
        }
    } else if (dateStr.length() == 8) {
        // Date-only format: 20250120
        QDate date = QDate::fromString(dateStr, QStringLiteral("yyyyMMdd"));
        if (date.isValid()) {
            const auto locale = ILocaleSource::global() ? ILocaleSource::global()->effectiveLocale() : QLocale();
            return locale.toString(date, QLocale::LongFormat);
        }
    }

    if (dt.isValid()) {
        const auto locale = ILocaleSource::global() ? ILocaleSource::global()->effectiveLocale() : QLocale();
        return locale.toString(dt, QLocale::ShortFormat);
    }

    return value;  // Return original if parsing fails
}

QString IncidenceDiff::formatRRule(const QString &value)
{
    // Parse RRULE like: FREQ=DAILY;INTERVAL=2
    QStringList parts = value.split(';');
    QMap<QString, QString> params;

    for (const QString &part : parts) {
        int eq = part.indexOf('=');
        if (eq > 0) {
            params[part.left(eq)] = part.mid(eq + 1);
        }
    }

    QString freq = params.value("FREQ").toLower();
    int interval = params.value("INTERVAL", "1").toInt();

    QString result;
    if (interval == 1) {
        if (freq == "daily") result = QObject::tr("Daily");
        else if (freq == "weekly") result = QObject::tr("Weekly");
        else if (freq == "monthly") result = QObject::tr("Monthly");
        else if (freq == "yearly") result = QObject::tr("Yearly");
        else result = freq;
    } else {
        if (freq == "daily") result = QObject::tr("Every %1 days").arg(interval);
        else if (freq == "weekly") result = QObject::tr("Every %1 weeks").arg(interval);
        else if (freq == "monthly") result = QObject::tr("Every %1 months").arg(interval);
        else if (freq == "yearly") result = QObject::tr("Every %1 years").arg(interval);
        else result = QObject::tr("Every %1 %2").arg(interval).arg(freq);
    }

    // Add count/until if present
    if (params.contains("COUNT")) {
        result += QObject::tr(", %1 times").arg(params["COUNT"]);
    }
    if (params.contains("UNTIL")) {
        result += QObject::tr(", until %1").arg(formatDateTime(params["UNTIL"]));
    }

    // Add BYDAY if present
    if (params.contains("BYDAY")) {
        result += QObject::tr(" on %1").arg(params["BYDAY"]);
    }

    return result;
}

QString IncidenceDiff::formatStatus(const QString &value)
{
    static const QMap<QString, QString> statusNames = {
        {"TENTATIVE", QObject::tr("Tentative")},
        {"CONFIRMED", QObject::tr("Confirmed")},
        {"CANCELLED", QObject::tr("Cancelled")},
        {"NEEDS-ACTION", QObject::tr("Needs Action")},
        {"COMPLETED", QObject::tr("Completed")},
        {"IN-PROCESS", QObject::tr("In Progress")},
        {"DRAFT", QObject::tr("Draft")},
        {"FINAL", QObject::tr("Final")},
    };

    return statusNames.value(value.toUpper(), value);
}

QString IncidenceDiff::formatPriority(const QString &value)
{
    int prio = value.toInt();
    if (prio == 0) return QObject::tr("None");
    if (prio >= 1 && prio <= 3) return QObject::tr("High (%1)").arg(prio);
    if (prio >= 4 && prio <= 6) return QObject::tr("Medium (%1)").arg(prio);
    if (prio >= 7 && prio <= 9) return QObject::tr("Low (%1)").arg(prio);
    return value;
}

QString IncidenceDiff::formatPropertyValue(const QString &propertyName, const QString &value)
{
    if (value.isEmpty()) {
        return QString();
    }

    QString upper = propertyName.toUpper();

    if (upper == "DTSTART" || upper == "DTEND" || upper == "DUE" ||
        upper == "COMPLETED" || upper == "DTSTAMP" || upper == "CREATED" ||
        upper == "LAST-MODIFIED" || upper == "RECURRENCE-ID") {
        return formatDateTime(value);
    }

    if (upper == "RRULE") {
        return formatRRule(value);
    }

    if (upper == "STATUS") {
        return formatStatus(value);
    }

    if (upper == "PRIORITY") {
        return formatPriority(value);
    }

    if (upper == "PERCENT-COMPLETE") {
        return QObject::tr("%1%").arg(value);
    }

    if (upper == "CLASS") {
        if (value.toUpper() == "PUBLIC") return QObject::tr("Public");
        if (value.toUpper() == "PRIVATE") return QObject::tr("Private");
        if (value.toUpper() == "CONFIDENTIAL") return QObject::tr("Confidential");
    }

    if (upper == "TRANSP") {
        if (value.toUpper() == "OPAQUE") return QObject::tr("Busy");
        if (value.toUpper() == "TRANSPARENT") return QObject::tr("Free");
    }

    // EXDATE - may have multiple values
    if (upper == "EXDATE") {
        QStringList dates = value.split(',');
        QStringList formatted;
        for (const QString &date : dates) {
            formatted << formatDateTime(date.trimmed());
        }
        return formatted.join(", ");
    }

    return value;
}

// ============================================================================
// Property Extraction
// ============================================================================

QString IncidenceDiff::getPropertyValue(
    const KCalendarCore::Incidence::Ptr &incidence,
    const QString &propertyName)
{
    if (!incidence) {
        return QString();
    }

    QString upper = propertyName.toUpper();

    // Core properties
    if (upper == "SUMMARY") return incidence->summary();
    if (upper == "DESCRIPTION") return incidence->description();
    if (upper == "LOCATION") return incidence->location();
    if (upper == "UID") return incidence->uid();
    if (upper == "CATEGORIES") return incidence->categories().join(',');
    if (upper == "URL") return incidence->url().toString();
    if (upper == "COMMENT") {
        QStringList comments = incidence->comments();
        return comments.isEmpty() ? QString() : comments.join(QStringLiteral("\\n"));
    }

    // Status properties
    if (upper == "STATUS") {
        switch (incidence->status()) {
        case KCalendarCore::Incidence::StatusTentative: return "TENTATIVE";
        case KCalendarCore::Incidence::StatusConfirmed: return "CONFIRMED";
        case KCalendarCore::Incidence::StatusCompleted: return "COMPLETED";
        case KCalendarCore::Incidence::StatusNeedsAction: return "NEEDS-ACTION";
        case KCalendarCore::Incidence::StatusCanceled: return "CANCELLED";
        case KCalendarCore::Incidence::StatusInProcess: return "IN-PROCESS";
        case KCalendarCore::Incidence::StatusDraft: return "DRAFT";
        case KCalendarCore::Incidence::StatusFinal: return "FINAL";
        default: return QString();
        }
    }

    if (upper == "PRIORITY") {
        int prio = incidence->priority();
        return prio > 0 ? QString::number(prio) : QString();
    }

    if (upper == "CLASS") {
        switch (incidence->secrecy()) {
        case KCalendarCore::Incidence::SecrecyPublic: return "PUBLIC";
        case KCalendarCore::Incidence::SecrecyPrivate: return "PRIVATE";
        case KCalendarCore::Incidence::SecrecyConfidential: return "CONFIDENTIAL";
        }
    }

    // Date/time properties
    if (upper == "DTSTART") {
        if (incidence->dtStart().isValid()) {
            if (incidence->allDay()) {
                return incidence->dtStart().date().toString(Qt::ISODate).remove('-');
            }
            return incidence->dtStart().toString(Qt::ISODate).remove('-').remove(':');
        }
        return QString();
    }

    if (upper == "DTEND") {
        if (auto event = incidence.dynamicCast<KCalendarCore::Event>()) {
            if (event->hasEndDate() && event->dtEnd().isValid()) {
                if (event->allDay()) {
                    return event->dtEnd().date().toString(Qt::ISODate).remove('-');
                }
                return event->dtEnd().toString(Qt::ISODate).remove('-').remove(':');
            }
        }
        return QString();
    }

    if (upper == "DUE") {
        if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
            if (todo->hasDueDate() && todo->dtDue().isValid()) {
                if (todo->allDay()) {
                    return todo->dtDue().date().toString(Qt::ISODate).remove('-');
                }
                return todo->dtDue().toString(Qt::ISODate).remove('-').remove(':');
            }
        }
        return QString();
    }

    if (upper == "COMPLETED") {
        if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
            if (todo->hasCompletedDate()) {
                return todo->completed().toString(Qt::ISODate).remove('-').remove(':');
            }
        }
        return QString();
    }

    if (upper == "PERCENT-COMPLETE") {
        if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
            return QString::number(todo->percentComplete());
        }
        return QString();
    }

    // Recurrence
    if (upper == "RRULE") {
        if (incidence->recurs()) {
            KCalendarCore::ICalFormat format;
            auto cal = QSharedPointer<KCalendarCore::MemoryCalendar>::create(QTimeZone::systemTimeZone());
            KCalendarCore::Incidence::Ptr clone(incidence->clone());
            cal->addIncidence(clone);
            QString ical = format.toString(cal);

            // Extract RRULE line
            QRegularExpression re(QStringLiteral("RRULE:([^\r\n]+)"));
            QRegularExpressionMatch match = re.match(ical);
            if (match.hasMatch()) {
                return match.captured(1);
            }
        }
        return QString();
    }

    if (upper == "EXDATE") {
        if (incidence->recurs()) {
            QStringList exdates;
            for (const QDateTime &dt : incidence->recurrence()->exDateTimes()) {
                exdates << dt.toString(Qt::ISODate).remove('-').remove(':');
            }
            for (const QDate &d : incidence->recurrence()->exDates()) {
                exdates << d.toString(Qt::ISODate).remove('-');
            }
            return exdates.join(',');
        }
        return QString();
    }

    if (upper == "RECURRENCE-ID") {
        if (incidence->hasRecurrenceId()) {
            return incidence->recurrenceId().toString(Qt::ISODate).remove('-').remove(':');
        }
        return QString();
    }

    // Related-to
    if (upper == "RELATED-TO") {
        return incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
    }

    // Geo
    if (upper == "GEO") {
        if (incidence->hasGeo()) {
            return QStringLiteral("%1;%2")
                .arg(incidence->geoLatitude(), 0, 'f', 6)
                .arg(incidence->geoLongitude(), 0, 'f', 6);
        }
        return QString();
    }

    // Transparency (Event only)
    if (upper == "TRANSP") {
        if (auto event = incidence.dynamicCast<KCalendarCore::Event>()) {
            return event->transparency() == KCalendarCore::Event::Opaque ? "OPAQUE" : "TRANSPARENT";
        }
        return QString();
    }

    // Timestamps
    if (upper == "CREATED") {
        if (incidence->created().isValid()) {
            return incidence->created().toString(Qt::ISODate).remove('-').remove(':');
        }
        return QString();
    }

    if (upper == "LAST-MODIFIED") {
        if (incidence->lastModified().isValid()) {
            return incidence->lastModified().toString(Qt::ISODate).remove('-').remove(':');
        }
        return QString();
    }

    if (upper == "DTSTAMP") {
        // DTSTAMP is handled via lastModified in KCalendarCore
        if (incidence->lastModified().isValid()) {
            return incidence->lastModified().toString(Qt::ISODate).remove('-').remove(':');
        }
        return QString();
    }

    if (upper == "SEQUENCE") {
        return QString::number(incidence->revision());
    }

    // Custom properties (X-* properties)
    if (upper.startsWith("X-")) {
        return incidence->customProperty("X-KOLAB", propertyName.mid(2).toUtf8());
    }

    return QString();
}

// ============================================================================
// iCal Parsing
// ============================================================================

QMap<QString, QString> IncidenceDiff::parseIcalProperties(const QString &ical)
{
    QMap<QString, QString> props;

    // Basic line-by-line parsing of iCal
    // Handle line folding (continuation lines start with space or tab)
    QString unfolded = ical;
    unfolded.replace(QRegularExpression(QStringLiteral("\r?\n[ \t]")), QString());

    QStringList lines = unfolded.split(QRegularExpression(QStringLiteral("\r?\n")));

    bool inVEvent = false;
    bool inVTodo = false;
    bool inVJournal = false;

    for (const QString &line : lines) {
        if (line.startsWith("BEGIN:VEVENT")) {
            inVEvent = true;
            continue;
        }
        if (line.startsWith("END:VEVENT")) {
            inVEvent = false;
            continue;
        }
        if (line.startsWith("BEGIN:VTODO")) {
            inVTodo = true;
            continue;
        }
        if (line.startsWith("END:VTODO")) {
            inVTodo = false;
            continue;
        }
        if (line.startsWith("BEGIN:VJOURNAL")) {
            inVJournal = true;
            continue;
        }
        if (line.startsWith("END:VJOURNAL")) {
            inVJournal = false;
            continue;
        }

        // Only parse properties within VEVENT/VTODO/VJOURNAL
        if (!inVEvent && !inVTodo && !inVJournal) {
            continue;
        }

        // Skip nested components
        if (line.startsWith("BEGIN:") || line.startsWith("END:")) {
            continue;
        }

        // Parse property: NAME;params:VALUE or NAME:VALUE
        int colonPos = line.indexOf(':');
        if (colonPos < 1) {
            continue;
        }

        QString propPart = line.left(colonPos);
        QString valuePart = line.mid(colonPos + 1);

        // Extract property name (before any semicolon for parameters)
        int semiPos = propPart.indexOf(';');
        QString propName = semiPos > 0 ? propPart.left(semiPos) : propPart;
        propName = propName.toUpper();

        // For properties with parameters (like DTSTART;TZID=...), include params
        if (semiPos > 0) {
            QString params = propPart.mid(semiPos + 1);
            // Prepend TZID to value if present
            if (params.startsWith("TZID=") || params.contains(";TZID=")) {
                QRegularExpression tzRe(QStringLiteral("TZID=([^;:]+)"));
                auto match = tzRe.match(params);
                if (match.hasMatch()) {
                    valuePart = QStringLiteral("TZID=%1:%2").arg(match.captured(1), valuePart);
                }
            }
        }

        // Handle multi-value properties (EXDATE, ATTENDEE, etc.)
        if (propName == "EXDATE" || propName == "RDATE" || propName == "ATTENDEE") {
            if (props.contains(propName)) {
                props[propName] += QStringLiteral(",%1").arg(valuePart);
            } else {
                props[propName] = valuePart;
            }
        } else {
            props[propName] = valuePart;
        }
    }

    return props;
}

// ============================================================================
// Comparison
// ============================================================================

QList<PropertyDiff> IncidenceDiff::comparePropertyMaps(
    const QMap<QString, QString> &mapA,
    const QMap<QString, QString> &mapB,
    const QMap<QString, QString> &mapBaseline)
{
    QList<PropertyDiff> diffs;
    bool hasBaseline = !mapBaseline.isEmpty();

    // Get all property names
    QSet<QString> allProps;
    for (const QString &key : mapA.keys()) allProps.insert(key);
    for (const QString &key : mapB.keys()) allProps.insert(key);

    // Skip certain properties that shouldn't be compared
    static const QSet<QString> skipProps = {
        "DTSTAMP", "LAST-MODIFIED", "SEQUENCE", "CREATED"
    };

    for (const QString &prop : allProps) {
        if (skipProps.contains(prop)) {
            continue;
        }

        PropertyDiff diff;
        diff.propertyName = prop;
        diff.displayName = propertyDisplayName(prop);
        diff.valueA = mapA.value(prop);
        diff.valueB = mapB.value(prop);
        diff.displayValueA = formatPropertyValue(prop, diff.valueA);
        diff.displayValueB = formatPropertyValue(prop, diff.valueB);

        if (hasBaseline) {
            diff.valueBaseline = mapBaseline.value(prop);
            diff.displayValueBaseline = formatPropertyValue(prop, diff.valueBaseline);
        }

        // Determine state
        bool inA = !diff.valueA.isEmpty();
        bool inB = !diff.valueB.isEmpty();
        bool inBase = !diff.valueBaseline.isEmpty();
        bool aEqualsB = (diff.valueA == diff.valueB);

        if (aEqualsB) {
            diff.state = PropertyDiff::Identical;
        } else if (!inA && inB) {
            diff.state = PropertyDiff::OnlyInB;
        } else if (inA && !inB) {
            diff.state = PropertyDiff::OnlyInA;
        } else if (hasBaseline) {
            // 3-way comparison
            bool aEqualsBase = (diff.valueA == diff.valueBaseline);
            bool bEqualsBase = (diff.valueB == diff.valueBaseline);

            if (aEqualsBase && !bEqualsBase) {
                diff.state = PropertyDiff::AMatchesBaseline;
                // B changed, so default to B
                diff.resolution = PropertyDiff::UseB;
            } else if (!aEqualsBase && bEqualsBase) {
                diff.state = PropertyDiff::BMatchesBaseline;
                // A changed, so default to A
                diff.resolution = PropertyDiff::UseA;
            } else if (!aEqualsBase && !bEqualsBase && aEqualsB) {
                diff.state = PropertyDiff::BothChangedSame;
            } else {
                diff.state = PropertyDiff::BothChangedDifferent;
                // True conflict - needs resolution
            }
        } else {
            // 2-way comparison
            diff.state = PropertyDiff::BothDifferent;
        }

        // Only include if there's an actual difference
        if (diff.state != PropertyDiff::Identical) {
            diffs.append(diff);
        }
    }

    // Sort by priority
    std::sort(diffs.begin(), diffs.end(), [](const PropertyDiff &a, const PropertyDiff &b) {
        return propertySortPriority(a.propertyName) < propertySortPriority(b.propertyName);
    });

    return diffs;
}

QList<PropertyDiff> IncidenceDiff::compare(
    const KCalendarCore::Incidence::Ptr &incidenceA,
    const KCalendarCore::Incidence::Ptr &incidenceB,
    const KCalendarCore::Incidence::Ptr &baseline)
{
    if (!incidenceA || !incidenceB) {
        return {};
    }

    // Properties to compare
    static const QStringList propsToCompare = {
        "SUMMARY", "DTSTART", "DTEND", "DUE", "DURATION",
        "DESCRIPTION", "LOCATION", "URL", "CATEGORIES", "GEO",
        "STATUS", "PRIORITY", "PERCENT-COMPLETE", "COMPLETED", "CLASS",
        "RRULE", "EXDATE", "RECURRENCE-ID",
        "RELATED-TO", "TRANSP"
    };

    QMap<QString, QString> mapA, mapB, mapBaseline;

    for (const QString &prop : propsToCompare) {
        QString valA = getPropertyValue(incidenceA, prop);
        QString valB = getPropertyValue(incidenceB, prop);

        if (!valA.isEmpty()) mapA[prop] = valA;
        if (!valB.isEmpty()) mapB[prop] = valB;

        if (baseline) {
            QString valBase = getPropertyValue(baseline, prop);
            if (!valBase.isEmpty()) mapBaseline[prop] = valBase;
        }
    }

    return comparePropertyMaps(mapA, mapB, mapBaseline);
}

QList<PropertyDiff> IncidenceDiff::compareIcal(
    const QString &icalA,
    const QString &icalB,
    const QString &icalBaseline)
{
    QMap<QString, QString> mapA = parseIcalProperties(icalA);
    QMap<QString, QString> mapB = parseIcalProperties(icalB);
    QMap<QString, QString> mapBaseline = icalBaseline.isEmpty()
        ? QMap<QString, QString>()
        : parseIcalProperties(icalBaseline);

    return comparePropertyMaps(mapA, mapB, mapBaseline);
}

// ============================================================================
// Property Application
// ============================================================================

bool IncidenceDiff::applyPropertyToIncidence(
    const KCalendarCore::Incidence::Ptr &incidence,
    const QString &propertyName,
    const QString &value)
{
    if (!incidence) {
        return false;
    }

    QString upper = propertyName.toUpper();

    // Core properties
    if (upper == "SUMMARY") {
        incidence->setSummary(value);
        return true;
    }
    if (upper == "DESCRIPTION") {
        incidence->setDescription(value);
        return true;
    }
    if (upper == "LOCATION") {
        incidence->setLocation(value);
        return true;
    }
    if (upper == "CATEGORIES") {
        incidence->setCategories(value.split(','));
        return true;
    }
    if (upper == "URL") {
        incidence->setUrl(QUrl(value));
        return true;
    }

    // Status
    if (upper == "STATUS") {
        QString upper = value.toUpper();
        if (upper == "TENTATIVE") incidence->setStatus(KCalendarCore::Incidence::StatusTentative);
        else if (upper == "CONFIRMED") incidence->setStatus(KCalendarCore::Incidence::StatusConfirmed);
        else if (upper == "CANCELLED") incidence->setStatus(KCalendarCore::Incidence::StatusCanceled);
        else if (upper == "NEEDS-ACTION") incidence->setStatus(KCalendarCore::Incidence::StatusNeedsAction);
        else if (upper == "COMPLETED") incidence->setStatus(KCalendarCore::Incidence::StatusCompleted);
        else if (upper == "IN-PROCESS") incidence->setStatus(KCalendarCore::Incidence::StatusInProcess);
        return true;
    }

    if (upper == "PRIORITY") {
        incidence->setPriority(value.toInt());
        return true;
    }

    if (upper == "CLASS") {
        QString upper = value.toUpper();
        if (upper == "PUBLIC") incidence->setSecrecy(KCalendarCore::Incidence::SecrecyPublic);
        else if (upper == "PRIVATE") incidence->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
        else if (upper == "CONFIDENTIAL") incidence->setSecrecy(KCalendarCore::Incidence::SecrecyConfidential);
        return true;
    }

    // Date/time - parse iCal format
    auto parseIcalDateTime = [](const QString &str) -> QDateTime {
        QString s = str;
        QTimeZone tz = QTimeZone::systemTimeZone();

        // Check for TZID prefix
        if (s.contains(':')) {
            QStringList parts = s.split(':');
            if (parts[0].startsWith("TZID=")) {
                tz = QTimeZone(parts[0].mid(5).toUtf8());
            }
            s = parts.last();
        }

        if (s.contains('T')) {
            QString fmt = s.endsWith('Z')
                ? QStringLiteral("yyyyMMdd'T'HHmmss'Z'")
                : QStringLiteral("yyyyMMdd'T'HHmmss");
            QDateTime dt = QDateTime::fromString(s, fmt);
            if (s.endsWith('Z')) {
                dt.setTimeZone(QTimeZone::utc());
            } else {
                dt.setTimeZone(tz);
            }
            return dt;
        } else if (s.length() == 8) {
            QDate date = QDate::fromString(s, QStringLiteral("yyyyMMdd"));
            return QDateTime(date, QTime(0, 0), tz);
        }
        return QDateTime();
    };

    if (upper == "DTSTART") {
        QDateTime dt = parseIcalDateTime(value);
        if (dt.isValid()) {
            incidence->setDtStart(dt);
            return true;
        }
    }

    if (upper == "DTEND") {
        if (auto event = incidence.dynamicCast<KCalendarCore::Event>()) {
            QDateTime dt = parseIcalDateTime(value);
            if (dt.isValid()) {
                event->setDtEnd(dt);
                return true;
            }
        }
    }

    if (upper == "DUE") {
        if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
            QDateTime dt = parseIcalDateTime(value);
            if (dt.isValid()) {
                todo->setDtDue(dt);
                return true;
            }
        }
    }

    if (upper == "COMPLETED") {
        if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
            QDateTime dt = parseIcalDateTime(value);
            if (dt.isValid()) {
                todo->setCompleted(dt);
                return true;
            }
        }
    }

    if (upper == "PERCENT-COMPLETE") {
        if (auto todo = incidence.dynamicCast<KCalendarCore::Todo>()) {
            todo->setPercentComplete(value.toInt());
            return true;
        }
    }

    if (upper == "RELATED-TO") {
        incidence->setRelatedTo(value, KCalendarCore::Incidence::RelTypeParent);
        return true;
    }

    if (upper == "GEO") {
        QStringList parts = value.split(';');
        if (parts.size() == 2) {
            incidence->setGeoLatitude(parts[0].toFloat());
            incidence->setGeoLongitude(parts[1].toFloat());
            return true;
        }
    }

    if (upper == "TRANSP") {
        if (auto event = incidence.dynamicCast<KCalendarCore::Event>()) {
            if (value.toUpper() == "OPAQUE") {
                event->setTransparency(KCalendarCore::Event::Opaque);
            } else {
                event->setTransparency(KCalendarCore::Event::Transparent);
            }
            return true;
        }
    }

    // Recurrence properties are more complex - skip for now
    if (upper == "RRULE" || upper == "EXDATE" || upper == "RDATE") {
        qWarning() << "IncidenceDiff::applyPropertyToIncidence: Recurrence properties not yet supported:" << propertyName;
        return false;
    }

    return false;
}

// ============================================================================
// Merge
// ============================================================================

KCalendarCore::Incidence::Ptr IncidenceDiff::merge(
    const KCalendarCore::Incidence::Ptr &base,
    const QList<PropertyDiff> &diffs)
{
    if (!base) {
        return nullptr;
    }

    KCalendarCore::Incidence::Ptr result(base->clone());

    for (const PropertyDiff &diff : diffs) {
        QString valueToUse;

        switch (diff.resolution) {
        case PropertyDiff::UseA:
            valueToUse = diff.valueA;
            break;
        case PropertyDiff::UseB:
            valueToUse = diff.valueB;
            break;
        case PropertyDiff::UseBaseline:
            valueToUse = diff.valueBaseline;
            break;
        case PropertyDiff::UseCustom:
            valueToUse = diff.customValue;
            break;
        case PropertyDiff::Unresolved:
            // Skip unresolved properties
            continue;
        }

        applyPropertyToIncidence(result, diff.propertyName, valueToUse);
    }

    return result;
}

// ============================================================================
// Exception Modified Properties Tracking
// ============================================================================

QStringList IncidenceDiff::modifiedPropertiesFromMaster(
    const KCalendarCore::Incidence::Ptr &master,
    const KCalendarCore::Incidence::Ptr &exception)
{
    if (!master || !exception) {
        return {};
    }

    QStringList modifiedProps;

    // Compare and collect all differing property names
    QList<PropertyDiff> diffs = compare(master, exception);

    for (const PropertyDiff &diff : diffs) {
        // Skip properties that are expected to differ between master and exception
        if (diff.propertyName == "RECURRENCE-ID" ||
            diff.propertyName == "UID" ||
            diff.propertyName == "RRULE" ||
            diff.propertyName == "EXDATE" ||
            diff.propertyName == "RDATE") {
            continue;
        }

        // Include any property that has a difference
        if (diff.state != PropertyDiff::Identical) {
            modifiedProps << diff.propertyName;
        }
    }

    return modifiedProps;
}

QStringList IncidenceDiff::getModifiedPropertiesMarker(
    const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!incidence) {
        return {};
    }

    // Read the custom property
    QString value = incidence->nonKDECustomProperty(MODIFIED_PROPS_PROPERTY);
    if (value.isEmpty()) {
        return {};
    }

    // Property list is stored as comma-separated values
    return value.split(',', Qt::SkipEmptyParts);
}

void IncidenceDiff::setModifiedPropertiesMarker(
    const KCalendarCore::Incidence::Ptr &incidence,
    const QStringList &properties)
{
    if (!incidence) {
        return;
    }

    if (properties.isEmpty()) {
        // Clear the property if no modified properties
        incidence->removeNonKDECustomProperty(MODIFIED_PROPS_PROPERTY);
    } else {
        // Store as comma-separated list
        QString value = properties.join(',');
        incidence->setNonKDECustomProperty(MODIFIED_PROPS_PROPERTY, value);
    }
}

void IncidenceDiff::clearModifiedPropertiesMarker(
    const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!incidence) {
        return;
    }

    incidence->removeNonKDECustomProperty(MODIFIED_PROPS_PROPERTY);
}

// ============================================================================
// Exception Inherent Properties
// ============================================================================

const QStringList IncidenceDiff::EXCEPTION_INHERENT_PROPERTIES = {
    QStringLiteral("UID"),
    QStringLiteral("RECURRENCE-ID"),
    QStringLiteral("RELATED-TO"),
    QStringLiteral("EXDATE"),
    QStringLiteral("RRULE"),
    QStringLiteral("RDATE"),
    // Date/time properties are inherently different (exception is on a different occurrence)
    QStringLiteral("DTSTART"),
    QStringLiteral("DTEND"),
    QStringLiteral("DUE"),
    // Metadata
    QStringLiteral("DTSTAMP"),
    QStringLiteral("LAST-MODIFIED"),
    QStringLiteral("SEQUENCE"),
    QStringLiteral("CREATED")
};

QList<PropertyDiff> IncidenceDiff::filterExceptionDiffs(const QList<PropertyDiff> &diffs)
{
    QList<PropertyDiff> filtered;

    for (const PropertyDiff &diff : diffs) {
        if (!EXCEPTION_INHERENT_PROPERTIES.contains(diff.propertyName)) {
            filtered.append(diff);
        }
    }

    return filtered;
}

bool IncidenceDiff::isExceptionIdenticalToMaster(
    const KCalendarCore::Incidence::Ptr &master,
    const KCalendarCore::Incidence::Ptr &exception,
    const QDateTime &occurrenceDateTime)
{
    if (!master || !exception) {
        return false;
    }

    // We need to check if the exception is semantically identical to what
    // the series would generate for this occurrence date.
    //
    // For a recurring event, each occurrence inherits:
    // - Same SUMMARY, DESCRIPTION, LOCATION, CATEGORIES, etc.
    // - DTSTART/DTEND shifted to the occurrence date (preserving time and duration)
    //
    // So we compare:
    // 1. DTSTART: exception should match occurrenceDateTime
    // 2. DTEND: exception should match occurrenceDateTime + original duration
    // 3. All other semantic properties: exception should match master

    // Check DTSTART matches the occurrence
    QDateTime exceptionStart = exception->dtStart();
    if (exceptionStart.date() != occurrenceDateTime.date()) {
        return false;
    }
    if (!exception->allDay() && exceptionStart.time() != master->dtStart().time()) {
        return false;  // Time should match master's time
    }

    // Check DTEND preserves the same duration
    auto masterEvent = master.dynamicCast<KCalendarCore::Event>();
    auto exceptionEvent = exception.dynamicCast<KCalendarCore::Event>();
    if (masterEvent && exceptionEvent) {
        qint64 masterDuration = masterEvent->dtStart().secsTo(masterEvent->dtEnd());
        qint64 exceptionDuration = exceptionEvent->dtStart().secsTo(exceptionEvent->dtEnd());
        if (masterDuration != exceptionDuration) {
            return false;
        }
    }

    // Check all other semantic properties match master
    // Properties to compare (excluding date/time and structural)
    static const QStringList propsToCompare = {
        QStringLiteral("SUMMARY"),
        QStringLiteral("DESCRIPTION"),
        QStringLiteral("LOCATION"),
        QStringLiteral("CATEGORIES"),
        QStringLiteral("URL"),
        QStringLiteral("STATUS"),
        QStringLiteral("PRIORITY"),
        QStringLiteral("CLASS"),
        QStringLiteral("GEO"),
        QStringLiteral("TRANSP"),
        QStringLiteral("PERCENT-COMPLETE")
    };

    for (const QString &prop : propsToCompare) {
        QString masterVal = getPropertyValue(master, prop);
        QString exceptionVal = getPropertyValue(exception, prop);
        if (masterVal != exceptionVal) {
            return false;
        }
    }

    return true;
}
