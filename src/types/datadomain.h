#ifndef DATADOMAIN_H
#define DATADOMAIN_H

/// Semantic classification of data flowing through the incidence pipeline.
/// Calendar: real calendar incidences (ICS, CalDAV, org-mode, etc.).
/// Project: tasks bridged from a non-calendar data model (PlanStan blocks).
enum class DataDomain { Calendar, Project };

#endif // DATADOMAIN_H
