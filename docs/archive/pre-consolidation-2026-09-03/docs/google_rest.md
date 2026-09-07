Yes. For what you're building, I would treat Google Workspace as a **real API integration**, not as a CalDAV/CardDAV endpoint with some Google-specific authentication bolted on. The useful stack is roughly:

**your Qt/C++ application → OAuth 2.0 → Google REST APIs → JSON resources**

with Calendar API and People API being the two important services initially.

There is one important wrinkle: **Google's official client-library situation for C++ is much less attractive than its Java/Python/Go ecosystem.** I would seriously consider using ordinary HTTPS + JSON from C++, with OAuth handled separately, rather than trying to build your application around an old Google C++ API client.

## 1. The primary technical documentation

### Google Calendar API

Start here:

[Google Calendar API documentation](https://developers.google.com/workspace/calendar/api/guides/overview?utm_source=chatgpt.com)

The Calendar API is REST/JSON and gives you considerably more than an `.ics` connection:

* calendars and calendar lists
* events
* recurring events
* attendees
* event instances
* reminders
* conference data
* ACLs
* colors
* free/busy information
* incremental synchronization
* push notifications

For your sync architecture, **incremental synchronization is particularly important**. Look at:

[Calendar API synchronization guide](https://developers.google.com/workspace/calendar/api/guides/sync?utm_source=chatgpt.com)

Google uses a `syncToken` mechanism. Conceptually, this fits very nicely into the kind of `SyncWorker`/`TransOperation` architecture you've been building:

```text
initial sync
    ↓
GET events.list
    ↓
save resources + nextSyncToken
    ↓
later:
GET events.list?syncToken=...
    ↓
apply additions/changes/deletions
    ↓
save new syncToken
```

That is much better than periodically downloading the entire calendar.

The Calendar event resource itself is worth studying closely:

[Calendar API Event resource](https://developers.google.com/workspace/calendar/api/v3/reference/events?utm_source=chatgpt.com)

In particular, don't think of a Google event as merely an iCalendar `VEVENT`. Google has its own resource identity and synchronization semantics.

---

## 2. Google People API — the contact side

This is the API you want instead of trying to make Google Contacts conform to vCard.

[Google People API documentation](https://developers.google.com/people?utm_source=chatgpt.com)

The People API gives you:

* contacts
* "Other contacts"
* profiles
* Google Workspace directory people
* organizations
* addresses
* phone numbers
* email addresses
* birthdays
* photos
* contact groups
* mutations to contacts

The important conceptual distinction is that a `Person` isn't necessarily a contact.

Google describes it as a **merged view** of information coming from contacts, profiles, and Workspace directory information. Only contact-derived people can be modified through the contact mutation APIs. ([Google for Developers][1])

The main endpoints you'll probably care about are:

```text
people.connections.list
people.get
people.createContact
people.updateContact
people.deleteContact
contactGroups.list
contactGroups.members.modify
```

The connections API also has exactly the kind of incremental synchronization facility you want:

[People API connections.list reference](https://developers.google.com/people/api/rest/v1/people.connections/list?utm_source=chatgpt.com)

It supports:

```text
requestSyncToken=true
        ↓
nextSyncToken
        ↓
later request with syncToken=...
```

and the response can be limited with `personFields`.

The documentation explicitly describes `syncToken` for obtaining changes since a previous synchronization. ([Google for Developers][2])

That means I would make your Google connector's internal model something like:

```text
GoogleCalendarBackend
    ├── Calendar discovery
    ├── Event synchronization
    └── Calendar metadata

GooglePeopleBackend
    ├── Contact discovery
    ├── Contact synchronization
    └── Contact-group synchronization
```

rather than trying to make the two APIs look identical.

---

# 3. OAuth 2.0 is the part you should understand first

For a Linux desktop application, you're looking at the **OAuth 2.0 installed/desktop application flow**.

Google's general OAuth documentation:

[Using OAuth 2.0 for Web Server Applications](https://developers.google.com/identity/protocols/oauth2?utm_source=chatgpt.com)

And the installed-app portion is particularly relevant:

[OAuth 2.0 for Desktop Apps](https://developers.google.com/identity/protocols/oauth2/native-app?utm_source=chatgpt.com)

The basic interaction is:

```text
Your application
      |
      | 1. construct authorization URL
      v
Google authorization endpoint
      |
      | user logs in / grants permission
      v
localhost redirect
      |
      | authorization code
      v
Your application
      |
      | 2. exchange code
      v
Google token endpoint
      |
      +---- access token
      |
      +---- refresh token
```

For a Linux Qt application, the **loopback redirect** is a very natural fit.

Your program can temporarily listen on something like:

```text
http://127.0.0.1:<random-port>/oauth2callback
```

open the user's browser, and wait for Google to redirect the browser back to it.

That means you don't need:

* an embedded browser
* a web server on the Internet
* a proprietary Google SDK
* a special Google desktop runtime

You just need an HTTPS-capable HTTP client, a local HTTP listener, a JSON implementation, and a browser-launch mechanism.

Qt already gives you most of that.

---

# 4. The scopes matter enormously

Don't start by asking Google for every possible scope.

For example, Calendar has:

```text
https://www.googleapis.com/auth/calendar
```

which gives very broad access.

Google also provides narrower scopes, such as:

```text
https://www.googleapis.com/auth/calendar.readonly
https://www.googleapis.com/auth/calendar.events
https://www.googleapis.com/auth/calendar.calendarlist
```

The complete scope catalogue is here:

[Google OAuth 2.0 scopes](https://developers.google.com/identity/protocols/oauth2/scopes?utm_source=chatgpt.com)

For People:

```text
https://www.googleapis.com/auth/contacts
```

is the obvious starting point if your application needs to synchronize contacts in both directions.

There is also:

```text
https://www.googleapis.com/auth/contacts.readonly
```

for read-only access. ([Google for Developers][3])

For a **bidirectional calendar/contact synchronization application**, you'll probably eventually need something along the lines of:

```text
calendar
contacts
```

plus possibly:

```text
userinfo.email
```

if you want a reliable way of determining which Google account was authenticated.

But I'd begin with the smallest set that lets you implement your first sync.

---

# 5. Google Cloud project / application registration

This is the practical part.

Go to:

[Google Cloud Console](https://console.cloud.google.com/?utm_source=chatgpt.com)

You aren't really registering an "app with a Google developer account" in the old sense. You create a **Google Cloud project**, and your OAuth credentials belong to that project.

### Step 1 — Create a project

In Google Cloud Console:

```text
Create Project
    ↓
Google Workspace Connector
```

You don't need to make this a complicated Cloud infrastructure project. The project primarily gives Google somewhere to associate:

* API enablement
* OAuth configuration
* credentials
* consent-screen configuration
* quota
* eventual verification

---

### Step 2 — Enable the APIs

Go to:

```text
APIs & Services
    ↓
Library
```

Enable:

```text
Google Calendar API
People API
```

You don't need to enable some enormous collection of Google Workspace APIs just because they exist.

---

### Step 3 — Configure the OAuth consent screen

Google's terminology/UI has changed several times, so don't be alarmed if current Cloud Console calls this something slightly different from older tutorials.

You're configuring the **OAuth consent screen / Google Auth Platform**.

For a normal application intended for arbitrary Google accounts, you'll generally be dealing with an **External** application.

For your own initial development, add yourself as a **test user**.

This is an important distinction:

```text
External + Testing
        |
        +-- you explicitly authorize test accounts
        +-- useful for development
        +-- OAuth restrictions apply
```

versus eventually:

```text
External + Production
        |
        +-- arbitrary users can authorize
        +-- Google may require verification for sensitive/restricted scopes
```

This is where Google's OAuth policies become important for a public application.

---

# 6. Create a Desktop OAuth client

Go to:

```text
APIs & Services
    ↓
Credentials
    ↓
Create Credentials
    ↓
OAuth client ID
    ↓
Desktop app
```

Google's own desktop OAuth flow is designed around this model.

You get something resembling:

```text
client_id:
123456789012-abcdefghijklmnop.apps.googleusercontent.com

client_secret:
GOCSPX-xxxxxxxxxxxxxxxx
```

and Google lets you download the credentials JSON.

**Do not commit this JSON into Git.**

The interesting thing is that for a native desktop application, the `client_secret` is not genuinely secret in the same way a web application's server-side secret is. The application is distributed to the user's machine, so you cannot fundamentally hide a credential embedded in it.

This is one reason Google's desktop-client OAuth model works differently from a web application's confidential-client model.

---

# 7. What I would actually implement in C++/Qt

I would **not** make the architecture:

```text
Qt
 ↓
Google C++ API library
 ↓
Calendar API
```

Instead I'd make:

```text
                    GoogleConnector
                          |
             +------------+------------+
             |                         |
       GoogleOAuth                GoogleApiClient
             |                         |
       Qt Network                  Qt Network
             |                         |
       QOAuth2AuthorizationCodeFlow  HTTPS/JSON
                                       |
                         +-------------+-------------+
                         |                           |
                   Calendar API                  People API
```

There is a very good reason.

Google has an official C++ API client repository:

[google-api-cpp-client on GitHub](https://github.com/google/google-api-cpp-client?utm_source=chatgpt.com)

and it does contain OAuth infrastructure. ([GitHub][4])

But I would regard it as **reference material rather than the foundation of a new Qt6 application**. It is old, comparatively awkward, and doesn't give you the same ecosystem experience as Google's Python/Java/Go libraries.

Google's API definitions themselves are available here:

[googleapis API definitions on GitHub](https://github.com/googleapis/googleapis?utm_source=chatgpt.com)

Those are actually extremely interesting for your purposes because Google describes its APIs using Protocol Buffers, from which REST/RPC interfaces and client artifacts can be generated. ([GitHub][5])

But your connector doesn't need gRPC.

For Calendar and People, I'd use:

```text
QNetworkAccessManager
QNetworkRequest
QNetworkReply
QJsonDocument
QJsonObject
QJsonArray
```

and implement the REST API yourself.

That's not as much work as it sounds.

---

# 8. The API is basically ordinary HTTP

For example, conceptually:

```http
GET https://www.googleapis.com/calendar/v3/users/me/calendarList
Authorization: Bearer ACCESS_TOKEN
```

and:

```http
GET https://people.googleapis.com/v1/people/me/connections
    ?personFields=names,emailAddresses,phoneNumbers,organizations
    &pageSize=100
Authorization: Bearer ACCESS_TOKEN
```

The responses are JSON.

So your Google connector can have a relatively clean abstraction:

```cpp
class GoogleApiClient
{
public:
    QNetworkReply *get(
        const QUrl &url,
        const QUrlQuery &query);

    QNetworkReply *post(
        const QUrl &url,
        const QJsonObject &body);

    QNetworkReply *patch(
        const QUrl &url,
        const QJsonObject &body);

private:
    QString accessToken;
};
```

Then:

```cpp
class GoogleCalendarClient
{
    GoogleApiClient *api;
};

class GooglePeopleClient
{
    GoogleApiClient *api;
};
```

This is actually a very nice match for the architecture you've already been developing.

---

# 9. Pay particular attention to resource identity

This is one place where I would **not** attempt to flatten Google into your `.ics`/`.vcf` representation.

For example, Google events have identifiers and synchronization metadata that you should preserve separately from your canonical calendar representation.

Likewise, People resources have:

```text
resourceName
etag
metadata
sources
memberships
```

etc.

I'd give every imported object an external identity such as:

```text
provider = google
collection = <Google calendar id>
remote_id = <Google event id>
etag = <Google etag>
```

and maintain your canonical object separately.

That allows:

```text
Google representation
        ↕
Google adapter
        ↕
canonical CalendarItem
        ↕
local .ics representation
```

rather than making Google itself dictate the internal data model.

---

# 10. Open-source implementations worth studying

There aren't as many good **C++ Google Calendar + People** implementations as I would like. But there are some useful ones.

### `google-api-cpp-client`

[google/google-api-cpp-client](https://github.com/google/google-api-cpp-client?utm_source=chatgpt.com)

Useful primarily for understanding how Google approached OAuth and API transport in C++. ([GitHub][4])

I wouldn't copy its overall architecture into a new Qt6 project.

---

### `gog` / gogcli

[gogcli on GitHub](https://github.com/openclaw/gogcli?utm_source=chatgpt.com)

This is actually one of the **most useful contemporary references** for understanding the Google side of things.

Its documentation walks through:

* Google Cloud project creation
* API enabling
* OAuth consent configuration
* desktop OAuth credentials
* Calendar
* People/Contacts
* credential storage
* authentication

It explicitly uses a user's own Google Cloud project and desktop OAuth credentials. ([GitHub][6])

Even though it's not C++, its OAuth/application-registration model is very relevant.

---

### Mach

[Mach on GitHub](https://github.com/bborn/mach?utm_source=chatgpt.com)

This is another interesting contemporary desktop Google client.

Its setup documentation is particularly useful because it deals with the uncomfortable real-world question of **what happens when you distribute a desktop application that uses Google OAuth**. It documents creating a Cloud project, enabling Calendar/People APIs, creating a Desktop OAuth client, configuring scopes, test users, and moving the application to production. ([GitHub][7])

---

### Google People API wrappers

For understanding the People API itself, this is a useful small implementation:

[gpeopleapiwrapper on GitHub](https://github.com/holtschn/gpeopleapiwrapper?utm_source=chatgpt.com)

Its authentication flow is essentially the same desktop OAuth pattern: Google Cloud project → People API → OAuth client ID → Desktop application → client JSON. ([GitHub][8])

---

# 11. One particularly important thing for your sync engine

I'd study Google's **incremental synchronization semantics before writing your connector abstraction**.

You've already got the right conceptual machinery from your CalDAV work, but Google's model is different enough that it deserves a first-class adapter.

For Calendar:

```text
calendarList
    |
    +-- calendar IDs
          |
          +-- events.list
                  |
                  +-- nextSyncToken
```

Then:

```text
events.list(syncToken=X)
       |
       +-- changed resources
       +-- deleted resources
       +-- nextSyncToken=Y
```

For People:

```text
people.connections.list
       |
       +-- contacts
       +-- nextSyncToken
```

then:

```text
people.connections.list(syncToken=X)
       |
       +-- changes
       +-- deleted resources
       +-- nextSyncToken=Y
```

This means your synchronization database should probably record something like:

```text
GoogleCollection
----------------
account_id
remote_id
kind
sync_token
last_sync

GoogleObject
------------
account_id
collection_id
remote_id
etag
local_id
deleted
last_seen
```

That's much more robust than putting a Google-specific ID into an `.ics` file and hoping the file itself can serve as the synchronization database.

---

# 12. Google authentication should be its own subsystem

I'd make this independent of Calendar and People:

```text
GoogleAccount
    |
    +-- account identity
    +-- OAuth client identity
    +-- access token
    +-- refresh token
    +-- expiry
    +-- granted scopes
```

Something like:

```cpp
class GoogleOAuthAccount
{
public:
    QString accountId() const;
    QString email() const;

    bool isAuthenticated() const;

    QFuture<void> authenticate();
    QFuture<void> refresh();

    QString accessToken() const;
};
```

Then the API clients don't care about OAuth:

```cpp
GoogleApiClient api(account);

GoogleCalendarClient calendar(api);
GooglePeopleClient people(api);
```

This will pay off enormously if you eventually add:

* multiple Google accounts
* multiple Workspace domains
* Google Drive
* Gmail
* Tasks
* Directory APIs

etc.

---

# 13. There is another authentication distinction you should know about

If this is a **user-facing desktop application**, OAuth authorization on behalf of the user is what you want.

Don't confuse it with:

**service accounts**

A service account is essentially an application identity rather than a user's identity. That's useful for server-to-server Google Cloud applications, and Workspace administrators can delegate authority to service accounts in certain circumstances.

But for:

> "User installs my Linux calendar/contact application and connects their Google account"

you want:

**OAuth 2.0 + user's consent + refresh token.**

The service-account model is not the right default.

---

# 14. Google Workspace versus ordinary Google accounts

This is another distinction worth designing for now.

The APIs can operate against ordinary Google accounts:

```text
user@gmail.com
```

and Workspace accounts:

```text
user@example.com
```

but Workspace adds things such as:

* domain directory
* domain contacts
* organizational profiles
* administrator policies
* possible admin approval
* domain-wide delegation

The People API specifically supports Workspace directory information when the relevant domain configuration and scope are available. ([Google for Developers][1])

So I would initially implement:

```text
Google consumer account
       +
Google Workspace user account
```

using the same user OAuth path.

Don't introduce domain-wide delegation until you actually need an administrator-level integration.

---

# 15. My recommended development order

I'd actually do this in this order:

### Phase 1 — OAuth smoke test

Before writing Calendar code:

```text
Create Cloud project
        ↓
Enable Calendar API
        ↓
Configure OAuth
        ↓
Create Desktop client
        ↓
Qt application
        ↓
open browser
        ↓
localhost callback
        ↓
authorization code
        ↓
access token
        ↓
refresh token
```

Get that working independently.

### Phase 2 — identity

Use the token to determine:

```text
Who am I?
```

and persist the refresh token securely.

### Phase 3 — Calendar discovery

Implement:

```text
calendarList.list
```

and get a list of the user's calendars.

### Phase 4 — Calendar synchronization

Implement:

```text
events.list
syncToken
nextSyncToken
deleted events
pagination
ETags
```

### Phase 5 — People

Then:

```text
people.connections.list
```

with a carefully chosen:

```text
personFields=
```

rather than requesting every field.

### Phase 6 — mutations

Only after reads and incremental sync are solid:

```text
create
update
delete
```

for contacts/events.

---

## The resources I'd bookmark

**Core API**

* [Calendar API](https://developers.google.com/workspace/calendar/api/guides/overview?utm_source=chatgpt.com)
* [Calendar API REST reference](https://developers.google.com/workspace/calendar/api/v3/reference?utm_source=chatgpt.com)
* [Calendar synchronization](https://developers.google.com/workspace/calendar/api/guides/sync?utm_source=chatgpt.com)
* [People API](https://developers.google.com/people?utm_source=chatgpt.com)
* [People API REST reference](https://developers.google.com/people/api/rest?utm_source=chatgpt.com)
* [People connections.list](https://developers.google.com/people/api/rest/v1/people.connections/list?utm_source=chatgpt.com)

**Authentication**

* [Google OAuth 2.0 documentation](https://developers.google.com/identity/protocols/oauth2?utm_source=chatgpt.com)
* [OAuth 2.0 for desktop/native applications](https://developers.google.com/identity/protocols/oauth2/native-app?utm_source=chatgpt.com)
* [OAuth scopes](https://developers.google.com/identity/protocols/oauth2/scopes?utm_source=chatgpt.com)

**Implementation/reference**

* [Google C++ API client](https://github.com/google/google-api-cpp-client?utm_source=chatgpt.com)
* [Google API definitions](https://github.com/googleapis/googleapis?utm_source=chatgpt.com)
* [gogcli](https://github.com/openclaw/gogcli?utm_source=chatgpt.com)
* [Mach](https://github.com/bborn/mach?utm_source=chatgpt.com)
* [gpeopleapiwrapper](https://github.com/holtschn/gpeopleapiwrapper?utm_source=chatgpt.com)

The two contemporary projects I'd study most closely are **gogcli for the Google/OAuth/application-registration side** and **Mach for the desktop-app/distribution side**. The old C++ client is useful as historical/reference material, but I wouldn't make it a dependency of your Qt application.

And given the architecture you've been developing for TimeBuster, I think there's a particularly clean way to fit this in: **Google should be another remote provider behind your existing synchronization machinery, with OAuth as an account/session service and Calendar/People as independent provider backends.** That would let your local `.ics`/calendar model remain useful without pretending that Google's resource model is actually iCalendar.

[1]: https://developers.google.com/people?authuser=993749347&utm_source=chatgpt.com "Introduction  |  People API  |  Google for Developers"
[2]: https://developers.google.com/resources/api-libraries/documentation/people/v1/php/latest/class-Google_Service_People_PeopleConnections_Resource.html?utm_source=chatgpt.com "Class Google_Service_People_PeopleConnections_Resource | Google People API"
[3]: https://developers.google.com/identity/protocols/oauth2/scopes?utm_source=chatgpt.com "OAuth 2.0 Scopes for Google APIs  |  Google for Developers"
[4]: https://github.com/google/google-api-cpp-client/blob/master/src/googleapis/client/auth/oauth2_authorization.h?utm_source=chatgpt.com "google-api-cpp-client/src/googleapis/client/auth/oauth2_authorization.h at master · google/google-api-cpp-client · GitHub"
[5]: https://github.com/googleapis/googleapis?utm_source=chatgpt.com "GitHub - googleapis/googleapis: Public interface definitions of Google APIs. · GitHub"
[6]: https://github.com/openclaw/gogcli/blob/main/docs/quickstart.md?utm_source=chatgpt.com "gogcli/docs/quickstart.md at main · openclaw/gogcli · GitHub"
[7]: https://github.com/bborn/mach?utm_source=chatgpt.com "GitHub - bborn/mach: A fast, keyboard-first Gmail and Google Calendar desktop client. Local-first, with a built-in agent. · GitHub"
[8]: https://github.com/holtschn/gpeopleapiwrapper?utm_source=chatgpt.com "GitHub - holtschn/gpeopleapiwrapper: A wrapper around the Google People API · GitHub"
