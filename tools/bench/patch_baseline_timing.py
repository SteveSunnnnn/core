#!/usr/bin/env python3
"""Add minimal timing instrumentation to a pristine checkout of the renderer.

The A/B benchmark needs a "before" build that is bit-for-bit the original
renderer plus just enough instrumentation to report the same timing fields as
the optimized build. This script grafts that instrumentation on so the
comparison isolates rendering changes rather than measurement changes.

Usage:
    python tools/bench/patch_baseline_timing.py <source-root>
"""

import sys
from pathlib import Path


def patch_once(text: str, anchor: str, addition: str, label: str) -> str:
    if anchor not in text:
        raise SystemExit(f"anchor not found for {label}")
    if text.count(anchor) != 1:
        raise SystemExit(f"anchor is ambiguous ({text.count(anchor)}x) for {label}")
    return text.replace(anchor, addition, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_baseline_timing.py <source-root>")
    root = Path(sys.argv[1])
    header = root / "VulkanDesktopBackend.hpp"
    source = root / "VulkanDesktopBackend.cpp"

    h = header.read_text(encoding="utf-8")
    c = source.read_text(encoding="utf-8")

    # ---- header: chrono include -------------------------------------------
    h = patch_once(
        h,
        "#include <vector>\n",
        "#include <chrono>\n#include <vector>\n",
        "chrono include",
    )

    # ---- header: private members + helper declaration ---------------------
    members = """
    // ---- Baseline benchmark instrumentation ------------------------------
    // Grafted on so the A/B run measures the original renderer with the same
    // timing harness as the optimized one. Not part of product behaviour.
    VkQueryPool query_pools_[frames_in_flight]{};
    bool timing_supported_ = false;
    bool query_written_[frames_in_flight]{};
    double timestamp_period_ns_ = 1.0;
    double last_gpu_ms_ = 0.0;
    std::chrono::steady_clock::time_point frame_start_{};
    std::chrono::steady_clock::time_point last_frame_time_{};
    bool have_last_frame_ = false;
    // Wall-clock interval between successive frames. This is what determines
    // the achievable frame rate, so the optimized build measures the same way.
    std::vector<double> frame_samples_;
    std::vector<double> gpu_samples_;
    std::vector<double> cpu_samples_;
    [[nodiscard]] double collect_gpu_timing(std::uint32_t frame);
};

} // namespace core"""
    h = patch_once(h, "\n};\n\n} // namespace core", members, "private members")

    # ---- source: create query pools ---------------------------------------
    c = patch_once(
        c,
        """        vkcheck(vkCreateFence(device_, &fence_info, nullptr, &frames_[index].fence), "vkCreateFence");
    }
}""",
        """        vkcheck(vkCreateFence(device_, &fence_info, nullptr, &frames_[index].fence), "vkCreateFence");
    }

    // Timestamp queries are optional infrastructure: if the device refuses
    // them the harness simply reports CPU timings only.
    timestamp_period_ns_ = static_cast<double>(properties_.limits.timestampPeriod);
    VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_info.queryCount = 2;
    timing_supported_ = true;
    for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
        if (vkCreateQueryPool(device_, &query_info, nullptr, &query_pools_[index]) != VK_SUCCESS) {
            timing_supported_ = false;
            break;
        }
    }
}""",
        "query pool creation",
    )

    # ---- source: destroy query pools --------------------------------------
    c = patch_once(
        c,
        """    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}""",
        """    for (auto& pool : query_pools_) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_, pool, nullptr);
            pool = VK_NULL_HANDLE;
        }
    }
    timing_supported_ = false;
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}""",
        "query pool destruction",
    )

    # ---- source: frame start ----------------------------------------------
    c = patch_once(
        c,
        """    auto& frame = frames_[frame_index_];
    vkcheck(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
""",
        """    auto& frame = frames_[frame_index_];
    vkcheck(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    // The fence we just waited on covers the submission that last recorded
    // this slot, so its timestamp pair is now safe to read back.
    const double frame_gpu_ms = collect_gpu_timing(frame_index_);
    const auto frame_now = std::chrono::steady_clock::now();
    double frame_ms = 0.0;
    if (have_last_frame_) {
        frame_ms = std::chrono::duration<double, std::milli>(frame_now - last_frame_time_).count();
    }
    last_frame_time_ = frame_now;
    have_last_frame_ = true;
    frame_start_ = frame_now;
""",
        "frame start timing",
    )

    # ---- source: timestamp begin ------------------------------------------
    c = patch_once(
        c,
        """    vkcheck(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");
""",
        """    vkcheck(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");
    if (timing_supported_) {
        vkCmdResetQueryPool(frame.command, query_pools_[frame_index_], 0, 2);
        vkCmdWriteTimestamp(frame.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            query_pools_[frame_index_], 0u);
    }
""",
        "timestamp begin",
    )

    # ---- source: timestamp end --------------------------------------------
    c = patch_once(
        c,
        """    vkcheck(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");
""",
        """    if (timing_supported_) {
        vkCmdWriteTimestamp(frame.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            query_pools_[frame_index_], 1u);
        query_written_[frame_index_] = true;
    }
    vkcheck(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");
""",
        "timestamp end",
    )

    # ---- source: sample collection ----------------------------------------
    c = patch_once(
        c,
        """    frame_index_ = (frame_index_ + 1u) % frames_in_flight;
    ++frames_presented_;
}""",
        """    const double frame_cpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start_)
            .count();
    cpu_samples_.push_back(frame_cpu_ms);
    if (frame_ms > 0.0) {
        frame_samples_.push_back(frame_ms);
    }
    if (frame_gpu_ms > 0.0) {
        last_gpu_ms_ = frame_gpu_ms;
        gpu_samples_.push_back(frame_gpu_ms);
    }
    frame_index_ = (frame_index_ + 1u) % frames_in_flight;
    ++frames_presented_;
}

double VulkanDesktopBackend::collect_gpu_timing(std::uint32_t frame) {
    if (!timing_supported_ || !query_written_[frame] || query_pools_[frame] == VK_NULL_HANDLE) {
        return 0.0;
    }
    std::uint64_t timestamps[2] = {0, 0};
    const auto result = vkGetQueryPoolResults(device_, query_pools_[frame], 0, 2,
                                              sizeof(timestamps), timestamps, sizeof(std::uint64_t),
                                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS || timestamps[1] <= timestamps[0]) {
        return 0.0;
    }
    return static_cast<double>(timestamps[1] - timestamps[0]) * timestamp_period_ns_ / 1.0e6;
}""",
        "sample collection",
    )

    # ---- source: report fields --------------------------------------------
    c = patch_once(
        c,
        """           << "ui_pipeline=" << (ui_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\\n';""",
        """           << "ui_pipeline=" << (ui_pipeline_ != VK_NULL_HANDLE ? 1 : 0) << '\\n'
           << "pipeline_cache=0\\n"
           << "timing_supported=" << (timing_supported_ ? 1 : 0) << '\\n'
           << benchmark_timing_fields();""",
        "report fields",
    )

    # ---- source: stats helper ---------------------------------------------
    c = patch_once(
        c,
        "\n} // namespace core",
        '''
namespace {

struct SampleSummary {
    std::size_t count = 0;
    double avg = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double p95 = 0.0;
};

SampleSummary summarise(const std::vector<double>& samples) {
    SampleSummary result;
    result.count = samples.size();
    if (samples.empty()) {
        return result;
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (const double value : sorted) {
        sum += value;
    }
    result.avg = sum / static_cast<double>(sorted.size());
    result.minimum = sorted.front();
    result.maximum = sorted.back();
    const std::size_t p95_index = std::min<std::size_t>(
        sorted.size() - 1u, static_cast<std::size_t>(sorted.size() * 0.95));
    result.p95 = sorted[p95_index];
    return result;
}

} // namespace

std::string VulkanDesktopBackend::benchmark_timing_fields() const {
    const SampleSummary frame = summarise(frame_samples_);
    const SampleSummary gpu = summarise(gpu_samples_);
    const SampleSummary cpu = summarise(cpu_samples_);
    std::ostringstream out;
    // Field names deliberately match the optimized renderer's report so the
    // A/B script can parse both without a translation table.
    out << "sampled_frames=" << frame.count << '\\n'
        << "avg_frame_ms=" << frame.avg << '\\n'
        << "min_frame_ms=" << frame.minimum << '\\n'
        << "max_frame_ms=" << frame.maximum << '\\n'
        << "p95_frame_ms=" << frame.p95 << '\\n'
        << "avg_gpu_ms=" << gpu.avg << '\\n'
        << "avg_cpu_ms=" << cpu.avg << '\\n'
        << "fps=" << (frame.avg > 0.0 ? 1000.0 / frame.avg : 0.0) << '\\n';
    return out.str();
}

} // namespace core''',
        "stats helper",
    )

    # ---- source: declaration for the stats helper -------------------------
    h = patch_once(
        h,
        "    [[nodiscard]] double collect_gpu_timing(std::uint32_t frame);",
        "    [[nodiscard]] double collect_gpu_timing(std::uint32_t frame);\n"
        "    [[nodiscard]] std::string benchmark_timing_fields() const;",
        "stats helper declaration",
    )
    h = patch_once(
        h,
        "#include <chrono>\n",
        "#include <algorithm>\n#include <chrono>\n#include <sstream>\n#include <string>\n",
        "stats includes",
    )

    header.write_text(h, encoding="utf-8")
    source.write_text(c, encoding="utf-8")
    print(f"patched {header.name} and {source.name}")


if __name__ == "__main__":
    main()
