#ifndef DATADOMAIN_H
#define DATADOMAIN_H

namespace Kalburator::Sync {

/// Semantic classification of data flowing through the incidence pipeline.
/// Calendar: real calendar incidences (ICS, CalDAV, org-mode, etc.).
/// Project: tasks bridged from a non-calendar data model (PlanStan blocks).
enum class DataDomain { Calendar, Project };

} // namespace Kalburator::Sync

#endif // DATADOMAIN_H
