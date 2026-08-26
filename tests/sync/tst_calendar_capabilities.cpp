// VP.a (vtodo-parity W8) — unified capability/trait query API tests.
//
// Pins:
// 1. Static per-backend-family reports (CapabilityReports) against the
//    EEE edge loss profiles + docs/campaign/eee/vendor-rest-api-wire-notes.md.
// 2. CalendarCapabilities toJson/fromJson round-trip (all enum values).
// 3. capabilitiesFromDiscovery() mapping from discovery JSON fixtures
//    (the .kalb-persisted DiscoveredCapabilities shape).
// 4. DiscoveredCalendar metadata-backed exposure round-trip.
// 5. Live-shape PRODID extraction + sync-collection probing against the
//    FakeCalDavServer multistat fixture.

#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "backendconfiguration.h"
#include "calendarcapabilities.h"
#include "caldavcapabilitydiscovery.h"
#include "discoveredcalendar.h"
#include "fakecaldavserver.h"

using namespace Kalburator::Sync;

class TstCalendarCapabilities : public QObject
{
    Q_OBJECT

private slots:
    // ---- static report pins ----

    void localBlob_report_is_full_fidelity();
    void calDav_default_matches_discovery_derivation();
    void orgBackend_pins_completion_anchor_and_no_unknowns();
    void googleCalendar_pins_exceptions_and_carrier_channel();
    void msGraphCalendar_pins_no_exception_writeback();
    void googleTasks_pins_no_extension_point();
    void msGraphTodo_pins_single_alarm_carrier_channel();

    // ---- JSON round-trip ----

    void json_roundtrip_preserves_every_field();
    void json_roundtrip_all_enum_values();

    // ---- derivation from discovery facts ----

    void capabilitiesFromDiscovery_maps_caldav_facts();
    void capabilitiesFromDiscovery_producer_fallback();
    void derivation_from_persisted_discovery_json_fixture();

    // ---- DiscoveredCalendar exposure ----

    void discoveredcalendar_metadata_exposure_roundtrip();

    // ---- discovery extensions (fake-server live shapes) ----

    void discovery_extracts_explicit_prodid_element();
    void discovery_sniffs_server_header_without_prodid();
    void discovery_detects_sync_collection_support();
    void discovery_defaults_sync_collection_false();
};

// ============================================================================
// 1. Static report pins
// ============================================================================

void TstCalendarCapabilities::localBlob_report_is_full_fidelity()
{
    // W7 passthrough table row LocalBlob: X-props/VALARM/VTIMEZONE verbatim,
    // bytes verbatim.
    const CalendarCapabilities c = CapabilityReports::localBlob();
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::Full);
    QVERIFY(c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(!c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::Full);
    QCOMPARE(c.producerId, QStringLiteral("local-blob"));
}

void TstCalendarCapabilities::calDav_default_matches_discovery_derivation()
{
    // The static CalDAV default must equal the discovery derivation with no
    // producer knowledge ("caldav" fallback producer id).
    const CalendarCapabilities c = CapabilityReports::calDav();
    QCOMPARE(c, capabilitiesFromDiscovery(PerCalendarCapabilities{}));
    QCOMPARE(c.producerId, QStringLiteral("caldav"));
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::Full);
    QVERIFY(c.recurrenceExceptions);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::Full);
}

void TstCalendarCapabilities::orgBackend_pins_completion_anchor_and_no_unknowns()
{
    // W7 table row Org: unknown X- props DROPPED; W4 decision: ++/.+
    // repeaters native; org has no VALARM home and no detached instances.
    const CalendarCapabilities c = CapabilityReports::orgBackend();
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::None);
    QVERIFY(!c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::None);
    QCOMPARE(c.producerId, QStringLiteral("orgmode"));
}

void TstCalendarCapabilities::googleCalendar_pins_exceptions_and_carrier_channel()
{
    // google-event loss profile: recurrenceId/recurrenceRange lossless ⇄
    // recurringEventId + originalStartTime (A4 live checkpoint both ways);
    // extendedProperties.private x-canon-* carriers live-Reversible
    // (wire notes §4.4); reminders carry display/email actions only.
    const CalendarCapabilities c = CapabilityReports::googleCalendar();
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::Display);
    QVERIFY(c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(!c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::XOnly);
    QCOMPARE(c.producerId, QStringLiteral("google-calendar"));
}

void TstCalendarCapabilities::msGraphCalendar_pins_no_exception_writeback()
{
    // ms-event loss profile: exceptions promote but "v1 writes flat events +
    // masters; exceptions expand read-only"; SVEP carriers stripped on
    // consumer creates (O61(e)); single reminderMinutesBeforeStart alarm.
    const CalendarCapabilities c = CapabilityReports::msGraphCalendar();
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::Display);
    QVERIFY(!c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(!c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::None);
    QCOMPARE(c.producerId, QStringLiteral("msgraph-calendar"));
}

void TstCalendarCapabilities::googleTasks_pins_no_extension_point()
{
    // google-task loss profile + O66(c): no extension point of any kind;
    // alarms/recurrence declared Dropped.
    const CalendarCapabilities c = CapabilityReports::googleTasks();
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::None);
    QVERIFY(!c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(!c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::None);
    QCOMPARE(c.producerId, QStringLiteral("google-tasks"));
}

void TstCalendarCapabilities::msGraphTodo_pins_single_alarm_carrier_channel()
{
    // ms-todotask loss profile + O66 correction: single reminder ⇄ alarms[0];
    // open-extension carrier survives via nav POST (live-Reversible).
    const CalendarCapabilities c = CapabilityReports::msGraphTodo();
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::Display);
    QVERIFY(!c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(!c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::XOnly);
    QCOMPARE(c.producerId, QStringLiteral("msgraph-todotask"));
}

// ============================================================================
// 2. JSON round-trip
// ============================================================================

void TstCalendarCapabilities::json_roundtrip_preserves_every_field()
{
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::Display;
    c.recurrenceExceptions = true;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = true;
    c.unknownPropertyPreservation =
        CalendarCapabilities::UnknownPropertyPreservation::XOnly;
    c.producerId = QStringLiteral("-//Radicale//NONSGML Radicale Server//EN");

    const CalendarCapabilities back =
        CalendarCapabilities::fromJson(
            QJsonDocument(QJsonDocument(c.toJson()).object()).object());
    QCOMPARE(back, c);

    // Empty producer id must survive as empty (not re-typed).
    c.producerId.clear();
    const CalendarCapabilities back2 =
        CalendarCapabilities::fromJson(c.toJson());
    QCOMPARE(back2, c);
    QVERIFY(back2.producerId.isEmpty());
}

void TstCalendarCapabilities::json_roundtrip_all_enum_values()
{
    using AS = CalendarCapabilities::AlarmSupport;
    using UP = CalendarCapabilities::UnknownPropertyPreservation;

    for (const auto alarms : { AS::None, AS::Display, AS::Full }) {
        for (const auto unknown :
             { UP::Full, UP::XOnly, UP::None }) {
            CalendarCapabilities c;
            c.alarms = alarms;
            c.recurrenceExceptions = true;
            c.thisAndFuture = true;
            c.completionAnchoredRepeat = false;
            c.unknownPropertyPreservation = unknown;
            c.producerId = QStringLiteral("p");
            const CalendarCapabilities back =
                CalendarCapabilities::fromJson(c.toJson());
            QCOMPARE(back, c);
        }
    }
}

// ============================================================================
// 3. Derivation from discovery facts
// ============================================================================

void TstCalendarCapabilities::capabilitiesFromDiscovery_maps_caldav_facts()
{
    PerCalendarCapabilities caps;
    caps.supportsVEvent = true;
    caps.supportsVTodo = false;
    caps.writable = false;
    caps.maxResourceSize = 1000000;
    caps.producerId = QStringLiteral("-//Nextcloud//NONSGML//EN");

    const CalendarCapabilities c =
        capabilitiesFromDiscovery(caps,
                                  QStringList{ QStringLiteral("VEVENT") });

    // Handoff §W8 pinned derivation.
    QCOMPARE(c.alarms, CalendarCapabilities::AlarmSupport::Full);
    QVERIFY(c.recurrenceExceptions);
    QVERIFY(!c.thisAndFuture);
    QVERIFY(!c.completionAnchoredRepeat);
    QCOMPARE(c.unknownPropertyPreservation,
             CalendarCapabilities::UnknownPropertyPreservation::Full);
    QCOMPARE(c.producerId, QStringLiteral("-//Nextcloud//NONSGML//EN"));

    // Content types do not modulate any field today — same answer without.
    QCOMPARE(capabilitiesFromDiscovery(caps), c);
}

void TstCalendarCapabilities::capabilitiesFromDiscovery_producer_fallback()
{
    // No producer discovered → stable "caldav" backend-type id.
    const CalendarCapabilities c =
        capabilitiesFromDiscovery(PerCalendarCapabilities{});
    QCOMPARE(c.producerId, QStringLiteral("caldav"));
}

void TstCalendarCapabilities::derivation_from_persisted_discovery_json_fixture()
{
    // .kalb-persisted DiscoveredCapabilities shape (the JSON
    // BackendConfiguration carries under "discoveredCapabilities").
    const QByteArray fixture = R"({
        "discoveredAt": "2026-08-26T10:00:00Z",
        "serverProduct": "Radicale",
        "supportsCalendarCreation": true,
        "calendars": {
            "Personal": {
                "supportsVEvent": true,
                "supportsVTodo": true,
                "writable": true,
                "displayName": "Personal",
                "producerId": "-//Radicale//NONSGML Radicale Server//EN",
                "supportsSyncCollection": true
            },
            "Tasks": {
                "supportsVEvent": false,
                "supportsVTodo": true,
                "writable": true,
                "displayName": "Tasks"
            }
        }
    })";

    const DiscoveredCapabilities disc = DiscoveredCapabilities::fromJson(
        QJsonDocument::fromJson(fixture).object());
    QVERIFY(disc.isValid());
    QCOMPARE(disc.perCalendarCapabilities.size(), 2);

    // Additive fields survived the persistence round-trip...
    const PerCalendarCapabilities &personal =
        disc.perCalendarCapabilities.value(QStringLiteral("Personal"));
    QCOMPARE(personal.producerId,
             QStringLiteral("-//Radicale//NONSGML Radicale Server//EN"));
    QVERIFY(personal.supportsSyncCollection);
    const PerCalendarCapabilities &tasks =
        disc.perCalendarCapabilities.value(QStringLiteral("Tasks"));
    QVERIFY(tasks.producerId.isEmpty());
    QVERIFY(!tasks.supportsSyncCollection);

    // ...and feed the derivation.
    const CalendarCapabilities personalCaps = capabilitiesFromDiscovery(personal);
    QCOMPARE(personalCaps.producerId,
             QStringLiteral("-//Radicale//NONSGML Radicale Server//EN"));
    QVERIFY(personalCaps.recurrenceExceptions);
    const CalendarCapabilities tasksCaps = capabilitiesFromDiscovery(tasks);
    QCOMPARE(tasksCaps.producerId, QStringLiteral("caldav"));
}

// ============================================================================
// 4. DiscoveredCalendar exposure
// ============================================================================

void TstCalendarCapabilities::discoveredcalendar_metadata_exposure_roundtrip()
{
    DiscoveredCalendar d;
    d.calendarId = QStringLiteral("work");
    d.backendId = QStringLiteral("primary");

    // Default-constructed before anything is set.
    const CalendarCapabilities defaultCaps = d.capabilities();
    QCOMPARE(defaultCaps.alarms, CalendarCapabilities::AlarmSupport::None);
    QVERIFY(!defaultCaps.recurrenceExceptions);
    QVERIFY(defaultCaps.producerId.isEmpty());

    // Set + read back through the metadata channel.
    const CalendarCapabilities caps = CapabilityReports::orgBackend();
    d.setCapabilities(caps);
    QCOMPARE(d.capabilities(), caps);
    QVERIFY(d.metadata.contains(QStringLiteral("capabilities")));

    // Existing fields/serialization untouched by the additive key.
    QCOMPARE(d.calendarId, QStringLiteral("work"));
    QCOMPARE(d.davUrl(), QString());
    d.setDavUrl(QStringLiteral("http://example.com/work/"));
    QCOMPARE(d.capabilities(), caps);   // coexists with other metadata
    QCOMPARE(d.davUrl(), QStringLiteral("http://example.com/work/"));
}

// ============================================================================
// 5. Discovery extensions against FakeCalDavServer
// ============================================================================

static bool runDiscovery(FakeCalDavServer &server,
                         DiscoveredCapabilities &out)
{
    CalDavCapabilityDiscovery disc(server.baseUrl(),
                                   QStringLiteral("testuser"),
                                   QStringLiteral("testpass"));
    QSignalSpy finishedSpy(&disc, &CalDavCapabilityDiscovery::finished);
    disc.start();
    if (!finishedSpy.wait(5000))
        return false;
    if (!finishedSpy.first().at(0).toBool())
        return false;
    out = disc.discoveredCapabilities();
    return true;
}

void TstCalendarCapabilities::discovery_extracts_explicit_prodid_element()
{
    FakeCalDavServer server;
    server.setServerProductHeader(QByteArrayLiteral(
        "-//Radicale//NONSGML Radicale Server//EN is also in the header"));
    server.setCalendarProducerId(
        QStringLiteral("/calendars/testuser/personal/"),
        QStringLiteral("-//Radicale//NONSGML Radicale Server//EN"));
    QVERIFY(server.startListening());

    DiscoveredCapabilities disc;
    QVERIFY(runDiscovery(server, disc));

    // Explicit <prodid> element wins over the header sniff.
    const PerCalendarCapabilities caps =
        disc.perCalendarCapabilities.value(QStringLiteral("Personal"));
    QCOMPARE(caps.producerId,
             QStringLiteral("-//Radicale//NONSGML Radicale Server//EN"));

    // And it persists through the .kalb JSON codec.
    const PerCalendarCapabilities back =
        PerCalendarCapabilities::fromJson(caps.toJson());
    QCOMPARE(back.producerId, caps.producerId);
}

void TstCalendarCapabilities::discovery_sniffs_server_header_without_prodid()
{
    FakeCalDavServer server;
    server.setServerProductHeader(QByteArrayLiteral("Radicale/3.5.0"));
    QVERIFY(server.startListening());

    DiscoveredCapabilities disc;
    QVERIFY(runDiscovery(server, disc));

    // No <prodid> element anywhere: the known-product sniff over body +
    // Server header identifies Radicale.
    const PerCalendarCapabilities caps =
        disc.perCalendarCapabilities.value(QStringLiteral("Personal"));
    QCOMPARE(caps.producerId, QStringLiteral("Radicale"));
}

void TstCalendarCapabilities::discovery_detects_sync_collection_support()
{
    FakeCalDavServer server;
    server.setSupportsSyncCollection(true);
    QVERIFY(server.startListening());

    DiscoveredCapabilities disc;
    QVERIFY(runDiscovery(server, disc));

    const PerCalendarCapabilities caps =
        disc.perCalendarCapabilities.value(QStringLiteral("Personal"));
    QVERIFY(caps.supportsSyncCollection);
}

void TstCalendarCapabilities::discovery_defaults_sync_collection_false()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    DiscoveredCapabilities disc;
    QVERIFY(runDiscovery(server, disc));

    const PerCalendarCapabilities caps =
        disc.perCalendarCapabilities.value(QStringLiteral("Personal"));
    QVERIFY(!caps.supportsSyncCollection);
    QVERIFY(caps.producerId.isEmpty());  // nothing to sniff from this fake
}

QTEST_GUILESS_MAIN(TstCalendarCapabilities)
#include "tst_calendar_capabilities.moc"
