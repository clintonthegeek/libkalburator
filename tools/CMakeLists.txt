# EEE Phase 6 — convergence-matrix generator (regenerates the committed
# docs/campaign/eee/CONVERGENCE-MATRIX.md; byte-enforced by
# tst_gm_pipeline_convergence).
add_subdirectory(matrixgen)

# IP.12 (incidence-parity campaign, O90) — cross-process demote-purity
# probe. No vendor credentials/network involved (unlike graphcli/googlecli),
# so built unconditionally, same as matrixgen. Consumed by
# tests/calendar/tst_demote_purity.cpp via QProcess.
add_subdirectory(demotepurityprobe)

if(KALBURATOR_BUILD_GRAPHCLI)
    add_subdirectory(graphcli)
    add_subdirectory(msroundtrip)
endif()
if(KALBURATOR_BUILD_GOOGLECLI)
    add_subdirectory(googlecli)
    add_subdirectory(groundtrip)
endif()
