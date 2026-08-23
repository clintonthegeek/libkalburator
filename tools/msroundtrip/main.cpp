// EEE Phase 7.B live-checkpoint runner (offline stage harness).
//
// Runs the ms-event ⇄ canon stages over real captured payloads so a human
// can execute the proposal-invariant-6 checkpoint without writing throwaway
// code:
//
//   ms-roundtrip promote <wire.json>
//       wire ms-event JSON → canon JSON on stdout
//   ms-roundtrip demote <canon.json>
//       canon JSON → ms-event wire JSON on stdout
//   ms-roundtrip roundtrip <wire.json> [--identity <file>]
//       G→C→G: prints the demoted wire′ to stdout (redirect to a file for
//       `graphcli create event`) and a per-path diff report to stderr.
//       Exit 0 iff every differing path is in the DECLARED-normalization set.
//   ms-roundtrip canon-compare <a.json> <b.json>
//       semantic compare of two promoted canon records modulo identity
//       fields; exit 0 = identical. For step 5 of the protocol (both server
//       copies promote identically).
//
// The declared-normalization path set mirrors
// docs/2026-08-23-ms-event-edge-loss-profile.md — extend BOTH that doc and
// kDeclaredNormalizations together, never silently.

#include <QCommandLineParser>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include "mseventcanonstages.h"
#include "canonenvelope.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Calendar::MsEventToCanonStage;
using Kalburator::Calendar::CanonToMsEventStage;

namespace {

QJsonObject loadObject(const QString &path, bool *ok)
{
    QFile f(path);
    *ok = f.open(QIODevice::ReadOnly);
    if (!*ok)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    // Accept both bare event objects and {value:[…]} pages (promote the
    // first item of a page for convenience).
    if (doc.isObject()) {
        const QJsonValue v =
            doc.object().value(QStringLiteral("value"));
        if (v.isArray() && v.toArray().size() == 1)
            return v.toArray().first().toObject();
        return doc.object();
    }
    *ok = false;
    return {};
}

/// Flatten a JSON object into path → serialized-value pairs.
void flatten(const QJsonValue &v, const QString &prefix,
             QMap<QString, QString> &out)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            flatten(it.value(),
                    prefix.isEmpty() ? it.key() : prefix + "." + it.key(), out);
        return;
    }
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        for (int i = 0; i < a.size(); ++i)
            flatten(a.at(i), QStringLiteral("%1[%2]").arg(prefix).arg(i), out);
        return;
    }
    out.insert(prefix, QJsonDocument(QJsonArray{v}).toJson(
                           QJsonDocument::Compact).mid(1).chopped(1));
}

/// Paths whose divergence is DECLARED by the loss profile. Wire-level paths,
/// i.e. keys as they appear in the demoted ms-event object.
QStringList declaredNormalizations()
{
    return {
        // timestamps: promote truncates to second-granular canon UTC ISO;
        // demote re-emits ".0000000Z" — sub-second precision is lost by design
        "^createdDateTime$",
        "^lastModifiedDateTime$",
        // start/end: offset-less wall time is re-derived from canon UTC +
        // zone; fraction form normalized to .0000000
        "^start\\.dateTime$", "^end\\.dateTime$",
        // recurrence: pattern/range re-derived through the converter
        // (interval defaults, WKST emission) — any RRULE-equivalent delta
        // is a converter-declared normalization
        "^recurrence\\.pattern\\.", "^recurrence\\.range\\.",
        // bodyPreview is server-derived and absent from authored bodies
        "^bodyPreview$",
        // @odata.etag / changeKey move when the server accepts a write
        "^@odata\\.etag$", "^changeKey$",
        // Graph `type` is reconstructed structurally on demote (declared
        // decision: canon never stores record topology)
        "^type$",
        // O57(d): year-1 .NET sentinels normalize ABSENT
        "^attendees\\[\\d+\\]\\.status\\.time$",
        "^responseStatus\\.",
        // partstat vocabulary: canon needsAction ≡ Graph "none" (absent)
        "^attendees\\[\\d+\\]\\.status\\.response$",
        // uid fallback chain: top-level uid ← iCalUId ← transport id (fires
        // only on $select-projection captures lacking both uid fields)
        "^uid$",
    };
}

bool matchesDeclared(const QString &path)
{
    static const QList<QRegularExpression> res = [] {
        QList<QRegularExpression> r;
        for (const QString &p : declaredNormalizations())
            r.append(QRegularExpression(p));
        return r;
    }();
    for (const auto &re : res)
        if (re.match(path).hasMatch())
            return true;
    return false;
}

int cmdRoundtrip(const QString &wirePath)
{
    bool ok = false;
    const QJsonObject wire = loadObject(wirePath, &ok);
    if (!ok || wire.isEmpty()) {
        QTextStream(stderr) << "cannot load wire JSON: " << wirePath << "\n";
        return 2;
    }

    MsEventToCanonStage promote;
    CanonToMsEventStage demote;

    const QByteArray canonBytes = promote.transform(
        QJsonDocument(wire).toJson(QJsonDocument::Compact));
    if (canonBytes.isEmpty()) {
        QTextStream(stderr) << "PROMOTE FAILED (empty canon)\n";
        return 3;
    }
    const QByteArray demotedBytes = demote.transform(canonBytes);
    if (demotedBytes.isEmpty()) {
        QTextStream(stderr) << "DEMOTE FAILED (empty wire')\n";
        return 3;
    }

    QTextStream(stdout) << QString::fromUtf8(demotedBytes) << "\n";

    // ---- diff report (stderr) ------------------------------------------------
    QMap<QString, QString> a, b;
    flatten(wire, QString(), a);
    flatten(parse(demotedBytes), QString(), b);

    QStringList undeclared;
    QSet<QString> allPaths;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        allPaths.insert(it.key());
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        allPaths.insert(it.key());

    int diffs = 0;
    QTextStream err(stderr);
    for (const QString &path : allPaths) {
        const QString va = a.value(path);
        const QString vb = b.value(path);
        if (va == vb)
            continue;
        ++diffs;
        const bool declared = matchesDeclared(path);
        if (!declared)
            undeclared << path;
        err << (declared ? "[declared]  " : "[UNDECLARED] ")
            << path << ": " << (va.isEmpty() ? QStringLiteral("<absent>") : va)
            << "  ->  " << (vb.isEmpty() ? QStringLiteral("<absent>") : vb)
            << "\n";
    }

    err << "---\nG→C→G diffs: " << diffs;
    if (!undeclared.isEmpty())
        err << "  (" << undeclared.size() << " UNDECLARED — BLOCKING)";
    err << "\n";
    return undeclared.isEmpty() ? 0 : 1;
}

int cmdCanonCompare(const QString &aPath, const QString &bPath)
{
    bool okA = false, okB = false;
    const QJsonObject a = loadObject(aPath, &okA);
    const QJsonObject b = loadObject(bPath, &okB);
    if (!okA || !okB) {
        QTextStream(stderr) << "cannot load inputs\n";
        return 2;
    }

    MsEventToCanonStage promote;
    const QByteArray ca = promote.transform(
        QJsonDocument(a).toJson(QJsonDocument::Compact));
    const QByteArray cb = promote.transform(
        QJsonDocument(b).toJson(QJsonDocument::Compact));

    QMap<QString, QString> fa, fb;
    flatten(parse(ca), QString(), fa);
    flatten(parse(cb), QString(), fb);

    // Identity fields: transport-local and per-copy; their canon carriers
    // differ between two distinct server copies BY DESIGN.
    static const QStringList identityPrefixes = {
        QStringLiteral("providerExtras.msgraph.id"),
        QStringLiteral("providerExtras.msgraph.changeKey"),
        QStringLiteral("providerExtras.msgraph.webLink"),
        QStringLiteral("providerExtras.msgraph.@odata.etag"),
        QStringLiteral("providerExtras.msgraph.createdDateTime"),
        QStringLiteral("providerExtras.msgraph.lastModifiedDateTime"),
    };
    auto isIdentity = [](const QString &path) {
        for (const QString &p : identityPrefixes)
            if (path == p || path.startsWith(p + QLatin1Char('.')))
                return true;
        return false;
    };

    QSet<QString> allPaths;
    for (auto it = fa.constBegin(); it != fa.constEnd(); ++it)
        allPaths.insert(it.key());
    for (auto it = fb.constBegin(); it != fb.constEnd(); ++it)
        allPaths.insert(it.key());

    int diffs = 0;
    QTextStream err(stderr);
    for (const QString &path : allPaths) {
        if (isIdentity(path))
            continue;
        if (fa.value(path) == fb.value(path))
            continue;
        ++diffs;
        err << path << ": " << fa.value(path) << "  !=  "
            << fb.value(path) << "\n";
    }
    err << "---\ncanon divergences (modulo identity): " << diffs << "\n";
    return diffs == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() < 2) {
        QTextStream(stderr)
            << "usage:\n"
            << "  ms-roundtrip promote <wire.json>\n"
            << "  ms-roundtrip demote <canon.json>\n"
            << "  ms-roundtrip roundtrip <wire.json>\n"
            << "  ms-roundtrip canon-compare <a.json> <b.json>\n";
        return 2;
    }
    const QString cmd = args.first();
    if (cmd == QLatin1String("roundtrip"))
        return cmdRoundtrip(args.at(1));
    if (cmd == QLatin1String("canon-compare") && args.size() >= 3)
        return cmdCanonCompare(args.at(1), args.at(2));

    MsEventToCanonStage p;
    CanonToMsEventStage d;
    bool ok = false;
    const QByteArray bytes = [&] {
        QFile f(args.at(1));
        ok = f.open(QIODevice::ReadOnly);
        return ok ? f.readAll() : QByteArray();
    }();
    if (!ok) {
        QTextStream(stderr) << "cannot read " << args.at(1) << "\n";
        return 2;
    }
    const QByteArray out = (cmd == QLatin1String("promote"))
                               ? p.transform(bytes)
                               : d.transform(bytes);
    if (out.isEmpty()) {
        QTextStream(stderr) << "transform returned empty bytes\n";
        return 3;
    }
    QTextStream(stdout) << QString::fromUtf8(out) << "\n";
    return 0;
}
