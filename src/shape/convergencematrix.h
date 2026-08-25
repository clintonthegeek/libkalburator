#pragma once

/**
 * @file convergencematrix.h
 * @brief Generates the EEE campaign's convergence-matrix ledger (the
 * "extinguish exhibit"): a per-property ledger of every DECLARED loss on
 * every canon→vendor demote edge across the registered stock-shape
 * contributions.
 *
 * GENERATED artifact discipline: `tools/matrixgen` regenerates the
 * committed doc (`docs/campaign/eee/CONVERGENCE-MATRIX.md`);
 * `tst_gm_pipeline_convergence` enforces that the committed copy matches
 * regeneration byte-for-byte, so growing an edges() list without
 * regenerating is a RED test (O63 rule applied to the ledger).
 */

#include "lossprofile.h"
#include "shapecontribution.h"

#include <QHash>
#include <QList>
#include <QString>
#include <algorithm>

namespace Kalburator::Shape {

class ConvergenceMatrix {
public:
    struct DomainInput {
        QString domain;
        const ShapeContribution* contribution = nullptr;
    };

    /// Markdown ledger. Deterministic: rows sorted by (edge, property).
    static QString generate(const QList<DomainInput>& inputs)
    {
        QString out;
        out += QLatin1String("# EEE Convergence Matrix\n\n");
        out += QLatin1String(
            "**GENERATED artifact** — regenerate with `tools/matrixgen` and\n"
            "commit alongside any change that grows or re-rules a stock-shape\n"
            "`edges()` list. Enforced byte-for-byte by\n"
            "`tst_gm_pipeline_convergence::committedMatrixMatchesGenerated`\n"
            "(FINDINGS O63 discipline applied to the ledger).\n\n");
        out += QLatin1String(
            "Per-property ledger of every **declared** loss on every\n"
            "canon→vendor demote edge. A canon property absent from an\n"
            "edge's rows survives that crossing unchanged. Loss kinds:\n"
            "`Dropped` < `Simplified` < `Degraded` < `Reversible` in\n"
            "increasing fidelity (Reversible = carried verbatim in a vendor\n"
            "extension channel).\n\n");
        out += QLatin1String(
            "## Carrier-survival verdicts (O66 + correction, 2026-08-24)\n\n"
            "Live drills on consumer accounts settled the O61(e) question\n"
            "per channel. `Reversible` rulings split three ways:\n\n"
            "- **live-Reversible** — Google People `clientData`\n"
            "  (create + fresh read proven), Google Calendar\n"
            "  `extendedProperties.private` (carriers survive consumer\n"
            "  creates byte-exact — Phase-2 checkpoint and A4 replay both\n"
            "  proven), and both Graph open-extension channels (`contact`,\n"
            "  `todoTask`), which SURVIVE when spoken to properly:\n"
            "  nav-property `POST .../{id}/extensions` — never PATCH-borne,\n"
            "  never inline-at-create (todoTask inline-create is echoed but\n"
            "  NOT persisted) — then collection-level\n"
            "  `$expand=extensions($filter=Id eq '<full-id>')`; the Outlook\n"
            "  full-id prefix is `Microsoft.OutlookServices.\n"
            "  OpenTypeExtension.*`.\n"
            "- **offline-only** — MS Graph event\n"
            "  `singleValueExtendedProperties` (O61(e): silently stripped\n"
            "  on consumer creates; PATCH-in-place works).\n"
            "- **no channel** — Google Tasks has no extension point; all\n"
            "  its non-carried properties remain `Dropped` (O66(c)\n"
            "  corpus-confirms).\n\n"
            "Backend rules (binding): nav POSTs only; filtered expand with\n"
            "the RETURNED full id; re-READ after write — never trust a\n"
            "create echo. Consumer contact GET-by-id is unreliable — drive\n"
            "reads through listings/delta/$expand.\n\n");

        for (const DomainInput& input : inputs) {
            const auto* c = input.contribution;
            if (!c)
                continue;
            out += QStringLiteral("## %1\n\n").arg(input.domain);

            // Edge inventory first.
            const auto edges = c->edges();
            out += QLatin1String("### Edge inventory\n\n");
            for (const TransformationEdge& e : edges) {
                out += QStringLiteral("- `%1 → %2`%3\n")
                           .arg(e.from.encoding.toString(),
                                e.to.encoding.toString(),
                                e.loss.isLossless()
                                    ? QString()
                                    : QStringLiteral(" (declared lossy)"));
            }
            out += QLatin1Char('\n');

            // Then the per-property loss rows.
            bool anyLoss = false;
            for (const TransformationEdge& e : edges) {
                if (e.loss.isLossless() || e.loss.affected.isEmpty())
                    continue;
                anyLoss = true;
                out += QStringLiteral("### %1 → %2\n\n")
                           .arg(e.from.encoding.toString(), e.to.encoding.toString());
                out += QLatin1String("| Property | LossKind |\n|---|---|\n");
                QList<QString> props;
                for (auto it = e.loss.affected.constBegin();
                     it != e.loss.affected.constEnd(); ++it)
                    props << it.key().toString();
                std::sort(props.begin(), props.end());
                for (const QString& prop : props) {
                    out += QStringLiteral("| %1 | %2 |\n")
                               .arg(prop,
                                    lossKindName(
                                        e.loss.affected.value(PropertyId{prop})));
                }
                out += QLatin1Char('\n');
            }
            if (!anyLoss)
                out += QLatin1String("_No declared losses._\n\n");
        }
        return out;
    }

    static const char* lossKindName(LossKind kind)
    {
        switch (kind) {
        case LossKind::Dropped:    return "Dropped";
        case LossKind::Simplified: return "Simplified";
        case LossKind::Degraded:   return "Degraded";
        case LossKind::Reversible: return "Reversible";
        }
        return "?";
    }
};

}  // namespace Kalburator::Shape
