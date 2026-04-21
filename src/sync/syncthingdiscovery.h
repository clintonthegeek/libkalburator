#ifndef SYNCTHINGDISCOVERY_H
#define SYNCTHINGDISCOVERY_H

#include <QUrl>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

/**
 * @brief Auto-discover Syncthing configuration from local config files.
 *
 * Reads config.xml from standard Syncthing config locations to extract
 * the GUI address and API key. Purely static — no state, no network.
 */
class SyncthingDiscovery
{
public:
    struct Config {
        QUrl url;           ///< GUI URL (e.g., http://127.0.0.1:8384)
        QString apiKey;     ///< REST API key
        bool found = false; ///< Whether a valid config was discovered
    };

    /**
     * @brief Discover Syncthing config from standard system paths.
     *
     * Search order (Linux):
     * 1. $STCONFDIR/config.xml
     * 2. $STHOMEDIR/config.xml
     * 3. $XDG_STATE_HOME/syncthing/config.xml
     * 4. $HOME/.local/state/syncthing/config.xml
     * 5. $HOME/.config/syncthing/config.xml
     *
     * @return Config with url and apiKey if found, or {found=false}
     */
    static Config discoverConfig();

    /**
     * @brief Parse a specific config.xml file.
     * @param configPath Full path to config.xml
     * @return Config with url and apiKey if valid, or {found=false}
     */
    static Config discoverFromPath(const QString &configPath);

    /**
     * @brief Try multiple config paths in order, return first found.
     * @param paths List of full paths to config.xml files
     * @return Config from the first valid file, or {found=false}
     */
    static Config discoverFromPaths(const QStringList &paths);

    /**
     * @brief Get the list of standard config.xml search paths.
     * @return Ordered list of paths to check
     */
    static QStringList standardConfigPaths();
};

} // namespace Kalburator::Sync

#endif // SYNCTHINGDISCOVERY_H
