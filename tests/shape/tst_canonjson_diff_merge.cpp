#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "canonenvelope.h"
#include "canonjsondiffer.h"
#include "canonjsonmerger.h"
#include "calendarcanonproperties.h"
#include "contactscanonproperties.h"
#include "icalcanonstages.h"

using namespace Kalburator::Shape;

class TestCanonJsonDiffMerge : public QObject
{
    Q_OBJECT
private slots:
    void envelopeStampsCanonAndUid()
    {
        QJsonObject o;
        o.insert("summary", "hi");
        CanonEnvelope::stampEnvelope(o, "calendar", "evt-1");
        QCOMPARE(CanonEnvelope::uid(o), QString("evt-1"));
        QCOMPARE(o.value("_canon").toObject().value("domain").toString(), QString("calendar"));
        QCOMPARE(o.value("_canon").toObject().value("v").toInt(), 1);
        QCOMPARE(o.value("summary").toString(), QString("hi"));  // untouched
    }

    void parseSerializeRoundTripsUnknownKeys()
    {
        const QByteArray in = R"({"uid":"x","futureKey":{"a":1}})";
        QJsonObject o = CanonEnvelope::parse(in);
        QVERIFY(o.contains("futureKey"));  // unknown key retained
        QByteArray out = CanonEnvelope::serialize(o);
        QJsonObject o2 = CanonEnvelope::parse(out);
        QVERIFY(CanonEnvelope::valuesEqual(o.value("futureKey"), o2.value("futureKey")));
    }

    void valuesEqualIsKeyOrderIndependent()
    {
        QJsonObject a{{"x",1},{"y",2}};
        QJsonObject b{{"y",2},{"x",1}};
        QVERIFY(CanonEnvelope::valuesEqual(a, b));
    }

    void differMarksChangedPropertyOnly()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("summary")}, PropertyId{QStringLiteral("location")} });
        CanonicalRecord src; src.data = R"({"summary":"new","location":"home"})";
        CanonicalRecord base; base.data = R"({"summary":"old","location":"home"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("summary")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("location")}));
    }

    void differIgnoresProviderExtrasAndCanon()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src; src.data = R"({"summary":"x","providerExtras":{"x-ms":{"a":1}},"_canon":{"v":1}})";
        CanonicalRecord base; base.data = R"({"summary":"x","providerExtras":{"x-ms":{"a":2}},"_canon":{"v":1}})";
        QVERIFY(d.diff(src, base).isEmpty());   // providerExtras change does not register
        QVERIFY(d.equal(src, base));
    }

    void differTreatsCompositeAsWhole()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("attendees")} });
        CanonicalRecord src; src.data = R"({"attendees":[{"email":"a@x"},{"email":"b@x"}]})";
        CanonicalRecord base; base.data = R"({"attendees":[{"email":"a@x"}]})";
        QVERIFY(d.diff(src, base).contains(PropertyId{QStringLiteral("attendees")}));
    }

    // W4 — completion-anchored recurrence: an anchor advance (e.g. after a
    // completion event, PlanStan/org-io stages a later `completed` and
    // caller-derived `completionAnchor`) is an ordinary field change, never
    // a conflict — this falls out for free once completionAnchor is a
    // catalogued todo canon property (CanonJsonDiffer iterates catalogued
    // ids only; it has no separate "conflict" notion — reporting the key
    // as changed IS the non-conflict treatment the differ contributes).
    void differMarksCompletionAnchorAdvanceAsOrdinaryChange()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("completionAnchor")},
                            PropertyId{QStringLiteral("completed")} });
        CanonicalRecord src;
        src.data = R"({"completionAnchor":{"type":"restart","interval":1,"unit":"w"},)"
                   R"("completed":"2026-06-08T09:00:00Z"})";
        CanonicalRecord base;
        base.data = R"({"completionAnchor":{"type":"restart","interval":1,"unit":"w"},)"
                    R"("completed":"2026-06-01T09:00:00Z"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("completed")}));
        // completionAnchor's own JSON is byte-identical here (both sides
        // converge on the same derived {type,interval,unit}) — only
        // `completed` differs, exactly as the binding spec predicts: both
        // sides converge on the same derived value, so the advance shows up
        // as an ordinary `completed` change, not a conflict on the anchor
        // itself.
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("completionAnchor")}));
    }

    // completionAnchor variant of differMarksChangedPropertyOnly: a direct
    // change to the anchor itself (e.g. interval bumped by a later org
    // edit) is reported like any other catalogued property — the differ
    // applies no special conflict machinery to it.
    void differMarksCompletionAnchorContentChangeOnly()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("completionAnchor")},
                            PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;
        src.data = R"({"completionAnchor":{"type":"catchUp","interval":2,"unit":"d"},"summary":"x"})";
        CanonicalRecord base;
        base.data = R"({"completionAnchor":{"type":"catchUp","interval":1,"unit":"d"},"summary":"x"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("completionAnchor")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("summary")}));
    }

    // W3 — seriesSplitOf: a trivial non-conflict pin, same as
    // completionAnchor above. Falls out for free once the key is
    // catalogued (todoCanonPropertyIds()); the differ has no separate
    // "conflict" concept — reporting a changed key IS the whole mechanism.
    void differMarksSeriesSplitOfChangeOnly()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("seriesSplitOf")},
                            PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;
        src.data = R"({"seriesSplitOf":"weekly-series-1","summary":"x"})";
        CanonicalRecord base;
        base.data = R"({"summary":"x"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("seriesSplitOf")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("summary")}));
    }

    // O74 — providerExtrasDigest is an ordinary catalogued property as far
    // as the differ is concerned: once catalogued, a change to its value
    // (computed upstream from providerExtras content the differ itself
    // never sees) is reported like any other property change. This is the
    // whole O74 fix: providerExtras itself stays permanently invisible to
    // the differ (differIgnoresProviderExtrasAndCanon above still holds,
    // unmodified — it tests a different, narrower catalogue that never
    // includes providerExtrasDigest), but a change confined to it is no
    // longer silently swallowed once a domain catalogues the digest.
    void differMarksProviderExtrasDigestChangeOnly()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("providerExtrasDigest")},
                            PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;
        src.data = R"({"providerExtrasDigest":"abc123","summary":"x"})";
        CanonicalRecord base;
        base.data = R"({"providerExtrasDigest":"def456","summary":"x"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("providerExtrasDigest")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("summary")}));
    }

    // IP.5/O80 — the two REAL production catalogues this item modifies now
    // both catalogue providerExtrasDigest (calendarcanonproperties.cpp /
    // contactscanonproperties.cpp), so the differ each domain actually
    // builds (CanonJsonDiffer over calendarCanonPropertyIds() /
    // contactsCanonPropertyIds(), mirroring calendardomaindefinition.cpp /
    // contactsdomaindefinition.cpp's own construction) must dirty on an
    // extras-only change — the differMarksProviderExtrasDigestChangeOnly()
    // slot above proves the MECHANISM with a synthetic 2-id catalogue; this
    // proves the REAL catalogues actually carry the id.
    void calendarDifferDetectsProviderExtrasDigestChangeOnly()
    {
        CanonJsonDiffer d(Kalburator::Calendar::calendarCanonPropertyIds());
        CanonicalRecord src;
        src.data = R"({"providerExtrasDigest":"abc123","summary":"x"})";
        CanonicalRecord base;
        base.data = R"({"providerExtrasDigest":"def456","summary":"x"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("providerExtrasDigest")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("summary")}));
    }

    void contactsDifferDetectsProviderExtrasDigestChangeOnly()
    {
        CanonJsonDiffer d(Kalburator::Contacts::contactsCanonPropertyIds());
        CanonicalRecord src;
        src.data = R"({"providerExtrasDigest":"abc123","names":"x"})";
        CanonicalRecord base;
        base.data = R"({"providerExtrasDigest":"def456","names":"x"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("providerExtrasDigest")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("names")}));
    }

    void mergerTakesSourceWhenTargetUnchanged()
    {
        CanonJsonMerger m(QStringLiteral("calendar"), { PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;  src.data  = R"({"uid":"e","summary":"edited"})"; src.recordId = QStringLiteral("e");
        CanonicalRecord tgt;  tgt.data  = R"({"uid":"e","summary":"base"})";   tgt.recordId = QStringLiteral("e");
        CanonicalRecord base; base.data = R"({"uid":"e","summary":"base"})";   base.recordId = QStringLiteral("e");
        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("summary").toString(), QString("edited"));
    }

    void mergerTakesTargetWhenSourceUnchanged()
    {
        CanonJsonMerger m(QStringLiteral("calendar"), { PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;  src.data  = R"({"uid":"e","summary":"base"})";    src.recordId = QStringLiteral("e");
        CanonicalRecord tgt;  tgt.data  = R"({"uid":"e","summary":"edited"})";  tgt.recordId = QStringLiteral("e");
        CanonicalRecord base; base.data = R"({"uid":"e","summary":"base"})";    base.recordId = QStringLiteral("e");
        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("summary").toString(), QString("edited"));
    }

    void mergerConflictResolvesToSourceUnderDefaultPolicy()
    {
        CanonJsonMerger m(QStringLiteral("calendar"), { PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;  src.data  = R"({"uid":"e","summary":"srcEdit"})"; src.recordId = QStringLiteral("e");
        CanonicalRecord tgt;  tgt.data  = R"({"uid":"e","summary":"tgtEdit"})"; tgt.recordId = QStringLiteral("e");
        CanonicalRecord base; base.data = R"({"uid":"e","summary":"base"})";    base.recordId = QStringLiteral("e");
        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("summary").toString(), QString("srcEdit"));  // default → source wins
    }

    // IP.2 / O78 — the regression that was LIVE, pinned against the REAL
    // calendar catalogue (calendarCanonPropertyIds()), not a hand-listed
    // set: a hand-listed set would pass even with the catalogue drifted,
    // which is precisely how this bug survived. {calendar,canon} carries
    // VTODOs through the shared emitter (icalcanonstages.cpp:56), so a
    // source-side seriesSplitOf edit must survive a merge. Before IP.2 the
    // key was uncatalogued, so CanonJsonMerger's `QJsonObject out = t`
    // (canonjsonmerger.cpp:29) handed back the TARGET's value — here, no
    // value at all — silently, on every merge.
    void mergerKeepsCalendarVtodoSeriesSplitOfFromSource()
    {
        CanonJsonMerger m(QStringLiteral("calendar"),
                          Kalburator::Calendar::calendarCanonPropertyIds());
        QJsonObject so{{"summary", "task"}, {"seriesSplitOf", "old-master-uid"}};
        QJsonObject to{{"summary", "task"}};
        QJsonObject bo{{"summary", "task"}};
        for (QJsonObject* o : { &so, &to, &bo })
            CanonEnvelope::stampEnvelope(*o, QStringLiteral("calendar"),
                                         QStringLiteral("t-1"), QStringLiteral("vtodo"));
        CanonicalRecord src;  src.data  = CanonEnvelope::serialize(so); src.recordId  = QStringLiteral("t-1");
        CanonicalRecord tgt;  tgt.data  = CanonEnvelope::serialize(to); tgt.recordId  = QStringLiteral("t-1");
        CanonicalRecord base; base.data = CanonEnvelope::serialize(bo); base.recordId = QStringLiteral("t-1");

        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        const QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("seriesSplitOf").toString(), QString("old-master-uid"));
    }

    // IP.3 / O84 RESOLVED. CanonJsonMerger::merge() used to re-stamp the
    // envelope with the 3-arg stampEnvelope (canonjsonmerger.cpp), which
    // built a FRESH _canon object and therefore ERASED _canon.kind.
    // CanonToICalStage treats an absent kind as vevent
    // (icalcanonstages.cpp, back-compat), so a merged {calendar,canon}
    // VTODO or VJOURNAL demoted to a VEVENT. Fixed by threading
    // CanonEnvelope::kind(t) (falling back to kind(s)) into the 5-arg
    // stampEnvelope call. Both assertions genuinely pass now — no
    // QEXPECT_FAIL, no XPASS.
    void mergerPreservesIncidenceKind()
    {
        CanonJsonMerger m(QStringLiteral("calendar"),
                          Kalburator::Calendar::calendarCanonPropertyIds());
        QJsonObject so{{"summary", "edited"}};
        QJsonObject to{{"summary", "task"}};
        QJsonObject bo{{"summary", "task"}};
        for (QJsonObject* o : { &so, &to, &bo })
            CanonEnvelope::stampEnvelope(*o, QStringLiteral("calendar"),
                                         QStringLiteral("t-4"), QStringLiteral("vtodo"));
        CanonicalRecord src;  src.data  = CanonEnvelope::serialize(so); src.recordId  = QStringLiteral("t-4");
        CanonicalRecord tgt;  tgt.data  = CanonEnvelope::serialize(to); tgt.recordId  = QStringLiteral("t-4");
        CanonicalRecord base; base.data = CanonEnvelope::serialize(bo); base.recordId = QStringLiteral("t-4");

        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        const QJsonObject o = QJsonDocument::fromJson(out.data).object();

        // The consequence, not just the symptom: demote the merged record.
        const QByteArray ical = Kalburator::Calendar::CanonToICalStage().transform(out.data);
        QVERIFY2(ical.contains("BEGIN:VTODO"),
                 qPrintable(QStringLiteral("merged vtodo demoted as: %1")
                                .arg(QString::fromUtf8(ical.left(200)))));
        QCOMPARE(CanonEnvelope::kind(o), QString("vtodo"));
    }

    // IP.3 / O84 — the easy case with only ONE side carrying a kind: the
    // present side's kind must survive (this is the shape a genuinely new
    // record takes mid-merge — e.g. a first-sync target with no envelope
    // kind yet, merging against a source that already has one).
    void mergerPreservesIncidenceKindWhenOnlySourceHasOne()
    {
        CanonJsonMerger m(QStringLiteral("calendar"),
                          Kalburator::Calendar::calendarCanonPropertyIds());
        QJsonObject so{{"summary", "edited"}};
        QJsonObject to{{"summary", "task"}};
        QJsonObject bo{{"summary", "task"}};
        CanonEnvelope::stampEnvelope(so, QStringLiteral("calendar"),
                                     QStringLiteral("t-5"), QStringLiteral("vjournal"));
        CanonEnvelope::stampEnvelope(to, QStringLiteral("calendar"), QStringLiteral("t-5"));
        CanonEnvelope::stampEnvelope(bo, QStringLiteral("calendar"), QStringLiteral("t-5"));
        CanonicalRecord src;  src.data  = CanonEnvelope::serialize(so); src.recordId  = QStringLiteral("t-5");
        CanonicalRecord tgt;  tgt.data  = CanonEnvelope::serialize(to); tgt.recordId  = QStringLiteral("t-5");
        CanonicalRecord base; base.data = CanonEnvelope::serialize(bo); base.recordId = QStringLiteral("t-5");

        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        const QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(CanonEnvelope::kind(o), QString("vjournal"));
    }

    // IP.3 / O84 — the deliberate disagreement rule. Source and target
    // both carry a NON-EMPTY kind and it differs for the same uid: this is
    // an upstream identity conflict (same class O55 named and treated as
    // fail-loud), not an ordinary field merge. CanonJsonMerger::merge()'s
    // interface (RecordMerger::merge() returns CanonicalRecord
    // unconditionally, no error channel) makes a genuine abort-the-sync
    // fail-loud a RecordMerger/engine interface change, out of IP.3's
    // scope. The deliberate, documented (not silent) precedence rule
    // chosen instead: target's kind wins, consistent with this function's
    // existing target-primary bias (`out = t`). Pinned here so the rule
    // cannot silently drift to source-wins or an unannounced default.
    void mergerKindDisagreementKeepsTargetKindDeliberately()
    {
        CanonJsonMerger m(QStringLiteral("calendar"),
                          Kalburator::Calendar::calendarCanonPropertyIds());
        QJsonObject so{{"summary", "edited"}};
        QJsonObject to{{"summary", "task"}};
        QJsonObject bo{{"summary", "task"}};
        CanonEnvelope::stampEnvelope(so, QStringLiteral("calendar"),
                                     QStringLiteral("t-6"), QStringLiteral("vevent"));
        CanonEnvelope::stampEnvelope(to, QStringLiteral("calendar"),
                                     QStringLiteral("t-6"), QStringLiteral("vtodo"));
        CanonEnvelope::stampEnvelope(bo, QStringLiteral("calendar"),
                                     QStringLiteral("t-6"), QStringLiteral("vtodo"));
        CanonicalRecord src;  src.data  = CanonEnvelope::serialize(so); src.recordId  = QStringLiteral("t-6");
        CanonicalRecord tgt;  tgt.data  = CanonEnvelope::serialize(to); tgt.recordId  = QStringLiteral("t-6");
        CanonicalRecord base; base.data = CanonEnvelope::serialize(bo); base.recordId = QStringLiteral("t-6");

        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        const QJsonObject o = QJsonDocument::fromJson(out.data).object();
        // Deliberate, not silent: target's kind is kept even though the
        // AutoResolveStrategy is None (source-preferred for ordinary
        // fields) — kind disagreement is not an ordinary field.
        QCOMPARE(CanonEnvelope::kind(o), QString("vtodo"));
    }

    // IP.2 — same shape for the other two drifted keys, kept in one slot
    // since they share the mechanism (uncatalogued ⇒ target's value wins).
    // completionAnchor was the one live-but-narrow loss (an org-repeater
    // VTODO arriving over CalDAV); providerExtrasDigest was benign-now but
    // becomes load-bearing under IP.5.
    void mergerKeepsCalendarVtodoAnchorAndDigestFromSource()
    {
        CanonJsonMerger m(QStringLiteral("calendar"),
                          Kalburator::Calendar::calendarCanonPropertyIds());
        const QJsonObject srcAnchor{{"type", "restart"}, {"interval", 1}, {"unit", "w"}};
        const QJsonObject tgtAnchor{{"type", "catchUp"}, {"interval", 2}, {"unit", "d"}};
        QJsonObject so{{"summary", "task"}, {"completionAnchor", srcAnchor},
                       {"providerExtrasDigest", "srcdigest"}};
        QJsonObject to{{"summary", "task"}, {"completionAnchor", tgtAnchor},
                       {"providerExtrasDigest", "basedigest"}};
        QJsonObject bo{{"summary", "task"}, {"completionAnchor", tgtAnchor},
                       {"providerExtrasDigest", "basedigest"}};
        for (QJsonObject* o : { &so, &to, &bo })
            CanonEnvelope::stampEnvelope(*o, QStringLiteral("calendar"),
                                         QStringLiteral("t-2"), QStringLiteral("vtodo"));
        CanonicalRecord src;  src.data  = CanonEnvelope::serialize(so); src.recordId  = QStringLiteral("t-2");
        CanonicalRecord tgt;  tgt.data  = CanonEnvelope::serialize(to); tgt.recordId  = QStringLiteral("t-2");
        CanonicalRecord base; base.data = CanonEnvelope::serialize(bo); base.recordId = QStringLiteral("t-2");

        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        const QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QVERIFY(CanonEnvelope::valuesEqual(o.value("completionAnchor"), srcAnchor));
        QCOMPARE(o.value("providerExtrasDigest").toString(), QString("srcdigest"));
    }

    // IP.2 — the differ half of the same regression, against the real
    // catalogue: an edit confined to any of the three keys must dirty a
    // {calendar,canon} vtodo record. Complements the narrow hand-catalogue
    // differ slots above (which pin the mechanism, not the calendar
    // catalogue's contents).
    void differSeesCalendarVtodoDriftedKeys()
    {
        CanonJsonDiffer d(Kalburator::Calendar::calendarCanonPropertyIds());
        for (const QString& key : { QStringLiteral("seriesSplitOf"),
                                    QStringLiteral("completionAnchor"),
                                    QStringLiteral("providerExtrasDigest") }) {
            QJsonObject so{{"summary", "task"}};
            QJsonObject bo{{"summary", "task"}};
            so.insert(key, key == QStringLiteral("completionAnchor")
                               ? QJsonValue(QJsonObject{{"type", "restart"}, {"interval", 1}, {"unit", "w"}})
                               : QJsonValue(QStringLiteral("changed")));
            CanonEnvelope::stampEnvelope(so, QStringLiteral("calendar"),
                                         QStringLiteral("t-3"), QStringLiteral("vtodo"));
            CanonEnvelope::stampEnvelope(bo, QStringLiteral("calendar"),
                                         QStringLiteral("t-3"), QStringLiteral("vtodo"));
            CanonicalRecord src;  src.data  = CanonEnvelope::serialize(so);
            CanonicalRecord base; base.data = CanonEnvelope::serialize(bo);
            QVERIFY2(d.diff(src, base).contains(PropertyId{key}),
                     qPrintable(QStringLiteral("calendar catalogue must make %1 differ-visible").arg(key)));
        }
    }

    void mergerKeepsProviderExtrasFromChosenOrigin()
    {
        CanonJsonMerger m(QStringLiteral("calendar"), { PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src;  src.data  = R"({"uid":"e","summary":"edited","providerExtras":{"x":1}})"; src.recordId=QStringLiteral("e");
        CanonicalRecord tgt;  tgt.data  = R"({"uid":"e","summary":"base","providerExtras":{"x":2}})";   tgt.recordId=QStringLiteral("e");
        CanonicalRecord base; base.data = R"({"uid":"e","summary":"base","providerExtras":{"x":2}})";   base.recordId=QStringLiteral("e");
        const CanonicalRecord out = m.merge(src, tgt, base, Kalburator::Shape::AutoResolveStrategy::None);
        QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("providerExtras").toObject().value("x").toInt(), 1); // followed source (the changed origin)
    }
};

QTEST_MAIN(TestCanonJsonDiffMerge)
#include "tst_canonjson_diff_merge.moc"
