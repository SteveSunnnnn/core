cmake_minimum_required(VERSION 3.25)
project(CoreEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CORE_BUILD_TESTS "Build Core tests" ON)
option(CORE_BUILD_DESKTOP "Build SDL3/Vulkan desktop shell" OFF)
option(CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CORE_BUILD_BENCHMARKS "Build Core microbenchmarks" OFF)
option(CORE_BUILD_STRESS_TESTS "Build long-running Core stress tests" OFF)

find_package(Threads REQUIRED)

add_library(core_runtime
    src/core/jobs/JobSystem.cpp
    src/core/economy/EconomyDefinitions.cpp
    src/core/economy/MarketStore.cpp
    src/core/economy/BuildingStore.cpp
    src/core/economy/PopStore.cpp
    src/core/economy/MarketEntityIndex.cpp
    src/core/economy/EconomySystem.cpp
    src/core/economy/CurrencyStore.cpp
    src/core/economy/BankStore.cpp
    src/core/economy/TradePolicyStore.cpp
    src/core/economy/ConstructionStore.cpp
    src/core/living/ProvinceEntityIndex.cpp
    src/core/living/LivingMapSystem.cpp
    src/core/living/LivingMapStreamingPlanner.cpp
    src/core/living/TransportNetwork.cpp
    src/core/simulation/GameClock.cpp
    src/core/simulation/World.cpp
    src/core/simulation/TickScheduler.cpp
    src/core/simulation/ModifierGraph.cpp
    src/core/simulation/HierarchicalModifierGraph.cpp
    src/core/simulation/CommandQueue.cpp
    src/core/scripting/ScriptRegistry.cpp
    src/core/scripting/SymbolTable.cpp
    src/core/scripting/CoreScriptParser.cpp
    src/core/scripting/ScriptContext.cpp
    src/core/scripting/ScriptProgram.cpp
    src/core/scripting/ScriptBytecode.cpp
    src/core/scripting/ScriptMemoizer.cpp
    src/core/scripting/ScopeResolver.cpp
    src/core/scripting/DynamicVariables.cpp
    src/core/scripting/GlobalScriptStore.cpp
    src/core/gameplay/ScriptedGameplay.cpp

    src/core/ai/UtilityAi.cpp
    src/core/content/ModManifest.cpp
    src/core/content/VirtualFileSystem.cpp
    src/core/content/DefinitionDatabase.cpp
    src/core/content/GameplayContent.cpp
    src/core/content/ResearchContent.cpp
    src/core/content/NotificationContent.cpp
    src/core/gameplay/NotificationRuntime.cpp
    src/core/content/OnActionContent.cpp
    src/core/gameplay/OnActionRuntime.cpp
    src/core/gameplay/GameplaySignalRegistry.cpp
    src/core/content/ContentLoader.cpp
    src/core/content/LocalizationDatabase.cpp
    src/core/render/RenderSnapshot.cpp
    src/core/render/RenderGraph.cpp
    src/core/render/LivingMapRenderPlan.cpp
    src/core/render/GpuCapabilities.cpp
    src/core/render/StrategicCamera.cpp
    src/core/render/GpuDrivenPipeline.cpp
    src/core/render/BindlessMaterialSystem.cpp
    src/core/render/PhysicalLighting.cpp
    src/core/render/terrain/TerrainClipmap.cpp

    src/core/render/terrain/TerrainHeightPage.cpp
    src/core/render/terrain/TerrainPageCache.cpp
    src/core/render/terrain/TerrainStreamingPlanner.cpp
    src/core/render/terrain/TerrainRenderPlan.cpp
    src/core/render/map/ProvinceRasterPage.cpp
    src/core/render/map/CoastDistancePage.cpp
    src/core/render/map/PoliticalMapState.cpp
    src/core/render/map/PoliticalMapRenderPlan.cpp
    src/core/render/map/VectorMapPipeline.cpp
    src/core/render/water/PhysicalWaterPass.cpp
    src/core/render/vulkan/VulkanProbe.cpp
    src/core/render/map/PoliticalMapStreamingPlanner.cpp
    src/core/render/map/ProvincePickingCache.cpp
    src/core/render/map/PoliticalMapPageBundle.cpp
    src/core/render/map/MapModeStore.cpp
    src/core/world/ProvinceAdjacencyGraph.cpp
    src/core/world/HierarchicalPathfinder.cpp
    src/core/world/SpatialPlacement.cpp
    src/core/world/GeographyStore.cpp
    src/core/world/WorldBootstrap.cpp
    src/core/grand_strategy/GrandStrategyStore.cpp
    src/core/research/ResearchSystem.cpp
    src/core/save/SaveGame.cpp
    src/core/save/AsyncSavePipeline.cpp
    src/core/save/ReplayJournal.cpp
    src/core/runtime/CoreEngine.cpp
    src/core/runtime/DebugConsole.cpp
    src/core/runtime/GameAppController.cpp
    src/core/localization/LocalizationStore.cpp
    src/core/ai/StrategicAiPlanner.cpp
    src/core/render/map/VectorMapTypography.cpp
    src/core/render/map/VictorianCartoucheRenderer.cpp
    src/core/warfare/LogisticsNetwork.cpp
    src/core/warfare/BattlePhaseSystem.cpp
    src/core/render/vfx/LivingMapVfxSystem.cpp
    src/core/assets/AssetPack.cpp
    src/core/assets/ArchitectureKit.cpp
    src/core/assets/Material.cpp
    src/core/ui/StrategyUi.cpp
    src/core/ui/VictorianHudSystem.cpp
    src/core/ui/TooltipStack.cpp
    src/core/ui/ScriptedGui.cpp
    src/core/ui/ScriptedGuiRuntime.cpp
    src/core/ui/FontAtlas.cpp
    src/core/editor/MapEditorSystem.cpp
    src/core/editor/GuiLayoutEditor.cpp
    src/core/worldpack/WorldPack.cpp
    src/core/io/RandomAccessFile.cpp
)

target_include_directories(core_runtime PUBLIC src)

target_compile_features(core_runtime PUBLIC cxx_std_23)
target_link_libraries(core_runtime PUBLIC Threads::Threads)

# Zstd is optional at source level, but strongly recommended for .coreworld packs.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
endif()
if(TARGET PkgConfig::ZSTD)
    target_link_libraries(core_runtime PRIVATE PkgConfig::ZSTD)
    target_compile_definitions(core_runtime PUBLIC CORE_HAS_ZSTD=1)
endif()
if(PkgConfig_FOUND)
    pkg_check_modules(XXHASH QUIET IMPORTED_TARGET libxxhash)
endif()
if(TARGET PkgConfig::XXHASH)
    target_link_libraries(core_runtime PRIVATE PkgConfig::XXHASH)
    target_compile_definitions(core_runtime PUBLIC CORE_HAS_XXHASH=1)
endif()

function(core_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        if(CORE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(CORE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

core_apply_warnings(core_runtime)

add_executable(core_cli src/apps/core_cli.cpp)
target_link_libraries(core_cli PRIVATE core_runtime)

add_executable(core_vulkan_probe src/apps/core_vulkan_probe.cpp)
target_link_libraries(core_vulkan_probe PRIVATE core_runtime)

add_executable(core_world_compiler src/apps/core_world_compiler.cpp)
target_link_libraries(core_world_compiler PRIVATE core_runtime)

add_executable(core_world_inspect src/apps/core_world_inspect.cpp)
target_link_libraries(core_world_inspect PRIVATE core_runtime)

add_executable(core_world_bootstrap_check src/apps/core_world_bootstrap_check.cpp)
target_link_libraries(core_world_bootstrap_check PRIVATE core_runtime)

add_executable(core_content_check src/apps/core_content_check.cpp)
target_link_libraries(core_content_check PRIVATE core_runtime)

add_executable(core_asset_cooker src/apps/core_asset_cooker.cpp)
target_link_libraries(core_asset_cooker PRIVATE core_runtime)
add_executable(core_asset_inspect src/apps/core_asset_inspect.cpp)
target_link_libraries(core_asset_inspect PRIVATE core_runtime)
add_executable(core_architecture_cooker src/apps/core_architecture_cooker.cpp)
target_link_libraries(core_architecture_cooker PRIVATE core_runtime)
add_executable(core_material_cooker src/apps/core_material_cooker.cpp)
target_link_libraries(core_material_cooker PRIVATE core_runtime)

add_executable(core_economy_demo src/apps/core_economy_demo.cpp)
target_link_libraries(core_economy_demo PRIVATE core_runtime)
add_executable(core_living_demo src/apps/core_living_demo.cpp)
target_link_libraries(core_living_demo PRIVATE core_runtime)
add_executable(core_grand_strategy_demo src/apps/core_grand_strategy_demo.cpp)
target_link_libraries(core_grand_strategy_demo PRIVATE core_runtime)
if(UNIX AND NOT APPLE)
    target_link_libraries(core_runtime PRIVATE dl)
endif()

if(CORE_BUILD_DESKTOP)
    # Fail fast before fetching/configuring SDL when the platform SDK needed by
    # the Vulkan backend is unavailable.
    find_package(Vulkan REQUIRED)
    include(FetchContent)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.14
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    add_executable(core_desktop
        src/apps/core_desktop.cpp
        src/core/platform/DesktopApp.cpp
        src/core/render/vulkan/VulkanDesktopBackend.cpp
    )
    target_link_libraries(core_desktop PRIVATE core_runtime SDL3::SDL3 Vulkan::Vulkan)
    core_apply_warnings(core_desktop)
endif()

if(CORE_BUILD_BENCHMARKS)
    add_executable(core_bench bench/core_bench.cpp)
    target_link_libraries(core_bench PRIVATE core_runtime)
    add_executable(core_economy_bench bench/economy_bench.cpp)
    target_link_libraries(core_economy_bench PRIVATE core_runtime)
    add_executable(core_living_bench bench/living_map_bench.cpp)
    target_link_libraries(core_living_bench PRIVATE core_runtime)
endif()


if(CORE_BUILD_STRESS_TESTS)
    add_executable(core_job_stress tests/job_system_stress.cpp)
    target_link_libraries(core_job_stress PRIVATE core_runtime)
endif()

if(CORE_BUILD_TESTS)
    enable_testing()
    # Tests use assert as an always-on contract check. Release normally defines
    # NDEBUG, which would otherwise erase both checks and any fixture calls inside
    # them, so explicitly keep test assertions enabled in every configuration.
    if(MSVC)
        add_compile_options(/UNDEBUG)
    else()
        add_compile_options(-UNDEBUG)
    endif()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE core_runtime)
    add_test(NAME core_tests COMMAND core_tests)
    add_executable(core_economy_tests tests/economy_tests.cpp)
    target_link_libraries(core_economy_tests PRIVATE core_runtime)
    add_test(NAME core_economy_tests COMMAND core_economy_tests)
    add_executable(core_living_tests tests/living_map_tests.cpp)
    target_link_libraries(core_living_tests PRIVATE core_runtime)
    add_test(NAME core_living_tests COMMAND core_living_tests)
    add_executable(core_engine_tests tests/engine_tests.cpp)
    target_link_libraries(core_engine_tests PRIVATE core_runtime)
    add_test(NAME core_engine_tests COMMAND core_engine_tests)
    add_executable(core_asset_ui_tests tests/asset_ui_tests.cpp)
    target_link_libraries(core_asset_ui_tests PRIVATE core_runtime)
    add_test(NAME core_asset_ui_tests COMMAND core_asset_ui_tests)
    add_executable(core_scripted_gui_tests tests/scripted_gui_tests.cpp)
    target_link_libraries(core_scripted_gui_tests PRIVATE core_runtime)
    add_test(NAME core_scripted_gui_tests COMMAND core_scripted_gui_tests)
    add_executable(core_logic_layer_tests tests/logic_layer_tests.cpp)
    target_link_libraries(core_logic_layer_tests PRIVATE core_runtime)
    add_test(NAME core_logic_layer_tests COMMAND core_logic_layer_tests)
    add_executable(core_script_runtime_tests tests/corescript_runtime_tests.cpp)
    target_link_libraries(core_script_runtime_tests PRIVATE core_runtime)
    add_test(NAME core_script_runtime_tests COMMAND core_script_runtime_tests)
    add_executable(core_world_bootstrap_tests tests/world_bootstrap_tests.cpp)
    target_link_libraries(core_world_bootstrap_tests PRIVATE core_runtime)
    add_test(NAME core_world_bootstrap_tests COMMAND core_world_bootstrap_tests)
    add_executable(core_strategy_system_tests tests/strategy_system_tests.cpp)
    target_link_libraries(core_strategy_system_tests PRIVATE core_runtime)
    add_test(NAME core_strategy_system_tests COMMAND core_strategy_system_tests)
    add_executable(core_runtime_save_tests tests/runtime_save_tests.cpp)
    target_link_libraries(core_runtime_save_tests PRIVATE core_runtime)
    target_compile_definitions(core_runtime_save_tests PRIVATE CORE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME core_runtime_save_tests COMMAND core_runtime_save_tests)
    add_executable(core_replay_journal_tests tests/replay_journal_tests.cpp)
    target_link_libraries(core_replay_journal_tests PRIVATE core_runtime)
    add_test(NAME core_replay_journal_tests COMMAND core_replay_journal_tests)
    add_executable(core_research_tests tests/research_tests.cpp)
    target_link_libraries(core_research_tests PRIVATE core_runtime)
    add_test(NAME core_research_tests COMMAND core_research_tests)
    add_executable(core_state_contract_tests tests/state_contract_tests.cpp)
    target_link_libraries(core_state_contract_tests PRIVATE core_runtime)
    add_test(NAME core_state_contract_tests COMMAND core_state_contract_tests)
    add_executable(core_mod_pipeline_tests tests/mod_pipeline_tests.cpp)
    target_link_libraries(core_mod_pipeline_tests PRIVATE core_runtime)
    add_test(NAME core_mod_pipeline_tests COMMAND core_mod_pipeline_tests)
    add_executable(core_notification_tests tests/notification_tests.cpp)
    target_link_libraries(core_notification_tests PRIVATE core_runtime)
    add_test(NAME core_notification_tests COMMAND core_notification_tests)
    add_executable(core_hierarchical_systems_tests tests/hierarchical_systems_tests.cpp)
    target_link_libraries(core_hierarchical_systems_tests PRIVATE core_runtime)
    add_test(NAME core_hierarchical_systems_tests COMMAND core_hierarchical_systems_tests)
    add_executable(core_memory_save_opt_tests tests/memory_save_opt_tests.cpp)
    target_link_libraries(core_memory_save_opt_tests PRIVATE core_runtime)
    add_test(NAME core_memory_save_opt_tests COMMAND core_memory_save_opt_tests)
    add_executable(core_script_vm_opt_tests tests/script_vm_opt_tests.cpp)
    target_link_libraries(core_script_vm_opt_tests PRIVATE core_runtime)
    add_test(NAME core_script_vm_opt_tests COMMAND core_script_vm_opt_tests)
    add_executable(core_gpu_driven_opt_tests tests/gpu_driven_opt_tests.cpp)
    target_link_libraries(core_gpu_driven_opt_tests PRIVATE core_runtime)
    add_test(NAME core_gpu_driven_opt_tests COMMAND core_gpu_driven_opt_tests)
    add_executable(core_vector_map_and_ui_tests tests/vector_map_and_ui_tests.cpp)
    target_link_libraries(core_vector_map_and_ui_tests PRIVATE core_runtime)
    add_test(NAME core_vector_map_and_ui_tests COMMAND core_vector_map_and_ui_tests)
    add_executable(core_grand_strategy_advanced_tests tests/grand_strategy_advanced_tests.cpp)
    target_link_libraries(core_grand_strategy_advanced_tests PRIVATE core_runtime)
    add_test(NAME core_grand_strategy_advanced_tests COMMAND core_grand_strategy_advanced_tests)
    add_executable(core_game_client_hud_tests tests/game_client_hud_tests.cpp)
    target_link_libraries(core_game_client_hud_tests PRIVATE core_runtime)
    add_test(NAME core_game_client_hud_tests COMMAND core_game_client_hud_tests)
    add_executable(core_tooltip_stack_tests tests/tooltip_stack_tests.cpp)
    target_link_libraries(core_tooltip_stack_tests PRIVATE core_runtime)
    add_test(NAME core_tooltip_stack_tests COMMAND core_tooltip_stack_tests)
    add_executable(core_slot_pool_tests tests/slot_pool_tests.cpp)
    target_link_libraries(core_slot_pool_tests PRIVATE core_runtime)
    add_test(NAME core_slot_pool_tests COMMAND core_slot_pool_tests)
    add_executable(core_map_editor_tests tests/map_editor_tests.cpp)
    target_link_libraries(core_map_editor_tests PRIVATE core_runtime)
    add_test(NAME core_map_editor_tests COMMAND core_map_editor_tests)
endif()
