# Core 1.0 RC-GPU Release Status

Date: 2026-08-26

This checkpoint is intended for target-machine GPU validation. It is not `1.0 Final`.

## Local release gates

The release procedure requires:
1. Fresh Release configure/build with warnings as errors.
2. All CTest targets.
3. JobSystem stress test.
4. ASan + UBSan tests/stress.
5. TSan tests/stress.
6. 300k and 1M economy benchmark capture.
7. GIS → manifest → `.coreworld` → WorldBootstrap smoke test.
8. Python tool syntax validation.

The historical checkpoint results are captured in `VALIDATION_1_0.txt`.
Current verification is recorded in `docs/FINAL_ENGINE_AUDIT.md`.

## Target Windows GPU gate

Run `tools\\windows\\validate_core.bat`. The script performs a clean build including `core_desktop`, compiles/validates shaders and executes a fixed-frame Vulkan Validation Layer test. It writes `validation-output/core_gpu_report.txt` and `validation-output/core_validation_report.txt`.

A successful swapchain/device gate does not by itself certify final Victoria/EU-scale visual fidelity. Full production Vulkan draw integration and visual A/B remain required before `1.0 Final` promotion.
