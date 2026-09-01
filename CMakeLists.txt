cmake_minimum_required(VERSION 3.25)
project(CoreEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Keep every target (including tools) on the same UTF-8 source
    # encoding.  Applying this globally avoids headers being reparsed under
    # the host code page when they are included by an executable target.
    add_compile_options(/utf-8)
endif()

option(CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(Threads REQUIRED)

# Enforce the one-way product boundary at configure time. Engine code may
# expose reusable capabilities, but it must never depend on this repository's
# game client or authored content adapters.
file(GLOB_RECURSE CORE_BOUNDARY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.hpp")
foreach(core_source IN LISTS CORE_BOUNDARY_SOURCES)
    file(READ "${core_source}" core_source_text)
    if(core_source_text MATCHES "#[ \t]*include[ \t]*[<\"]game/")
        message(FATAL_ERROR "Engine/game boundary violation: ${core_source} includes game code")
    endif()
    if(core_source_text MATCHES "set_world_map_layers|world_map(_ids|_terrain|_height)?\\.coreimg")
        message(FATAL_ERROR "Legacy full-world atlas path is forbidden in Core: ${core_source}")
    endif()
endforeach()

add_library(core_runtime
    src/core/jobs/JobSystem.cpp
    src/core/economy/EconomyDefinitions.cpp
    src/core/economy/EconomyPopulationPhases.cpp
    src/core/economy/EconomyMarketPhases.cpp
    src/core/economy/CountryStore.cpp
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
    src/core/scripting/ScriptProgramDatabase.cpp
    src/core/scripting/ScriptCompiler.cpp
    src/core/scripting/ScriptCompilerScoped.cpp
    src/core/scripting/ScriptProfiler.cpp
    src/core/scripting/ScriptVm.cpp
    src/core/scripting/ScriptVmValues.cpp
    src/core/scripting/ScriptBytecode.cpp
    src/core/scripting/ScriptMemoizer.cpp
    src/core/scripting/ScopeResolver.cpp
    src/core/scripting/DynamicVariables.cpp
    src/core/scripting/GlobalScriptStore.cpp
    src/core/gameplay/ScriptedGameplay.cpp

    src/core/ai/UtilityAi.cpp
    src/core/content/ModManifest.cpp
    src/core/content/ModVersion.cpp
    src/core/content/VirtualFileSystem.cpp
    src/core/content/DefinitionDatabase.cpp
    src/core/content/WorldContentBinder.cpp
    src/core/content/GameplayContent.cpp
    src/core/content/ResearchContent.cpp
    src/core/content/NotificationContent.cpp
    src/core/gameplay/NotificationRuntime.cpp
    src/core/content/OnActionContent.cpp
    src/core/gameplay/OnActionRuntime.cpp
    src/core/gameplay/GameplaySignalRegistry.cpp
    src/core/content/ContentLoader.cpp
    src/core/content/LocalizationDatabase.cpp
    src/core/render/RenderSnapshotBuilder.cpp
    src/core/render/RenderGraph.cpp
    src/core/render/LivingMapRenderPlan.cpp
    src/core/render/GpuCapabilities.cpp
    src/core/render/StrategicCamera.cpp
    src/core/render/GpuDrivenPipeline.cpp
    src/core/render/BindlessMaterialSystem.cpp
    src/core/render/PhysicalLighting.cpp
    src/core/render/RenderQuality.cpp
    src/core/render/flag/DynamicFlag3D.cpp
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
    src/core/render/map/WorldMapPageCache.cpp
    src/core/render/map/WorldMapPageSource.cpp
    src/core/render/map/WorldMapPageStreamingPlanner.cpp
    src/core/render/map/WorldMapPageStreamer.cpp
    src/core/render/map/WorldStaticLayerSource.cpp
    src/core/render/map/MapModeStore.cpp
    src/core/world/ProvinceAdjacencyGraph.cpp
    src/core/world/HierarchicalPathfinder.cpp
    src/core/world/SpatialPlacement.cpp
    src/core/world/StateRegionIndex.cpp
    src/core/world/GeographyStore.cpp
    src/core/world/WorldBootstrap.cpp
    src/core/world/WorldBootstrapWire.cpp
    src/core/world/WorldTopology.cpp
    src/core/world/WorldStaticLayers.cpp
    src/core/world/WorldMapHierarchy.cpp
    src/core/world/WorldMapHierarchyWire.cpp
    src/core/grand_strategy/GrandStrategyStore.cpp
    src/core/grand_strategy/GrandStrategyValidation.cpp
    src/core/grand_strategy/GrandStrategyWeekly.cpp
    src/core/research/ResearchSystem.cpp
    src/core/save/SaveGame.cpp
    src/core/save/SaveGameLegacy.cpp
    src/core/save/SaveGameRuntime.cpp
    src/core/save/SaveGameScriptSections.cpp
    src/core/save/SaveGameWorldSections.cpp
    src/core/save/AsyncSavePipeline.cpp
    src/core/save/ReplayJournal.cpp
    src/core/runtime/CoreEngine.cpp
    src/core/runtime/GameContentRuntime.cpp
    src/core/runtime/DebugConsole.cpp
    src/core/localization/LocalizationStore.cpp
    src/core/ai/StrategicAiPlanner.cpp
    src/core/render/map/VectorMapTypography.cpp
    src/core/render/map/MapDecorationRenderer.cpp
    src/core/warfare/LogisticsNetwork.cpp
    src/core/warfare/BattlePhaseSystem.cpp
    src/core/render/vfx/LivingMapVfxSystem.cpp
    src/core/assets/AssetPack.cpp
    src/core/assets/ArchitectureKit.cpp
    src/core/assets/Material.cpp
    src/core/ui/StrategyUi.cpp
    src/core/ui/StrategyUiAnalytics.cpp
    src/core/ui/StrategyUiComponents.cpp
    src/core/ui/StrategyUiPrimitives.cpp
    src/core/ui/UiTheme.cpp
    src/core/ui/TooltipStack.cpp
    src/core/ui/ScriptedGui.cpp
    src/core/ui/ScriptedGuiCompiler.cpp
    src/core/ui/ScriptedGuiRuntime.cpp
    src/core/ui/ScriptedGuiValues.cpp
    src/core/ui/ScriptedGuiPainter.cpp
    src/core/ui/ScriptedGuiLayout.cpp
    src/core/ui/FontAtlas.cpp
    src/core/ui/LocalizationRichText.cpp
    src/core/editor/MapEditorSystem.cpp
    src/core/editor/GuiLayoutEditor.cpp
    src/core/worldpack/WorldPack.cpp
    src/core/worldpack/WorldPackMetadata.cpp
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
        # Repository sources are UTF-8 (including localized content keys and
        # documentation comments).  Make the source/execution charset
        # explicit so `/WX` does not depend on the host Windows code page.
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
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

add_executable(core_world_compiler src/apps/core_world_compiler.cpp)
target_link_libraries(core_world_compiler PRIVATE core_runtime)

add_executable(core_world_inspect src/apps/core_world_inspect.cpp)
target_link_libraries(core_world_inspect PRIVATE core_runtime)

add_executable(core_asset_cooker src/apps/core_asset_cooker.cpp)
target_link_libraries(core_asset_cooker PRIVATE core_runtime)
add_executable(core_asset_inspect src/apps/core_asset_inspect.cpp)
target_link_libraries(core_asset_inspect PRIVATE core_runtime)
add_executable(core_architecture_cooker src/apps/core_architecture_cooker.cpp)
target_link_libraries(core_architecture_cooker PRIVATE core_runtime)
add_executable(core_material_cooker src/apps/core_material_cooker.cpp)
target_link_libraries(core_material_cooker PRIVATE core_runtime)

if(UNIX AND NOT APPLE)
    target_link_libraries(core_runtime PRIVATE dl)
endif()
