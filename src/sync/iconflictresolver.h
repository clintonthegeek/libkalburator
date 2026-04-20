#ifndef ICONFLICTRESOLVER_H
#define ICONFLICTRESOLVER_H

#include "synctypes.h"

/**
 * @brief Interface for conflict resolution providers.
 *
 * This interface allows dependency injection of conflict resolution logic,
 * enabling unit tests to provide mock implementations that return predetermined
 * resolutions without requiring GUI interaction.
 *
 * Production code uses DialogConflictResolver, which shows the standard
 * ConflictResolutionDialog. Tests can use MockConflictResolver or any
 * custom implementation that returns specific resolutions.
 *
 * Example test usage:
 * @code
 * class MockResolver : public IConflictResolver {
 * public:
 *     ConflictResolution resolveConflict(const ConflictInfo&, QWidget*) override {
 *         return m_resolution;
 *     }
 *     ConflictResolution m_resolution = ConflictResolution::SourceWins;
 * };
 *
 * MockResolver *mockResolver = new MockResolver();
 * mockResolver->m_resolution = ConflictResolution::TargetWins;
 * conflictManager->setConflictResolver(mockResolver);
 * @endcode
 */
class IConflictResolver
{
public:
    virtual ~IConflictResolver() = default;

    /**
     * @brief Resolve a conflict, potentially showing UI.
     *
     * @param conflict The conflict information to resolve
     * @param parentWidget Parent widget for dialogs (may be nullptr in tests)
     * @return The user's chosen resolution
     */
    virtual ConflictResolution resolveConflict(const ConflictInfo &conflict,
                                                QWidget *parentWidget) = 0;

    /**
     * @brief Get the merged iCal data from the last resolution.
     *
     * Only valid if the last resolveConflict() returned CustomMerge.
     * The default implementation returns an empty string.
     *
     * @return The merged iCal data, or empty if not applicable
     */
    virtual QString lastMergedIcalData() const { return QString(); }
};

#endif // ICONFLICTRESOLVER_H
