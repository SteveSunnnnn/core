#include "game/harness/Scenes.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace core::harness {
namespace {

// Save/load is the boundary where determinism either survives or silently
// dies: any field that round-trips imperfectly makes a restored session
// diverge from the one that produced it. This surface exercises encode,
// decode, checksum equality and disk persistence, and reports which part of
// the round trip broke when something does.
class SaveLoadScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "saveload"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Save / Load"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "SaveGameCodec round trip: encode the authoritative state, decode it back and verify "
               "the checksums match. Also verifies mutation-then-restore actually rolls state back, "
               "and writes real files so the blob can be inspected outside the engine.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"S", "save to slot"}, {"L", "load from slot"}, {"R", "round-trip check"}};
    }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        const auto& c = ui.draw().theme().colors;

        ui.header("SLOT");
        ui.stat_line("slot size", std::format("{} bytes", slot_.bytes.size()));
        if (slot_.bytes.empty()) {
            ui.text_line("Slot is empty. Press Save to capture the current state.", c.text_muted);
        } else {
            ui.stat_line("format version", std::to_string(slot_.metadata.version));
            ui.stat_line("content hash", std::format("{:#018x}", slot_.metadata.content_hash));
            ui.stat_line("world pack hash", std::format("{:#018x}", slot_.metadata.world_pack_hash));
            ui.stat_line("world checksum", std::format("{:#018x}", slot_.metadata.world_checksum));
            ui.stat_line("runtime checksum", std::format("{:#018x}", slot_.metadata.runtime_checksum));
        }

        ui.spacer(6.0f);
        ui.header("OPERATIONS");
        if (ui.button("Save to slot")) save_slot(ctx);
        if (ui.button("Load from slot", !slot_.bytes.empty())) load_slot(ctx);
        if (ui.button("Round-trip check")) round_trip(ctx);
        if (ui.button("Mutation rollback test")) mutation_rollback(ctx);

        ui.spacer(6.0f);
        ui.header("DISK");
        ui.wrapped_text("Writes the slot to a file and reads it back, exercising the same codec "
                        "path a real launcher would use.",
                        c.text_muted, 17.0f);
        if (ui.button("Write slot to disk", !slot_.bytes.empty())) write_disk(ctx);
        if (ui.button("Read file into slot")) read_disk(ctx);
        ui.stat_line("last file", last_path_.empty() ? "(none)" : last_path_.filename().string());

        ui.spacer(6.0f);
        ui.header("LAST RESULT");
        if (!last_result_.empty()) {
            ui.wrapped_text(last_result_, last_ok_ ? c.text_positive : c.text_negative, 17.0f);
        }
        if (last_encode_ms_ > 0.0 || last_decode_ms_ > 0.0) {
            ui.stat_line("encode", std::format("{:.2f} ms", last_encode_ms_));
            ui.stat_line("decode", std::format("{:.2f} ms", last_decode_ms_));
        }
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        switch (sdl_keycode) {
        case SDLK_s: save_slot(ctx); return true;
        case SDLK_l: load_slot(ctx); return true;
        case SDLK_r: round_trip(ctx); return true;
        default: return false;
        }
    }

private:
    void save_slot(SceneContext& ctx) {
        if (ctx.engine == nullptr) return;
        const auto started = std::chrono::steady_clock::now();
        slot_ = ctx.engine->make_save();
        last_encode_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        if (slot_.bytes.empty()) {
            last_ok_ = false;
            last_result_ = "save failed: encoder produced no bytes";
            ctx.bad(last_result_);
            return;
        }
        last_ok_ = true;
        last_result_ = std::format("saved {} bytes, runtime checksum {:#018x}", slot_.bytes.size(),
                                   slot_.metadata.runtime_checksum);
        ctx.good(last_result_);
    }

    void load_slot(SceneContext& ctx) {
        if (ctx.engine == nullptr || slot_.bytes.empty()) return;
        const auto started = std::chrono::steady_clock::now();
        try {
            ctx.engine->restore(slot_.bytes);
        } catch (const std::exception& error) {
            last_decode_ms_ = 0.0;
            last_ok_ = false;
            last_result_ = std::format("restore threw: {}", error.what());
            ctx.bad(last_result_);
            return;
        }
        last_decode_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        const std::uint64_t after = ctx.engine->engine_checksum();
        last_ok_ = after == slot_.metadata.runtime_checksum;
        last_result_ = std::format("restored; checksum {:#018x} (slot said {:#018x}) -> {}", after,
                                   slot_.metadata.runtime_checksum,
                                   last_ok_ ? "MATCH" : "MISMATCH");
        if (last_ok_) ctx.good(last_result_);
        else ctx.bad(last_result_);
    }

    // Save, restore, save again: the two blobs and both checksums must agree.
    void round_trip(SceneContext& ctx) {
        if (ctx.engine == nullptr) return;
        const SaveGameBlob first = ctx.engine->make_save();
        if (first.bytes.empty()) {
            last_ok_ = false;
            last_result_ = "round trip aborted: empty save";
            ctx.bad(last_result_);
            return;
        }
        const std::uint64_t before = ctx.engine->engine_checksum();
        try {
            ctx.engine->restore(first.bytes);
        } catch (const std::exception& error) {
            last_ok_ = false;
            last_result_ = std::format("round trip aborted: restore threw ({})", error.what());
            ctx.bad(last_result_);
            return;
        }
        const std::uint64_t after = ctx.engine->engine_checksum();
        const SaveGameBlob second = ctx.engine->make_save();
        const bool same_bytes = first.bytes.size() == second.bytes.size() &&
                               std::equal(first.bytes.begin(), first.bytes.end(), second.bytes.begin());
        last_ok_ = before == after && same_bytes;
        last_result_ = std::format(
            "round trip: checksum {} / {} ({}), re-encoded blobs {} ({} bytes)", before, after,
            before == after ? "match" : "MISMATCH", same_bytes ? "identical" : "differ",
            first.bytes.size());
        if (last_ok_) ctx.good(last_result_);
        else ctx.bad(last_result_);
    }

    // Proves restore is a real rollback, not a no-op that happens to checksum.
    void mutation_rollback(SceneContext& ctx) {
        if (ctx.engine == nullptr) return;
        const CountryId player{0};
        if (ctx.engine->world().countries.size() == 0u) {
            last_ok_ = false;
            last_result_ = "rollback test skipped: world has no countries";
            ctx.warn(last_result_);
            return;
        }
        const double before = ctx.engine->world().countries.treasury(player);
        ctx.engine->queue_command(CommandType::AddTreasury, player, 12345.0);
        ctx.engine->advance_tick();
        const double mutated = ctx.engine->world().countries.treasury(player);

        const SaveGameBlob snapshot = ctx.engine->make_save();
        ctx.engine->queue_command(CommandType::AddTreasury, player, 6789.0);
        ctx.engine->advance_tick();
        const double drifted = ctx.engine->world().countries.treasury(player);

        try {
            ctx.engine->restore(snapshot.bytes);
        } catch (const std::exception& error) {
            last_ok_ = false;
            last_result_ = std::format("rollback test aborted: {}", error.what());
            ctx.bad(last_result_);
            return;
        }
        const double restored = ctx.engine->world().countries.treasury(player);

        // restore() clears the command queue, so the mutated value must come
        // back exactly rather than the drifted one.
        const bool moved = mutated != before;
        const bool rolled_back = restored == mutated;
        last_ok_ = moved && rolled_back;
        last_result_ = std::format("treasury {:.0f} -> {:.0f} -> {:.0f}; restore returned to {}",
                                   before, mutated, drifted, mutated);
        if (!moved) {
            last_result_ += " (mutation had no effect)";
            ctx.warn(last_result_);
        } else if (last_ok_) {
            ctx.good(std::format("rollback verified: {}", last_result_));
        } else {
            ctx.bad(std::format("rollback FAILED: {}", last_result_));
        }
    }

    void write_disk(SceneContext& ctx) {
        if (slot_.bytes.empty()) return;
        std::error_code ec;
        const auto directory = std::filesystem::path{"harness_saves"};
        std::filesystem::create_directories(directory, ec);
        last_path_ = directory / "slot_0.bin";
        std::ofstream out{last_path_, std::ios::binary};
        if (!out) {
            last_ok_ = false;
            last_result_ = "disk write failed: could not open file";
            ctx.bad(last_result_);
            return;
        }
        out.write(reinterpret_cast<const char*>(slot_.bytes.data()),
                  static_cast<std::streamsize>(slot_.bytes.size()));
        out.close();
        last_ok_ = true;
        last_result_ = std::format("wrote {} bytes to {}", slot_.bytes.size(), last_path_.string());
        ctx.good(last_result_);
    }

    void read_disk(SceneContext& ctx) {
        if (last_path_.empty()) {
            ctx.warn("no file has been written yet");
            return;
        }
        std::ifstream in{last_path_, std::ios::binary};
        if (!in) {
            last_ok_ = false;
            last_result_ = std::format("disk read failed: cannot open {}", last_path_.string());
            ctx.bad(last_result_);
            return;
        }
        std::vector<std::byte> buffer{std::istreambuf_iterator<char>{in},
                                      std::istreambuf_iterator<char>{}};
        slot_.bytes = std::move(buffer);
        // Metadata is only meaningful after a decode; leave it stale-cleared so
        // the panel does not claim a checksum the engine has not confirmed.
        slot_.metadata = {};
        last_ok_ = true;
        last_result_ = std::format("read {} bytes from {}", slot_.bytes.size(), last_path_.string());
        ctx.good(last_result_);
    }

    SaveGameBlob slot_{};
    std::filesystem::path last_path_{};
    std::string last_result_{};
    bool last_ok_ = true;
    double last_encode_ms_ = 0.0;
    double last_decode_ms_ = 0.0;
};

} // namespace

TestScenePtr make_saveload_scene() { return std::make_unique<SaveLoadScene>(); }

} // namespace core::harness
