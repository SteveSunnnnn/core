#pragma once

#include "game/harness/TestHarness.hpp"
#include "game/harness/TestScene.hpp"

namespace core::harness {

// Every factory returns one self-contained scene. Adding a surface means
// adding a factory here and a line in register_all_scenes; nothing else in the
// harness needs to change.
TestScenePtr make_simulation_scene();
TestScenePtr make_saveload_scene();
TestScenePtr make_scripting_scene();
TestScenePtr make_notification_scene();
TestScenePtr make_onaction_scene();
TestScenePtr make_research_scene();
TestScenePtr make_economy_scene();
TestScenePtr make_ai_scene();
TestScenePtr make_jobs_scene();
TestScenePtr make_render_scene();
TestScenePtr make_camera_flag_scene();
TestScenePtr make_world_scene();
TestScenePtr make_profiler_scene();
TestScenePtr make_localization_scene();

// Installs every surface in presentation order.
void register_all_scenes(TestHarness& harness);

} // namespace core::harness
