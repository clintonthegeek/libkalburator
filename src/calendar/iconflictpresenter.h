#ifndef ICONFLICTPRESENTER_H
#define ICONFLICTPRESENTER_H

namespace Kalburator::Sync {

/**
 * @brief Interface for presenting conflicts to the user.
 *
 * The sync library needs to notify a UI component when new conflicts
 * are queued, but must not depend on the concrete ConflictDockWidget.
 * The app shell wires up the concrete widget at startup.
 *
 * Signal wiring (conflictResolved) is done by the app shell, not
 * through this interface, to avoid QObject diamond inheritance.
 */
class IConflictPresenter
{
public:
    virtual ~IConflictPresenter() = default;

    virtual void refreshConflicts() = 0;
};

} // namespace Kalburator::Sync

#endif // ICONFLICTPRESENTER_H
