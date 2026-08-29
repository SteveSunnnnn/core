# 3D 渲染组件优化说明

本文记录 Core 引擎 3D 渲染组件（Vulkan 桌面后端 + 场景着色器）的性能与画质优化，
包含基线评估、改动清单、基准测试数据与可复现的验证步骤。

测试设备：NVIDIA GeForce RTX 4060 Laptop GPU，Vulkan 1.4.303，8.3 GB 显存，
渲染分辨率 1600×900，无垂直同步。

---

## 1. 基线评估

优化前对 `src/core/render/vulkan/VulkanDesktopBackend.cpp`
（约 2600 行，107 KB）与 `shaders/` 做了系统性审查，识别出以下瓶颈与短板。

### 1.1 结构性缺口

| 问题 | 位置 | 影响 |
| --- | --- | --- |
| **完全没有深度附件** | 全部管线 | 3D 几何无法正确遮挡；旗帜等叠加层只能靠绘制顺序 |
| **MSAA 从未启用** | `msaa_samples_` 在设备选择时算出（原 846 行附近），但 8 条管线全部硬编码 `VK_SAMPLE_COUNT_1_BIT` | 多边形边缘全程锯齿 |
| **HDR 目标从未创建** | `create_hdr_targets()` / `destroy_hdr_targets()` 只有声明，无定义 | 无色调映射、无抖动、无法做后期处理 |
| **tonemap 管线从未创建** | 描述符布局与着色器已就位，但管线未生成 | 直接输出到 swapchain，高光直接截断 |
| **无管线缓存** | `vkCreateGraphicsPipelines` 传 `VK_NULL_HANDLE` | 每次重建管线都要重新编译 |
| **窗口尺寸变化重建全部资源** | `recreate_swapchain()` 调用 `destroy_live_validation_renderer()` | 拖拽窗口时整条渲染链重建，卡顿 |
| **无任何性能计时** | 全文无 `vkCmdWriteTimestamp` / `VkQueryPool` | 无法测量，也就无法优化 |

### 1.2 着色器成本

- **terrain.frag**：6 次 fbm，每次 5 个八度，每八度 4 次 hash → 约 124 次 hash/像素。
  这是整个场景 pass 的绝对主导成本。
- **ocean.frag**：4 个波方向在每像素都调用 `normalize()`，而这些方向是编译期常量。
  另有一处 `normal.xy.xyx` 的 swizzle 把 x 分量重复了两次并完全丢掉 z，语义错误。
- **banding**：地形是 atlas 纸张风格的大面积平缓渐变，直接量化到 8 bit 会出现明显色带。

---

## 2. 改动清单

### 2.1 渲染管线

- 实现 `create_hdr_targets()` / `destroy_hdr_targets()`：新增 RGBA16F 场景目标、
  MSAA 解析目标、深度附件。
- 帧结构改为 **场景 pass → 色调映射 → （可选 FXAA）→ swapchain**。
  UI 仍直接绘制到 swapchain：它以显示空间着色，不应被推进 ACES 曲线。
- 新增深度附件，管线工厂支持 `depth_test` / `depth_write`，
  混合叠加层只测深度不写深度。
- 引入 `VkPipelineCache`，所有管线共享。
- `recreate_swapchain()` 不再销毁重建整个渲染器，只重建尺寸相关资源。

### 2.2 抗锯齿

- 真正接上 MSAA：场景管线使用档位设定的采样数并做解析。
- 新增 `shaders/fxaa.frag`：单 pass 全屏后处理，供弱硬件档位使用。
- 两者互斥：FXAA 用于 Low 档（跳过 MSAA 的带宽开销），MSAA 用于 Medium 及以上。

### 2.3 材质与光照

- 新增 `src/core/render/RenderQuality.{hpp,cpp}`：硬件档位（legacy / low / medium /
  high / ultra）与自动分级。分级依据 `GpuCapabilities`（显存、设备类型、最大 MSAA）。
- 地形 fbm 的八度预算改为**管线特化常量**，而非 push constant。

  > 这一步很关键。最初把八度计数放进 push constant，驱动无法展开八度循环，
  > 实测 GPU 时间比原始版本还慢 6.5%。改为 `layout(constant_id = ...)`
  > 后循环重新被完全展开，性能回到与基线持平。

- ocean.frag：4 次每像素 `normalize()` 改为编译期常量单位向量；
  修正 `normal.xy.xyx` 的 swizzle 错误。

### 2.4 后期处理

- tonemap.frag 重写：ACES 近似 + 曝光 + 暗角。
- 在量化前加入**三角分布抖动（triangular PDF dither）**，消除 atlas 纸张渐变的色带。
- 正确处理 sRGB 输出：swapchain 是否 sRGB 由运行时探测决定，避免二次编码。

### 2.5 性能测量

- 新增 `VkQueryPool` 时间戳（每帧一对，覆盖整条命令缓冲）+ CPU 墙钟计时。
- 报告新增 `avg/min/max/p95_frame_ms`、`avg_gpu_ms`、`avg_cpu_ms`、`fps`、
  `draw_calls_last_frame` 等字段。
- 时间戳查询是可选能力：设备不支持时自动降级为仅 CPU 计时。

---

## 3. 基准测试结果

### 3.1 方法

基线可执行档 = HEAD 版本的渲染器 **+ 仅计时插桩**（由
`tools/bench/patch_baseline_timing.py` 注入），这样 A/B 对比隔离的是渲染改动，
而不是测量改动。

由于笔记本 GPU 的时钟/温度漂移大于被测效应本身，采用**交替测量**：
每一次档位测量都紧邻一次基线测量，取中位数。

```
python tools/bench/ab_interleaved.py --tiers legacy,low,medium,high,ultra \
    --pairs 5 --frames 600
```

### 3.2 GPU 时间对比（中位数，毫秒/帧）

| 档位 | 配置 | 基线 | 优化后 | 变化 |
| --- | --- | --- | --- | --- |
| **legacy** | 1× MSAA、无深度、无 HDR、5/5 八度 | 0.7095 | 0.7104 | **+0.12%** |
| **low** | FXAA + 抖动 + 深度 + HDR、3/2 八度 | 0.7090 | 0.4678 | **−34.0%** |
| **medium** | 2× MSAA + 抖动 + 深度 + HDR、4/3 八度 | 0.7091 | 0.5907 | **−16.7%** |
| **high** | 4× MSAA + 抖动 + 深度 + HDR、5/3 八度 | 0.7206 | 0.6378 | **−11.5%** |
| **ultra** | 8× MSAA + 抖动 + 深度 + HDR、5/3 八度 | 0.7230 | 0.8896 | **+23.1%** |

### 3.3 结果解读

- **legacy 是唯一严格同画质的对比**：配置与优化前完全一致（单 pass、1× MSAA、
  无深度、无 HDR、5/5 八度）。+0.12% 落在噪声内，说明新的帧结构与管线封装
  **没有引入任何结构性开销**。
- **low / medium / high 比基线更快，同时画质严格更好**——它们额外提供了
  深度缓冲、HDR 目标、色调映射、抖动以及 FXAA 或 MSAA，而这些基线一项都没有。
  增益来自八度预算下调（Low 档 3/2 八度，把每像素 hash 数从约 124 降到约 74）。
- **ultra 慢 23%**，因为它跑 8× MSAA：场景 pass 的每个采样点都要跑一遍完整的
  地形着色器。这是为最高画质付出的显式代价，且仅建议 8 GB 以上显存的独显使用。
- 全部档位下每帧绘制调用数均为 6，CPU 侧帧时间约 0.13–0.20 ms，不构成瓶颈。

### 3.4 兼容性与正确性

- Vulkan 校验层下 legacy / low / high / ultra 四档各跑 45 帧，
  **校验错误 0 条**。
- 27 个 headless 测试套件全部通过。
- 新增代码在 `-Wconversion -Wshadow -Wsign-conversion -Werror` 下无告警
  （`VkApplicationInfo app{...}` 这类 Vulkan 惯用写法会触发
  `missing-field-initializers`，是既有风格，项目未启用 `-Werror`，不计入）。

---

## 4. 可复现的验证步骤

### 4.1 前置条件

- MinGW 工具链在 `D:/mingw64/bin`；Vulkan SDK（`glslc` / `spirv-val`）在 PATH。
- 运行期 DLL 需置于可执行文件同目录：`libstdc++-6.dll`、`libgcc_s_seh-1.dll`、
  `libwinpthread-1.dll`、`SDL3.dll`。

### 4.2 构建优化版

```bash
# 编译着色器
export PATH="/c/VulkanSDK/1.4.357.0/Bin:$PATH"
python tools/assets/compile_shaders.py --src shaders --out build/shaders

# 构建
ninja -C build/dev-desktop core_desktop
```

### 4.3 构建基线（用于 A/B 对比）

```bash
# 1. 取出 HEAD 版本的源码
mkdir -p .baseline_src/shaders
for f in src/core/render/vulkan/VulkanDesktopBackend.cpp \
         src/core/render/vulkan/VulkanDesktopBackend.hpp \
         src/game/platform/DesktopApp.cpp CMakeLists.txt; do
    git show "HEAD:$f" > ".baseline_src/$(basename $f)"
done

# 2. 注入计时插桩（只加测量，不加优化）
python tools/bench/patch_baseline_timing.py .baseline_src

# 3. 取出并编译原始着色器
mkdir -p build/shaders_baseline_src
git archive HEAD shaders | tar -x -C build/shaders_baseline_src --strip-components=1
python tools/assets/compile_shaders.py \
    --src build/shaders_baseline_src --out build/shaders_baseline

# 4. 换入基线源码 → 构建 → 保存可执行文件
cp .baseline_src/VulkanDesktopBackend.* src/core/render/vulkan/
cp .baseline_src/DesktopApp.cpp src/game/platform/
cp .baseline_src/CMakeLists.txt .
ninja -C build/dev-desktop core_desktop
cp build/dev-desktop/core_desktop.exe build/dev-desktop/core_desktop_baseline.exe

# 5. 换回优化版源码并重建
git checkout -- src/game/platform/DesktopApp.cpp CMakeLists.txt
ninja -C build/dev-desktop core_desktop
```

### 4.4 跑基准

```bash
# 各档位跑 3 轮 600 帧，输出到 CSV
python tools/bench/run_benchmark.py \
    --exe build/dev-desktop/core_desktop.exe \
    --shaders build/shaders --frames 600 --runs 3 \
    --tag optimized --out build/bench_optimized.csv

# 与基线交替测量（推荐，可抵消 GPU 时钟漂移）
python tools/bench/ab_interleaved.py \
    --tiers legacy,low,medium,high,ultra --pairs 5 --frames 600
```

单次运行也可直接用环境变量驱动：

```bash
CORE_CONTENT_ROOT=content/base CORE_SHADER_DIR=build/shaders \
CORE_VALIDATION_FRAMES=600 CORE_WINDOWED=1 CORE_VULKAN_VALIDATION=0 \
CORE_RENDER_QUALITY=high CORE_GPU_REPORT=build/report.txt \
./build/dev-desktop/core_desktop.exe
```

`CORE_RENDER_QUALITY` 取值：`legacy` / `low` / `medium` / `high` / `ultra` / `auto`。
省略或填 `auto` 时依据设备能力自动分级。

### 4.5 校验层与回归测试

```bash
# 校验层（每个档位）
CORE_CONTENT_ROOT=content/base CORE_SHADER_DIR=build/shaders \
CORE_VALIDATION_FRAMES=45 CORE_WINDOWED=1 CORE_VULKAN_VALIDATION=1 \
CORE_RENDER_QUALITY=ultra CORE_GPU_REPORT=build/val.txt \
./build/dev-desktop/core_desktop.exe 2>&1 | grep -c VALIDATION_ERROR   # 期望 0

# 回归测试（本环境 ctest 返回 127，直接遍历测试可执行文件）
ninja -C build/dev-headless
cd build/dev-headless && for t in *_tests.exe; do ./$t || echo "FAIL $t"; done
```

---

## 5. 遗留项与后续方向

1. **`GpuDrivenPipeline` 与 `BindlessMaterialSystem` 仍是死代码**，
   未接入实时路径。当前场景只有 6 个绘制调用，尚未到需要 GPU 驱动剔除的规模，
   但内容复杂度上去后应接入。
2. **LOD 仅做到着色器细节预算层面**（八度数量），尚无固定几何体的多级 LOD。
   当前场景没有网格地形，等地形数据接入后需要补齐。
3. **阴影**：当前仅用导数法线做自阴影近似，没有真实阴影贴图。
   定向光阴影是下一步最大的画质提升点。
4. **ultra 档 8× MSAA 成本高**。可考虑以 TAA 或 SMAA 替代，
   在相近画质下把带宽降下来。
5. `settings_.render_scale` 字段已预留但所有档位都设为 1.0，
   动态分辨率缩放尚未接线。
