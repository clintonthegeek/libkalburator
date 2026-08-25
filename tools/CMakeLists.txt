# EEE Phase 6 — convergence-matrix generator (regenerates the committed
# docs/campaign/eee/CONVERGENCE-MATRIX.md; byte-enforced by
# tst_gm_pipeline_convergence).
add_subdirectory(matrixgen)

if(KALBURATOR_BUILD_GRAPHCLI)
    add_subdirectory(graphcli)
    add_subdirectory(msroundtrip)
endif()
if(KALBURATOR_BUILD_GOOGLECLI)
    add_subdirectory(googlecli)
    add_subdirectory(groundtrip)
endif()
