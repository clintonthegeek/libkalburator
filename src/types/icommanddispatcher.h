#ifndef ICOMMANDDISPATCHER_H
#define ICOMMANDDISPATCHER_H

#include <KCalendarCore/Incidence>

class QUndoStack;

/**
 * @brief Narrow interface for undo/command infrastructure.
 *
 * Provides access to the undo stack and unified command dispatch.
 * Used by libraries that need to push undoable commands without
 * depending on CollectionController directly.
 *
 * Implemented by CollectionController in the app shell.
 */
class ICommandDispatcher
{
public:
    virtual ~ICommandDispatcher() = default;

    virtual QUndoStack* undoStack() = 0;

    virtual void pushUpdateCommand(const QString &calendarId,
                                   const KCalendarCore::Incidence::Ptr &oldIncidence,
                                   const KCalendarCore::Incidence::Ptr &newIncidence) = 0;
};

#endif // ICOMMANDDISPATCHER_H
