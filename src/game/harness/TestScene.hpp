#pragma once

#include "core/base/StrongId.hpp"
#include "core/render/StrategicCamera.hpp"
#include "core/render/flag/DynamicFlag3D.hpp"
#include "core/runtime/CoreEngine.hpp"
#include "core/render/vulkan/VulkanDesktopBackend.hpp"
#include "core/ui/StrategyUi.hpp"
#include "game/harness/HarnessUi.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core::harness {

// Bounded ring of timestamped messages shared by every scene, so a scene can
// report what an engine call actually did without owning its own log widget.
class HarnessLog {
public:
    static constexpr std::size_t capacity = 200;

    enum class Level : std::uint8_t { Info, Good, Warn, Bad };

    void push(Level level, std::string message);
    void clear() noexcept { entries_.clear(); }

    struct Entry {
        Level level = Level::Info;
        std::string text;
        std::uint64_t tick = 0;
    };
    [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }

private:
    std::deque<Entry> entries_;
};

// Everything a scene may touch. The harness owns every pointee; scenes only
// borrow. Keeping this a plain struct means a new scene can be added without
// threading new parameters through the host.
struct SceneContext {
    CoreEngine* engine = nullptr;
    VulkanDesktopBackend* backend = nullptr;
    StrategicCamera* camera = nullptr;
    DynamicFlag3D* flag = nullptr;
    HarnessLog* log = nullptr;

    // Shared time controls owned by the host; scenes read and mutate them so
    // the global bar and every scene stay in agreement.
    bool* paused = nullptr;
    int* speed = nullptr;

    std::optional<ProvinceId>* selected_province = nullptr;
    std::uint64_t tick = 0;

    void info(std::string message) const;
    void good(std::string message) const;
    void warn(std::string message) const;
    void bad(std::string message) const;
};

// One engine capability, one scene. Scenes are independent units: each owns
// its own scratch state and is activated/deactivated as the player switches,
// so a scene can be verified in isolation and re-entered for regression
// checks without restarting the application.
class TestScene {
public:
    virtual ~TestScene() = default;

    // Stable identity used for navigation and hotkey binding.
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view title() const noexcept = 0;
    // One line shown under the title: what engine surface this exercises.
    [[nodiscard]] virtual std::string_view summary() const noexcept = 0;

    // Short list of keys this scene responds to, rendered in the panel.
    [[nodiscard]] virtual std::vector<std::pair<std::string, std::string>> hotkeys() const {
        return {};
    }

    [[nodiscard]] virtual float preferred_panel_width() const noexcept { return 520.0f; }

    // A scene can request that the global clock keep running while it is open.
    [[nodiscard]] virtual bool wants_simulation_running() const noexcept { return false; }

    virtual void on_activate(SceneContext& ctx) { (void)ctx; }
    virtual void on_deactivate(SceneContext& ctx) { (void)ctx; }

    // Real-time update. Called every frame, independent of simulation pause.
    virtual void on_update(SceneContext& ctx, float dt_seconds) { (void)ctx; (void)dt_seconds; }

    // Draw the scene's control panel. The host has already opened a panel with
    // the scene title and set the vertical cursor.
    virtual void on_ui(SceneContext& ctx, HarnessUi& ui) = 0;

    // Return true when the key was consumed, which stops host-level handling.
    virtual bool on_key(SceneContext& ctx, int sdl_keycode) {
        (void)ctx;
        (void)sdl_keycode;
        return false;
    }
};

using TestScenePtr = std::unique_ptr<TestScene>;

} // namespace core::harness
