#pragma once

/**
 * @file autoresolvestrategy.h
 * @brief Strategy vocabulary for automatic conflict resolution during merge.
 *
 * Extracted from conflict/conflictpolicy.h (architectural-redress Plan 6):
 * shape/'s RecordMerger consumes exactly this enum, so it lives in the
 * abstract transformation layer. conflict/ includes it downward and
 * re-exports it as Kalburator::Conflict::AutoResolveStrategy for source
 * compatibility (WildPalms palmconflicthandler.cpp et al.).
 */

namespace Kalburator::Shape {

/**
 * @brief Automatic resolution strategy
 */
enum class AutoResolveStrategy
{
    None,               ///< Never auto-resolve, always defer or ask
    SourceAlwaysWins,   ///< Source overwrites target automatically
    TargetAlwaysWins,   ///< Target overwrites source automatically
    NewerWins,          ///< Most recently modified version wins
    OlderWins,          ///< Preserve the older version (conservative)
    LargerWins,         ///< Keep the version with more content
    DuplicateAll        ///< Always create duplicates (never lose data)
};

} // namespace Kalburator::Shape
