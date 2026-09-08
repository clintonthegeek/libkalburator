#include <kalburator/types/logicalcalendar.h>

int main()
{
    Kalburator::Sync::LogicalCalendar calendar;
    return calendar.id.isEmpty() ? 0 : 1;
}
