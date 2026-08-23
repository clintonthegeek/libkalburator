#include "googleauth.h"
#include "googleclient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

static const char *kScopes =
    "https://www.googleapis.com/auth/calendar.events "
    "https://www.googleapis.com/auth/userinfo.email";

namespace {

struct Session {
    ClientCredentials creds;
    Tokens tokens;
};

QString calendarBaseUrl()
{
    return qEnvironmentVariable("KALBURATOR_GOOGLE_BASE_URL",
                                "https://www.googleapis.com/calendar/v3");
}

bool acquireTokens(Session &session, bool forceLogin, const QString &googleDir)
{
    TokenStore store(googleDir + "/token-cache.json");

    if (!forceLogin) {
        Tokens t = store.load();
        if (t.hasLiveAccessToken()) {
            session.tokens = t;
            return true;
        }
        if (!t.refreshToken.isEmpty()) {
            Tokens refreshed = refreshTokens(session.creds, t);
            if (!refreshed.accessToken.isEmpty()) {
                store.save(refreshed);
                session.tokens = refreshed;
                return true;
            }
            QTextStream(stderr) << "Token refresh failed; falling back to browser login.\n";
        }
    }

    LoopbackCodeFlow flow(session.creds, QString::fromUtf8(kScopes));
    Tokens fresh;
    if (!flow.runInteractive(fresh))
        return false;
    store.save(fresh);
    session.tokens = fresh;
    return true;
}

HttpResponse googleCall(const Session &session,
                        const QString &method,
                        const QString &pathOrAbsoluteUrl,
                        const QByteArray &body = {})
{
    QUrl url(pathOrAbsoluteUrl);
    if (url.isRelative())
        url = QUrl(calendarBaseUrl() + pathOrAbsoluteUrl);

    QList<QPair<QByteArray, QByteArray>> headers = {
        {"Authorization", "Bearer " + session.tokens.accessToken.toUtf8()},
    };
    if (!body.isEmpty())
        headers.emplace_back("Content-Type", "application/json");

    HttpResponse resp = httpRequest(url, method.toUtf8(), headers, body);
    if (resp.status == 401)
        QTextStream(stderr) << "Got 401 Unauthorized — run `googlecli login` to re-authenticate.\n";
    return resp;
}

int printJsonResponse(const HttpResponse &resp, const char *label)
{
    if (!resp.ok()) {
        printGoogleError(label, resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument::fromJson(resp.body).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

/// Google list responses page via nextPageToken + `items` (not Graph's
/// @odata.nextLink + `value`).
QJsonArray paginate(Session &session, const QString &firstPath, bool &ok)
{
    ok = true;
    QJsonArray all;
    QString nextPath = firstPath;
    int pages = 0;

    while (!nextPath.isEmpty() && pages < 100) {
        const HttpResponse resp = googleCall(session, "GET", nextPath);
        if (!resp.ok()) {
            printGoogleError("list request", resp.status, resp.body);
            ok = false;
            return {};
        }
        const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
        for (const QJsonValue &v : obj.value("items").toArray())
            all.append(v);
        const QString token = obj.value("nextPageToken").toString();
        if (token.isEmpty()) {
            nextPath.clear();
        } else {
            // Re-issue the first path with pageToken appended/replaced.
            QUrl url(nextPath);
            if (url.isRelative())
                url = QUrl(calendarBaseUrl() + nextPath);
            QUrlQuery query(url.query());
            query.removeQueryItem("pageToken");
            query.addQueryItem("pageToken", token);
            url.setQuery(query);
            nextPath = url.toString();
        }
        ++pages;
    }
    return all;
}

int cmdMe(Session &session)
{
    return printJsonResponse(googleCall(session, "GET",
        "https://www.googleapis.com/oauth2/v3/userinfo"), "userinfo");
}

int cmdCalendars(Session &session)
{
    bool ok = false;
    QJsonArray calendars = paginate(session, "/users/me/calendarList?maxResults=100", ok);
    if (!ok)
        return 1;
    QJsonObject wrapped;
    wrapped.insert("items", calendars);
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument(wrapped).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

int cmdEvents(Session &session, const QStringList &args)
{
    QString calendarId = QStringLiteral("primary");
    if (!args.isEmpty())
        calendarId = args.first();
    QString extra;
    if (args.size() > 1)
        extra = args.at(1);   // raw query extras, e.g. "timeMin=...&maxResults=10"

    QString path = "/calendars/" + urlEncodePathSegment(calendarId) + "/events?maxResults=50";
    if (!extra.isEmpty())
        path += "&" + extra;

    bool ok = false;
    QJsonArray events = paginate(session, path, ok);
    if (!ok)
        return 1;
    QJsonObject wrapped;
    wrapped.insert("items", events);
    QTextStream(stdout) << QString::fromUtf8(
        QJsonDocument(wrapped).toJson(QJsonDocument::Indented)) << '\n';
    return 0;
}

int cmdCapture(Session &session, const QStringList &args)
{
    if (args.isEmpty()) {
        QTextStream(stderr) << "usage: googlecli capture <api-path-or-url>\n"
                            << "example: googlecli capture /calendars/primary/events?maxResults=10\n";
        return 2;
    }

    const HttpResponse resp = googleCall(session, "GET", args.first());
    if (!resp.ok()) {
        printGoogleError("capture GET", resp.status, resp.body);
        return 1;
    }

    const QString dir = googledir() + "/captured";
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

int cmdLogout(const QString &googleDir)
{
    TokenStore(googleDir + "/token-cache.json").clear();
    QTextStream(stdout) << "Token cache removed.\n";
    return 0;
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

QString eventCollectionPath(const QStringList &args)
{
    QString calendarId = QStringLiteral("primary");
    if (!args.isEmpty())
        calendarId = args.first();
    return "/calendars/" + urlEncodePathSegment(calendarId) + "/events";
}

int cmdCreate(Session &session, const QStringList &args)
{
    if (args.size() != 2) {
        QTextStream(stderr) << "usage: googlecli create event [calendarId] <json-file>\n"
                            << "       (calendarId defaults to primary; pass it as the\n"
                            << "        second-to-last argument)\n";
        return 2;
    }
    bool ok = false;
    const QByteArray body = readJsonFile(args.last(), ok);
    if (!ok) return 2;

    const HttpResponse resp = googleCall(session, "POST",
                                         eventCollectionPath(args.mid(0, args.size() - 1)), body);
    if (!resp.ok()) {
        printGoogleError("create event", resp.status, resp.body);
        return 1;
    }
    const QJsonObject created = QJsonDocument::fromJson(resp.body).object();
    QTextStream(stdout) << "Created:\n  id = " << created.value("id").toString()
                        << "\n  summary = " << created.value("summary").toString()
                        << "\n  iCalUID = " << created.value("iCalUID").toString()
                        << "\n  etag = " << created.value("etag").toString() << '\n';
    return 0;
}

int cmdPatch(Session &session, const QStringList &args)
{
    if (args.size() != 3) {
        QTextStream(stderr) << "usage: googlecli patch event [calendarId] <id> <json-file>\n";
        return 2;
    }
    bool ok = false;
    const QByteArray body = readJsonFile(args.last(), ok);
    if (!ok) return 2;

    const QString path = eventCollectionPath(args.mid(0, args.size() - 2))
        + "/" + urlEncodePathSegment(args.at(args.size() - 2));
    const HttpResponse resp = googleCall(session, "PATCH", path, body);
    if (!resp.ok()) {
        printGoogleError("patch event", resp.status, resp.body);
        return 1;
    }
    const QJsonObject patched = QJsonDocument::fromJson(resp.body).object();
    QTextStream(stdout) << "Patched:\n  id = " << patched.value("id").toString()
                        << "\n  etag = " << patched.value("etag").toString() << '\n';
    return 0;
}

int cmdDelete(Session &session, const QStringList &args)
{
    if (args.size() != 2) {
        QTextStream(stderr) << "usage: googlecli delete event [calendarId] <id>\n";
        return 2;
    }
    const QString path = eventCollectionPath(args.mid(0, args.size() - 1))
        + "/" + urlEncodePathSegment(args.last());
    const HttpResponse resp = googleCall(session, "DELETE", path);
    if (!resp.ok()) {
        printGoogleError("delete event", resp.status, resp.body);
        return 1;
    }
    QTextStream(stdout) << "Deleted.\n";
    return 0;
}

int cmdSweepClean(Session &session, const QStringList &args)
{
    // tag empty → every "CORPUS:" subject; non-empty → one run's tag
    // (mirrors graphcli / FINDINGS O57 tooling).
    const QString prefix = args.isEmpty() ? QStringLiteral("CORPUS:") : args.first();
    bool ok = false;
    const QJsonArray events = paginate(session,
        "/calendars/primary/events?maxResults=250&singleEvents=false", ok);
    if (!ok) return 1;

    int deleted = 0;
    for (const QJsonValue &v : events) {
        const QJsonObject evt = v.toObject();
        if (!evt.value("summary").toString().startsWith(prefix))
            continue;
        const HttpResponse resp = googleCall(session, "DELETE",
            "/calendars/primary/events/" + urlEncodePathSegment(evt.value("id").toString()));
        if (!resp.ok()) {
            printGoogleError(QStringLiteral("delete CORPUS event"), resp.status, resp.body);
            continue;
        }
        ++deleted;
        QTextStream(stdout) << "deleted: " << evt.value("summary").toString() << '\n';
    }
    QTextStream(stdout) << deleted << " CORPUS event(s) deleted.\n";
    return 0;
}

void printUsage()
{
    QTextStream(stdout)
        << "googlecli — Google Calendar protocol lab (EEE Phase 0 corpus tool)\n\n"
        << "usage: googlecli <command> [args]\n\n"
        << "  auth                  run the loopback OAuth flow now\n"
        << "  logout                forget tokens\n"
        << "  me                    who am I (userinfo)\n"
        << "  calendars             list calendarList\n"
        << "  events [cal] [query]  list events (default primary; query = raw extras)\n"
        << "  capture <path|url>    GET and save pretty JSON into google/captured/\n"
        << "  create event [cal] f  create from JSON file\n"
        << "  patch event [cal] id f\n"
        << "  delete event [cal] id\n"
        << "  sweep-clean [tag]     delete primary-calendar events whose subject starts 'CORPUS:'\n"
        << "\nenvironment: KALBURATOR_GOOGLE_DIR, KALBURATOR_GOOGLE_CLIENT_ID/SECRET,\n"
        << "             KALBURATOR_GOOGLE_BASE_URL (mock server injection)\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        printUsage();
        return 2;
    }
    const QString command = args.first();
    const QStringList rest = args.mid(1);

    const QString googleDir = googledir();
    if (command == "logout")
        return cmdLogout(googleDir);

    Session session;
    session.creds = readClientCredentials(googleDir);
    if (!session.creds.valid())
        return 2;
    if (command == "help" || command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }

    if (!acquireTokens(session, command == "auth", googleDir))
        return 1;

    if (command == "auth") {
        QTextStream(stdout) << "Authorized. Token cache written to "
                            << googleDir << "/token-cache.json\n";
        return 0;
    }
    if (command == "me")
        return cmdMe(session);
    if (command == "calendars")
        return cmdCalendars(session);
    if (command == "events")
        return cmdEvents(session, rest);
    if (command == "capture")
        return cmdCapture(session, rest);
    if (command == "create")
        return cmdCreate(session, rest);
    if (command == "patch")
        return cmdPatch(session, rest);
    if (command == "delete")
        return cmdDelete(session, rest);
    if (command == "sweep-clean")
        return cmdSweepClean(session, rest);

    QTextStream(stderr) << "Unknown command: " << command << "\n\n";
    printUsage();
    return 2;
}
