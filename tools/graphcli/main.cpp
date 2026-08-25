#include "labpaths.h"

#include <graphauthenticator.h>
#include <blockinghttp.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

static const char *kScopes =
    "offline_access User.Read Mail.Read Calendars.ReadWrite Contacts.ReadWrite"
    " Tasks.ReadWrite";

namespace {

using Kalburator::Graph::Tokens;
using Kalburator::Graph::TokenStore;
using Kalburator::Graph::DeviceCodeFlow;
using Kalburator::Graph::refreshTokens;
using Kalburator::Net::HttpResponse;
using Kalburator::Net::httpRequest;
using Kalburator::Net::urlEncodePathSegment;

struct Session {
    QString clientId;
    Tokens tokens;
};

QString graphBaseUrl()
{
    return qEnvironmentVariable("KALBURATOR_GRAPH_BASE_URL",
                                "https://graph.microsoft.com/v1.0");
}

bool acquireTokens(Session &session, bool forceLogin, const QString &graphDir)
{
    TokenStore store(graphDir + "/token-cache.json");

    if (!forceLogin) {
        Tokens t = store.load();
        if (t.hasLiveAccessToken()) {
            session.tokens = t;
            return true;
        }
        if (!t.refreshToken.isEmpty()) {
            Tokens refreshed = refreshTokens(session.clientId, kScopes, t);
            if (!refreshed.accessToken.isEmpty()) {
                store.save(refreshed);
                session.tokens = refreshed;
                return true;
            }
            QTextStream(stderr) << "Token refresh failed; falling back to device login.\n";
        }
    }

    DeviceCodeFlow flow(session.clientId, kScopes, forceLogin);
    Tokens fresh;
    if (!flow.runInteractive(fresh))
        return false;
    store.save(fresh);
    session.tokens = fresh;
    return true;
}

HttpResponse graphCall(const Session &session,
                       const QString &method,
                       const QString &pathOrAbsoluteUrl,
                       const QByteArray &body = {})
{
    QUrl url(pathOrAbsoluteUrl);
    if (url.isRelative())
        url = QUrl(graphBaseUrl() + pathOrAbsoluteUrl);

    QList<QPair<QByteArray, QByteArray>> headers = {
        {"Authorization", "Bearer " + session.tokens.accessToken.toUtf8()},
    };
    if (!body.isEmpty())
        headers.emplaceBack("Content-Type", "application/json");

    HttpResponse resp = httpRequest(url, method.toUtf8(), headers, body);
    if (resp.status == 401)
        QTextStream(stderr) << "Got 401 Unauthorized — run `graphcli login` to re-authenticate.\n";
    return resp;
}

int printJsonResponse(const HttpResponse &resp, const char *label)
{
    if (!resp.ok()) {
        printGraphError(label, resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument::fromJson(resp.body).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

QJsonArray paginate(Session &session, const QString &firstPath, bool &ok)
{
    ok = true;
    QJsonArray all;
    QString nextPath = firstPath;
    int pages = 0;

    while (!nextPath.isEmpty() && pages < 100) {
        const HttpResponse resp = graphCall(session, "GET", nextPath);
        if (!resp.ok()) {
            printGraphError("list request", resp.status, resp.body);
            ok = false;
            return {};
        }
        const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
        for (const QJsonValue &v : obj.value("value").toArray())
            all.append(v);
        nextPath = obj.value("@odata.nextLink").toString();
        ++pages;
    }
    return all;
}

int cmdListEvents(Session &session, const QStringList &args)
{
    QString path = "/me/calendar/events?$top=50";
    if (!args.isEmpty())
        path = "/me/calendars/" + urlEncodePathSegment(args.first()) + "/events?$top=50";

    bool ok = false;
    QJsonArray events = paginate(session, path, ok);
    if (!ok)
        return 1;

    QJsonObject wrapped;
    wrapped.insert("value", events);
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument(wrapped).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

QByteArray testEventJson()
{
    const QDateTime start(QDate::currentDate().addDays(1), QTime(10, 0), QTimeZone::UTC);
    const QDateTime end = start.addSecs(3600);

    QJsonObject evt;
    evt.insert("subject", QStringLiteral("GraphCLI test event — DELETE ME"));
    QJsonObject s;
    s.insert("dateTime", start.toString("yyyy-MM-ddTHH:mm:ss"));
    s.insert("timeZone", "UTC");
    evt.insert("start", s);
    QJsonObject e;
    e.insert("dateTime", end.toString("yyyy-MM-ddTHH:mm:ss"));
    e.insert("timeZone", "UTC");
    evt.insert("end", e);

    return QJsonDocument(evt).toJson(QJsonDocument::Compact);
}

QByteArray testContactJson()
{
    QJsonObject contact;
    contact.insert("givenName", "GraphCLI");
    contact.insert("surname", "Test");
    QJsonArray emails;
    QJsonObject email;
    email.insert("address", "graphcli-delete-me@example.com");
    email.insert("name", "GraphCLI Test");
    emails.append(email);
    contact.insert("emailAddresses", emails);

    return QJsonDocument(contact).toJson(QJsonDocument::Compact);
}

int cmdCreateTest(Session &session, bool isEvent)
{
    const QByteArray json = isEvent ? testEventJson() : testContactJson();
    const char *path = isEvent ? "/me/calendar/events" : "/me/contacts";
    const HttpResponse resp = graphCall(session, "POST", path, json);
    if (!resp.ok()) {
        printGraphError(isEvent ? "create event" : "create contact",
                        resp.status, resp.body);
        return 1;
    }
    const QJsonObject created = QJsonDocument::fromJson(resp.body).object();
    QTextStream(stdout) << "Created:\n  id = "
                        << created.value("id").toString() << "\n  changeKey = "
                        << created.value("changeKey").toString() << '\n';
    return 0;
}

int cmdDelete(Session &session, const QStringList &args)
{
    if (args.size() != 2
        || (args.first() != "event" && args.first() != "contact"
            && args.first() != "calendar")) {
        QTextStream(stderr) << "usage: graphcli delete <event|contact|calendar> <id>\n";
        return 2;
    }
    const QString kind = args.first() == "event" ? "events"
                       : args.first() == "contact" ? "contacts" : "calendars";
    const HttpResponse resp = graphCall(
        session, "DELETE", "/me/" + kind + "/" + urlEncodePathSegment(args.last()));
    if (!resp.ok()) {
        printGraphError("delete", resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << "Deleted.\n";
    return 0;
}

int cmdCapture(Session &session, const QStringList &args)
{
    if (args.isEmpty()) {
        QTextStream(stderr) << "usage: graphcli capture <api-path>\n"
                            << "example: graphcli capture /me/calendar/events?$top=10\n";
        return 2;
    }

    const HttpResponse resp = graphCall(session, "GET", args.first());
    if (!resp.ok()) {
        printGraphError("capture GET", resp.status, resp.body);
        return 1;
    }

    const QString dir = msgraphDir() + "/captured";
    QDir().mkpath(dir);
    QStringList slugParts;
    for (const QString &seg : args.first().split('/', Qt::SkipEmptyParts)) {
        QString clean;
        for (const QChar c : seg)
            clean.append(c.isLetterOrNumber() || QStringLiteral("._~-").contains(c)
                             ? c : QLatin1Char('_'));
        slugParts.append(clean.left(16));
    }
    QString slug = slugParts.join(QLatin1Char('-'));
    if (slug.size() > 90)
        slug = slug.right(90);
    const QString file = dir + '/'
        + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz")
        + '-' + slug + ".json";
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(stderr) << "Cannot write " << file << ": " << out.errorString() << '\n';
        return 1;
    }
    const QJsonDocument parsed = QJsonDocument::fromJson(resp.body);
    if (parsed.isNull() && !resp.body.trimmed().startsWith('{')
        && !resp.body.trimmed().startsWith('['))
        out.write(resp.body);
    else
        out.write(parsed.toJson(QJsonDocument::Indented));
    QTextStream(stdout) << "Captured -> " << file << '\n';
    return 0;
}

int cmdLogout(const QString &graphDir)
{
    TokenStore(graphDir + "/token-cache.json").clear();
    QTextStream(stdout) << "Token cache removed.\n";
    return 0;
}

QString kindCollectionPath(const QString &kind)
{
    if (kind == "event") return "/me/events";
    if (kind == "contact") return "/me/contacts";
    if (kind == "calendar") return "/me/calendars";
    return {};
}

QByteArray readJsonFile(const QString &path, bool &ok)
{
    QFile file(path);
    ok = file.open(QIODevice::ReadOnly);
    if (!ok) {
        QTextStream(stderr) << "Cannot read " << path << ": " << file.errorString() << '\n';
        return {};
    }
    const QByteArray raw = file.readAll();
    QJsonParseError parseError;
    QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QTextStream(stderr) << path << " is not valid JSON: " << parseError.errorString() << '\n';
        ok = false;
        return {};
    }
    return raw;
}

int cmdCreate(Session &session, const QStringList &args)
{
    if (args.size() != 2) {
        QTextStream(stderr) << "usage: graphcli create <event|contact|calendar> <json-file>\n";
        return 2;
    }
    const QString path = kindCollectionPath(args.first());
    if (path.isEmpty()) {
        QTextStream(stderr) << "Unknown kind: " << args.first() << '\n';
        return 2;
    }
    bool ok = false;
    const QByteArray body = readJsonFile(args.last(), ok);
    if (!ok) return 2;

    const HttpResponse resp = graphCall(session, "POST", path, body);
    if (!resp.ok()) {
        printGraphError("create", resp.status, resp.body);
        return 1;
    }
    const QJsonObject created = QJsonDocument::fromJson(resp.body).object();
    QTextStream(stdout) << "Created:\n  id = " << created.value("id").toString()
                        << "\n  subject/displayName = "
                        << created.value("subject").toString(
                               created.value("displayName").toString())
                        << "\n  changeKey = " << created.value("changeKey").toString() << '\n';
    return 0;
}

int cmdPatch(Session &session, const QStringList &args)
{
    if (args.size() != 3) {
        QTextStream(stderr) << "usage: graphcli patch <event|contact|calendar> <id> <json-file>\n";
        return 2;
    }
    const QString path = kindCollectionPath(args.at(0));
    if (path.isEmpty()) {
        QTextStream(stderr) << "Unknown kind: " << args.at(0) << '\n';
        return 2;
    }
    bool ok = false;
    const QByteArray body = readJsonFile(args.last(), ok);
    if (!ok) return 2;

    const HttpResponse resp = graphCall(
        session, "PATCH",
        path + "/" + urlEncodePathSegment(args.at(1)), body);
    if (!resp.ok()) {
        printGraphError("patch", resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << "Patched.\n"
                        << QString::fromUtf8(QJsonDocument::fromJson(resp.body)
                                                 .toJson(QJsonDocument::Indented))
                        << '\n';
    return 0;
}

int cmdInstances(Session &session, const QStringList &args)
{
    if (args.size() != 3) {
        QTextStream(stderr) << "usage: graphcli instances <eventId> <startDateTime> <endDateTime>\n"
                            << "example: graphcli instances <id> 2026-08-01T00:00:00Z 2026-09-01T00:00:00Z\n";
        return 2;
    }
    QUrlQuery query;
    query.addQueryItem("startDateTime", args.at(1));
    query.addQueryItem("endDateTime", args.at(2));
    const QString path = "/me/events/" + urlEncodePathSegment(args.first())
        + "/instances?" + query.toString(QUrl::FullyEncoded);

    bool ok = false;
    const QJsonArray instances = paginate(session, path, ok);
    if (!ok) return 1;

    QJsonObject wrapped;
    wrapped.insert("value", instances);
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument(wrapped).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

int cmdCalendarView(Session &session, const QStringList &args)
{
    if (args.size() != 2) {
        QTextStream(stderr) << "usage: graphcli calendarview <startDateTime> <endDateTime>\n"
                            << "example: graphcli calendarview 2026-08-01T00:00:00Z 2026-09-01T00:00:00Z\n";
        return 2;
    }
    QUrlQuery query;
    query.addQueryItem("startDateTime", args.first());
    query.addQueryItem("endDateTime", args.last());
    bool ok = false;
    const QJsonArray items = paginate(
        session, "/me/calendarview?" + query.toString(QUrl::FullyEncoded), ok);
    if (!ok) return 1;

    QJsonObject wrapped;
    wrapped.insert("value", items);
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument(wrapped).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

int cmdDelta(Session &session, const QString &graphDir, bool fresh)
{
    const QString linkFile = graphDir + "/delta-link.txt";

    QString url = graphBaseUrl() + "/me/calendar/events/delta";
    if (!fresh && QFile::exists(linkFile)) {
        QFile stored(linkFile);
        if (stored.open(QIODevice::ReadOnly)) {
            url = QString::fromUtf8(stored.readAll()).trimmed();
            QTextStream(stdout) << "Resuming from saved delta link.\n";
        }
    }

    QJsonArray all;
    QString deltaLink;
    int pages = 0;
    while (!url.isEmpty() && pages < 200) {
        const HttpResponse resp = graphCall(session, "GET", url);
        if (!resp.ok()) {
            printGraphError("delta", resp.status, resp.body);
            return 1;
        }
        const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
        for (const QJsonValue &v : obj.value("value").toArray())
            all.append(v);
        deltaLink = obj.value("@odata.deltaLink").toString();
        url = obj.value("@odata.nextLink").toString();
        if (!deltaLink.isEmpty())
            break;
        ++pages;
    }

    if (!deltaLink.isEmpty()) {
        QFile stored(linkFile);
        if (stored.open(QIODevice::WriteOnly | QIODevice::Truncate))
            stored.write(deltaLink.toUtf8());
    }

    QJsonObject wrapped;
    wrapped.insert("value", all);
    wrapped.insert("@odata.deltaLink", deltaLink);
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument(wrapped).toJson(QJsonDocument::Indented)) << '\n';
    QTextStream(stdout) << "(" << all.size() << " records; delta link "
                        << (deltaLink.isEmpty() ? "NOT saved" : "saved") << " to "
                        << linkFile << ")\n";
    return 0;
}

int cmdSweepClean(Session &session, const QString &tag = {})
{
    // tag empty → every "CORPUS:" subject; non-empty → subjects starting with
    // that exact run tag (e.g. "CORPUS:20260823T...-1234" from corpus-sweep.sh).
    const QString prefix = tag.isEmpty() ? QStringLiteral("CORPUS:") : tag;
    bool ok = false;
    const QJsonArray events = paginate(
        session, "/me/events?$top=50&$select=id,subject", ok);
    if (!ok) return 1;

    int deleted = 0;
    for (const QJsonValue &v : events) {
        const QJsonObject evt = v.toObject();
        if (!evt.value("subject").toString().startsWith(prefix))
            continue;
        const HttpResponse resp = graphCall(
            session, "DELETE",
            "/me/events/" + urlEncodePathSegment(evt.value("id").toString()));
        if (!resp.ok()) {
            printGraphError(QStringLiteral("delete CORPUS event"), resp.status, resp.body);
            continue;
        }
        ++deleted;
        QTextStream(stdout) << "deleted: " << evt.value("subject").toString() << '\n';
    }
    QTextStream(stdout) << deleted << " CORPUS event(s) deleted.\n";
    return 0;
}

int cmdRespond(Session &session, const QStringList &args)
{
    static const struct { QString cli; QString graph; } kResponses[] = {
        {"accept", "accept"}, {"tentative", "tentativelyAccept"}, {"decline", "decline"},
    };
    QString graphMethod;
    QString eventId;
    QString comment;
    QString proposeStart;
    QString proposeEnd;
    QStringList positional;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == "--propose-start" && i + 1 < args.size())
            proposeStart = args.at(++i);
        else if (a == "--propose-end" && i + 1 < args.size())
            proposeEnd = args.at(++i);
        else
            positional.append(a);
    }
    for (const auto &r : kResponses)
        if (!positional.isEmpty() && r.cli == positional.first())
            graphMethod = r.graph;
    if (graphMethod.isEmpty() || positional.size() < 2) {
        QTextStream(stderr) << "usage: graphcli respond <accept|tentative|decline> <eventId> [comment]\n"
                            << "       [--propose-start <dt> --propose-end <dt>]  (counter-offer; tentative only)\n"
                            << "       prefix the id with 'msg:' to act on an eventMessage (unread invite)\n";
        return 2;
    }
    eventId = positional.at(1);
    if (positional.size() > 2)
        comment = positional.at(2);

    QJsonObject body;
    body.insert("sendResponse", true);
    if (!comment.isEmpty())
        body.insert("comment", comment);
    if (!proposeStart.isEmpty() || !proposeEnd.isEmpty()) {
        if (proposeStart.isEmpty() || proposeEnd.isEmpty()) {
            QTextStream(stderr) << "--propose-start and --propose-end must be given together\n";
            return 2;
        }
        QJsonObject start;
        start.insert("dateTime", proposeStart);
        start.insert("timeZone", "UTC");
        QJsonObject end;
        end.insert("dateTime", proposeEnd);
        end.insert("timeZone", "UTC");
        body.insert("proposedNewStart", start);
        body.insert("proposedNewEnd", end);
    }

    const QString collection = eventId.startsWith("msg:")
        ? "/me/messages/" + urlEncodePathSegment(eventId.mid(4))
        : "/me/events/" + urlEncodePathSegment(eventId);
    const HttpResponse resp = graphCall(
        session, "POST", collection + "/" + graphMethod,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!resp.ok()) {
        printGraphError("respond", resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << "Responded (" << graphMethod
                        << (proposeStart.isEmpty() ? "" : " + counter-proposal")
                        << "); response mailed to organizer.\n";
    return 0;
}

int cmdPost(Session &session, const QStringList &args)
{
    if (args.size() != 2) {
        QTextStream(stderr) << "usage: graphcli post <api-path> <json-file>\n";
        return 2;
    }
    bool ok = false;
    const QByteArray body = readJsonFile(args.last(), ok);
    if (!ok) return 2;

    const HttpResponse resp = graphCall(session, "POST", args.first(), body);
    if (!resp.ok()) {
        printGraphError("post", resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << "Posted.\n"
                        << QString::fromUtf8(QJsonDocument::fromJson(resp.body)
                                                 .toJson(QJsonDocument::Indented))
                        << '\n';
    return 0;
}

void printUsage()
{
    QTextStream out(stdout);
    out << "usage: graphcli <command>\n\n"
        << "commands:\n"
        << "  login                 force a fresh device-code login\n"
        << "  logout                discard the cached tokens\n"
        << "  me                    GET /me\n"
        << "  calendars             list calendars\n"
        << "  events [calendarId]   list events (default calendar if no id)\n"
        << "  contacts              list contacts\n"
        << "  create-test-event     create 'DELETE ME' event in the default calendar\n"
        << "  create-test-contact   create 'DELETE ME' contact\n"
        << "  delete <kind> <id>    delete event|contact|calendar by id\n"
        << "  capture <api-path>    GET any v1.0 path and save JSON to msgraph/captured/\n"
        << "  create <kind> <file>  POST arbitrary JSON body (kind: event|contact|calendar)\n"
        << "  patch <kind> <id> <f> PATCH arbitrary JSON body onto an item\n"
        << "  instances <id> s e    expanded occurrences of a series in [start,end]\n"
        << "  calendarview s e      expanded calendar view (surfaces overrides)\n"
        << "  delta [--fresh]       delta-query walk; saves/resumes msgraph/delta-link.txt\n"
        << "  sweep-clean [tag]     delete every event whose subject starts 'CORPUS:'\n"
        << "                        (or, with tag, only one run's 'CORPUS:<tag>' subjects)\n"
        << "  respond <how> <id>    RSVP to a meeting: accept|tentative|decline [comment]\n"
        << "  post <api-path> <f>   raw POST escape hatch (actions, move, ...)\n"
        << "\nprofiles:\n"
        << "  --profile <name>      use msgraph-<name>/ for tokens (multi-account);\n"
        << "                        app registration still read from msgraph/\n"
        << "\nsweeper script (scenario matrix): tools/graphcli/corpus-sweep.sh\n"
        << "\nenvironment:\n"
        << "  KALBURATOR_MSGRAPH_DIR      where credentials/token-cache live\n"
        << "                              (default: msgraph/ found from cwd)\n"
        << "  KALBURATOR_GRAPH_CLIENT_ID  override the Application ID\n"
        << "  KALBURATOR_GRAPH_BASE_URL   override API base (mock server support),\n"
        << "                              default https://graph.microsoft.com/v1.0\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);

    // --profile <name> isolates tokens/delta-link in msgraph-<name>/ while
    // reading the app registration from the base msgraph dir. N accounts,
    // one client id, no re-login churn between invocations.
    QString profile;
    for (int i = 0; i < args.size() - 1; ++i) {
        if (args.at(i) == "--profile") {
            profile = args.at(i + 1);
            args.removeAt(i);
            args.removeAt(i);
            break;
        }
    }

    const QString baseDir = msgraphDir();
    const QString graphDir = profile.isEmpty()
        ? baseDir
        : baseDir + "-" + profile;
    if (!profile.isEmpty())
        QDir().mkpath(graphDir);

    if (args.isEmpty() || args.first() == "help" || args.first() == "--help") {
        printUsage();
        return 0;
    }

    const QString command = args.first();
    const QStringList rest = args.mid(1);

    Session session;
    session.clientId = readClientId(baseDir);
    if (session.clientId.isEmpty())
        return 2;

    if (!profile.isEmpty())
        QTextStream(stdout) << "[profile " << profile << " -> " << graphDir << "]\n";

    if (command == "logout")
        return cmdLogout(graphDir);

    const bool forceLogin = command == "login";
    if (!acquireTokens(session, forceLogin, graphDir))
        return 1;

    if (command == "login") {
        QTextStream(stdout) << "Signed in. Token cached in " << graphDir
                            << "/token-cache.json\n";
        return 0;
    }
    if (command == "me")
        return printJsonResponse(graphCall(session, "GET", "/me"), "GET /me");
    if (command == "calendars")
        return printJsonResponse(graphCall(session, "GET", "/me/calendars?$top=100"),
                                 "GET /me/calendars");
    if (command == "events")
        return cmdListEvents(session, rest);
    if (command == "contacts")
        return printJsonResponse(graphCall(session, "GET", "/me/contacts?$top=50"),
                                 "GET /me/contacts");
    if (command == "create-test-event")
        return cmdCreateTest(session, true);
    if (command == "create-test-contact")
        return cmdCreateTest(session, false);
    if (command == "delete")
        return cmdDelete(session, rest);
    if (command == "capture")
        return cmdCapture(session, rest);
    if (command == "create")
        return cmdCreate(session, rest);
    if (command == "patch")
        return cmdPatch(session, rest);
    if (command == "instances")
        return cmdInstances(session, rest);
    if (command == "calendarview")
        return cmdCalendarView(session, rest);
    if (command == "delta")
        return cmdDelta(session, graphDir, rest.contains("--fresh"));
    if (command == "sweep-clean")
        return cmdSweepClean(session, rest.isEmpty() ? QString() : rest.first());
    if (command == "respond")
        return cmdRespond(session, rest);
    if (command == "post")
        return cmdPost(session, rest);

    QTextStream(stderr) << "Unknown command: " << command << "\n\n";
    printUsage();
    return 2;
}
