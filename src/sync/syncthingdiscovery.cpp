#include "syncthingdiscovery.h"

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QDebug>

namespace Kalburator::Sync {

SyncthingDiscovery::Config SyncthingDiscovery::discoverConfig()
{
    return discoverFromPaths(standardConfigPaths());
}

SyncthingDiscovery::Config SyncthingDiscovery::discoverFromPath(const QString &configPath)
{
    Config result;

    QFile file(configPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    QXmlStreamReader xml(&file);

    QString address;
    QString apiKey;
    bool tls = false;
    bool inGui = false;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QStringLiteral("gui")) {
                inGui = true;
                tls = xml.attributes().value(QStringLiteral("tls")) == QStringLiteral("true");
            } else if (inGui && xml.name() == QStringLiteral("address")) {
                address = xml.readElementText().trimmed();
            } else if (inGui && xml.name() == QStringLiteral("apikey")) {
                apiKey = xml.readElementText().trimmed();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QStringLiteral("gui")) {
                inGui = false;
            }
        }
    }

    if (xml.hasError()) {
        qDebug() << "SyncthingDiscovery: XML parse error in" << configPath
                 << ":" << xml.errorString();
        return result;
    }

    if (address.isEmpty() || apiKey.isEmpty()) {
        return result;
    }

    // Build URL
    QString scheme = tls ? QStringLiteral("https") : QStringLiteral("http");
    result.url = QUrl(QStringLiteral("%1://%2").arg(scheme, address));
    result.apiKey = apiKey;
    result.found = true;

    qDebug() << "SyncthingDiscovery: Found config at" << configPath
             << "url:" << result.url.toString();

    return result;
}

SyncthingDiscovery::Config SyncthingDiscovery::discoverFromPaths(const QStringList &paths)
{
    for (const QString &path : paths) {
        Config config = discoverFromPath(path);
        if (config.found) {
            return config;
        }
    }
    return Config{};
}

QStringList SyncthingDiscovery::standardConfigPaths()
{
    QStringList paths;

    // 1. $STCONFDIR/config.xml
    QString stConfDir = qEnvironmentVariable("STCONFDIR");
    if (!stConfDir.isEmpty()) {
        paths << stConfDir + QStringLiteral("/config.xml");
    }

    // 2. $STHOMEDIR/config.xml
    QString stHomeDir = qEnvironmentVariable("STHOMEDIR");
    if (!stHomeDir.isEmpty()) {
        paths << stHomeDir + QStringLiteral("/config.xml");
    }

    QString home = QDir::homePath();

    // 3. $XDG_STATE_HOME/syncthing/config.xml
    QString xdgStateHome = qEnvironmentVariable("XDG_STATE_HOME");
    if (!xdgStateHome.isEmpty()) {
        paths << xdgStateHome + QStringLiteral("/syncthing/config.xml");
    }

    // 4. $HOME/.local/state/syncthing/config.xml (Syncthing v1.27.0+)
    paths << home + QStringLiteral("/.local/state/syncthing/config.xml");

    // 5. $HOME/.config/syncthing/config.xml (pre-v1.27.0)
    paths << home + QStringLiteral("/.config/syncthing/config.xml");

    return paths;
}


} // namespace Kalburator::Sync
