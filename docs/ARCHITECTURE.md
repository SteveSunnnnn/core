# Core 引擎架构

最后校准：2026-09-01，对应 Core 1.0.0 纯引擎构建之后。

本文是 Core 的**顶层权威文档**：职责边界、依赖方向、数据流和"什么还没接通"。
单个子系统的内部契约放在领域文档里（见文末索引），构建与验证的真实状态以
`FINAL_ENGINE_AUDIT.md` 为准。取代了旧版 `ARCHITECTURE.md`（0.1 提案版）、
`ENGINE_ARCHITECTURE.md`（解耦边界稿）、`ENGINE_GAME_BOUNDARY_AUDIT.md` 与
`LOGIC_LAYER_1_0.md`。

## 1. 使命

Core 只为一类游戏服务：全球尺度大战略模拟。它的设计压力来自四个约束——
十万级模拟群体、数千地理区域、稠密经济依赖、可连续缩放的三维地图，同时要求
mod 可以不改 C++ 就重写规则。任何不直接服务于这个品类的通用能力都不进引擎
（"no generic-engine tax"）。

## 2. 架构法则

法则仍然是这十条，但每条后面补上"现在由什么强制"。

| # | 法则 | 当前的强制手段 |
|---|---|---|
| 1 | 模拟不依赖渲染，渲染不得改写世界 | 后端只接受紧凑 payload：`VulkanDesktopBackend::submit_ui` / `submit_living_instances` / `submit_map_overlay` / `set_world_political_state` |
| 2 | 玩家与 UI 行为一律变成确定性 Command | `core/simulation` 命令校验 + world checksum |
| 3 | 运行时数据面向数据布局，强类型 ID 索引稠密 SoA | `core/base/StrongId.hpp`、各 `*Store` |
| 4 | 内容是数据不是 C++ | 外部内容根目录通过 VFS + CoreScript 装载，`src/core` 里出现国家硬编码视为缺陷 |
| 5 | 昂贵派生状态反应式重算 | ModifierGraph 脏传播，50,000 节点链式回归防栈递归 |
| 6 | 模拟工作表达为依赖 DAG | `TickScheduler` 波次，未标 `ParallelSafe` 保守串行 |
| 7 | 确定性是功能 | 命名 RNG 流、命令定序、三年多 worker 连续 + 周期性存读档回归 |
| 8 | 昂贵 GIS 离线做完 | `core_world_compiler` + `.coreworld`；运行期不解析 shp/GeoJSON |
| 9 | Mod 是一等公民 | `core/content/ModManifest` + `ModVersion`、overlay VFS、确定性 load plan |
| 10 | 引擎不认识某一款游戏 | 配置期守卫：`src/core/**` 里出现 `#include "game/..."` 直接 `FATAL_ERROR` |

还有一条同样是 configure 期强制的禁令：`src/core` 源码不得再引用
`set_world_map_layers` 或 `world_map(_ids|_terrain|_height).coreimg`。全图栅格
atlas 这条路径已经被正式判死，地图只有一个权威来源——`.coreworld`。

## 3. 物理分层

```text
src/core    -> core_runtime (引擎能力，唯一可分发库)
src/apps    -> core_cli / core_world_compiler / core_world_inspect / *_cooker (引擎工具)
shaders/    -> GLSL 源码，由外部 glslc 编译为 SPIR-V
tools/      -> GIS、资产、shader、manifest 与 Windows 引擎工具
thirdparty/ -> 第三方依赖
```

依赖方向严格单向：引擎工具只能依赖 `core_runtime`。引擎不包含游戏组合层或随仓库
附带的作者化内容。


## 4. 权威图

```text
GIS / DEM / 作者化空间层  (外部数据；--provinces 必填，见 tools/core_gis_compile.py:1183)
        |
        v
  core_world_compiler  -->  只读 world.coreworld
        |                        |
        v                        v
  WorldTopology              流式地图页面
  (纯解码，无模拟)                 |
        |                        v
        v                  WorldMapPageSource  <- 拾取与渲染共用同一页面契约
   WorldBootstrap                |
        |                        v
        |                  WorldMapPageStreamer (准入/CPU 常驻/有限待上传)
        v                        |
     World  <---- WorldContentBinder        v
  (可变模拟状态)      ^                VulkanWorldMap
                      |                (只做 atlas 分配、staging、barrier)
        DefinitionDatabase + EconomyDefinitions
        (只编译不可变作者定义，不知道 World 存在)
```

三条最容易踩坏的红线：

- `DefinitionDatabase` 不 include `World`、不创建模拟实体。把 script key 解析成
  运行期 ID、生成国家/建筑/POP、按日期落历史，**只有** `WorldContentBinder`
  能做（`src/core/content/WorldContentBinder.hpp:16`）。
- `WorldPack` 拥有地理、拓扑、页面与 placement，不得变成第二个国家/经济数据库。
- `WorldMapPageStreamer` 拥有地图侧的有界 planner/cache；后端可以拥有 GPU 常驻，
  但不得重新实现 pack 解码或页面淘汰。

## 5. 目录归属与允许依赖

| 目录 | 拥有 | 允许依赖 |
|---|---|---|
| `core/base` `core/memory` `core/io` `core/jobs` | ID、hash、受限内存、文件访问、调度 | 标准库与更低层 Core |
| `core/worldpack` | `.coreworld` 格式、索引、压缩、元数据 | base、io |
| `core/world` | 地理、邻接、placement、state-region 视图、拓扑解码、pack bootstrap | base、worldpack、economy 定义接口 |
| `core/economy` | goods / markets / buildings / POP stores 与经济 kernel | base、jobs；`.cpp` 内不得已的跨 store 操作才可及 World |
| `core/simulation` | 可变 `World` 聚合、clock、command、modifier、调度 | economy、world、gameplay 状态 |
| `core/content` | VFS、面向 parser 的定义、localization 入口、显式 content binder | scripting；binder 可及 world/simulation |
| `core/scripting` | 解析、编译、字节码、VM、profiler | base、content 数据 |
| `core/gameplay` `core/ai` `core/research` `core/grand_strategy` `core/warfare` | 各自独立模拟域 | simulation 接口 + 自身域依赖 |
| `core/save` | 存档 codec、tagged section、legacy checksum 兼容 | simulation、economy、scripting |
| `core/render` | 后端无关的渲染数据、地图页面、plan、cache、相机 | base、worldpack、UI 契约；snapshot builder 是唯一模拟桥 |
| `core/render/vulkan` | Vulkan 句柄、命令录制、GPU 上传 | render 契约 + Vulkan |
| `core/ui` `core/editor` | 可复用 UI/编辑器机制 | render 契约、scripting、显式 world 服务 |
| `core/runtime` | `CoreEngine` 组合门面、内容安装 | 全部运行期域（顶层） |

## 6. 引擎模块具体拆分

引擎"看起来不一样"的来源是以下物理拆分，全部已核实存在（部分进入 `core_runtime`
的行见 `CMakeLists.txt`）。
放代码的依据是它拥有什么数据，而不是第一个用到它的功能。

**世界与地图**
- `world/WorldTopology.*` 拥有纯 `.coreworld` 解码；`WorldBootstrap.*` 只把不可变
  记录组合成 `World`；`WorldBootstrapWire.*` 拥有二进制 wire 序列化。
- `world/WorldStaticLayers.*` 拥有静态层目录、建筑区域与资源分布表；渲染侧对应
  `render/map/WorldStaticGeometry.hpp` 与 `WorldStaticLayerSource.*` 按需解码河流/运输。
- `world/StateRegionIndex.hpp` 是只读的地理分组层，今天仍是 1:1 兼容映射。
- `economy/CountryStore.*` 拥有国家列与财政操作，`simulation/World.*` 只剩聚合
  checksum 与世界组装。
- `render/map/WorldMapPage.hpp`（解码后 payload）/`WorldMapPageSource.*`（唯一 CPU
  读取器）/`WorldMapPageStreamer.*`（准入与常驻）/`WorldMapPageKey.hpp`（身份）四分。
- `render/map/VectorMapPipeline.*` + `VectorMapTypography.*`：矢量边界、晕滃线、
  罗盘线与地图排版，作为引擎能力提供给使用方。

**渲染后端**（`VulkanDesktopBackend` 仍是门面，实现按职责落文件）
`VulkanDevice`（实例/设备/swapchain 图像/帧同步）、`VulkanFrame`（swapchain 重建、
帧命令、呈现）、`VulkanScene`（政治页面网格与 living 实例的场景通道）、
`VulkanPostProcess`（tonemap + FXAA）、`VulkanUi`（UI batch 与动态旗帜模块通道）、
`VulkanUiStaging`（draw-list 转换、字形兜底、buffer 增长）、`VulkanUiResources`
（字体度量与图像上传）、`VulkanLivingMap`（instance buffer staging）、
`VulkanRuntimeRenderer`（pipeline/descriptor 组装与 teardown）。
`ui/UiRenderTypes.hpp` 是 UI 到后端的最小契约——Vulkan 头文件不再按值持有
`FontAtlas`，也不再 include 整套 Strategy UI 实现。

**脚本与经济**
- `scripting`：`ScriptProgramDatabase`（编译产物存储）、`ScriptCompiler` 与
  `ScriptCompilerScoped`（作用域遍历与顶层程序编译）、`ScriptVm` 与
  `ScriptVmValues`（作用域执行与脚本值求值）、`ScriptProfiler` 各自独立；
  `ScriptProgram.cpp` 有意只保留为兼容翻译单元。
- `economy`：`EconomyPopulationPhases.cpp`（人口 gathers/就业/生产/消费）与
  `EconomyMarketPhases.cpp`（贸易/价格/结算/投资池/建造）从 `EconomySystem.cpp`
  拆出，后者只保留资产负债表、scratch/index 初始化与阶段编排。
- `grand_strategy`：`GrandStrategyWeekly.cpp`（周政治/外交/战争/军队/机构/科技/抵抗
  转移）与 `GrandStrategyValidation.cpp`（校验、checksum、内存计量）从 store 拆出。

**存档与 UI 运行时**
- `save`：`SaveGame.cpp` 为编解码与 wire 格式主入口，`SaveGameScriptSections`
  （tagged gameplay/AI/notification）、`SaveGameWorldSections`（市场/金融/地理/slot）、
  `SaveGameLegacy`（历史 checksum 兼容）、`SaveGameRuntime`（运行期 checksum 组合）。
- `ui`：`ScriptedGui`（schema/蓝本模型）/`ScriptedGuiCompiler`（解析到蓝本）/
  `ScriptedGuiValues`（呈现值）/`ScriptedGuiLayout`（节点尺寸与子布局）/
  `ScriptedGuiPainter`（语义绘制与 tooltip）/`ScriptedGuiRuntime`（retained tree）；
  `StrategyUiPrimitives`（几何与 batching）/`StrategyUiComponents`（主题面板与按钮）/
  `StrategyUiAnalytics`（进度、图表、经济专有绘制）/`StrategyUi`（装饰、控件、文本、命中）。
- `localization/LocalizationStore` 拥有查表、插值与分词，`ui/LocalizationRichText`
  是唯一面向 draw-list 的富文本适配器。
- `content/ModVersion.cpp` 拥有语义化版本解析与比较，`ModManifest.cpp` 只保留
  manifest 语法与 load plan 构造。

## 7. 一次新游戏启动的数据流

```text
外部 GIS / 作者化内容
  -> core_world_compiler -> 外部 .coreworld
       -> WorldTopology（纯解码）
       -> WorldBootstrap -> World（可变模拟状态）
外部 CoreScript 内容 -> VFS -> DefinitionDatabase / WorldContentBinder -> World
引擎工具 -> core_runtime 的只读 world、内容与资产接口
```

关键点：外部作者化内容不进入引擎源码；`.coreworld` 是地图运行时的唯一权威来源，
而 `WorldTopology`、`WorldBootstrap` 和 `WorldContentBinder` 保持职责分离。

## 8. 已知未接通（不要当成已完成）

这些是解耦后暴露出的真实边界，写在这里避免文档比代码乐观：

1. **仓库不附带作者化世界包。** `core_world_compiler` 的 `--provinces` 是必填外部输入，
   运行时只能加载外部提供的 `.coreworld`，世界包无法只靠引擎源码重建。
2. **矢量地图管线仍是引擎能力。** `VectorMapPipeline`、三档边界 payload 与
   `WorldMapPageStreamer` 已进入 `core_runtime`，具体游戏客户端接入由引擎使用方负责。
3. **`RenderSnapshot` 是定义但未采用的边界。** `RenderSnapshotData.hpp` 与
   `RenderSnapshotBuilder.cpp` 目前只被引擎工具使用；快照通道要成为真正的同步
   边界，需要先扩出 ownership/mode/living payload。
4. **`StateRegionIndex` 仍是 1:1 兼容层。** 德国式分裂州与州级经济查询需要
   world compiler 先产出共享 region key。
5. **市场拓扑缺一层。** market capital、港口/贸易中心与航线呈现模型尚未成型；
   经济目前主要按市场分区，单个巨型世界市场无法用满所有核心。
6. **财政内部表示仍是 binary64。** 国家库藏在经济边界处适配到定点 milli-units，
   权威账户应改成定点（见 `FINAL_ENGINE_AUDIT.md` 风险节）。
7. **当前 MSVC 验证缺口。** 引擎源码清理后，`src/core` 与 `src/apps` 的 MSVC
   编译状态需要重新复验。

## 9. 参考模式与下一步拆分

Victoria 3 的公开 modding 材料把四件事分开，这是 Core 借鉴的**权威分离**，不是要
复制它的资产或私有实现：省份地图提供稳定地理身份；state region 聚合省份，政治州
在 region 内创建并可被多个所有者拆分；国家定义与 state/building/POP 历史写在内容
文件里而不是渲染器里；市场与贸易中心是地图之上的经济抽象，由港口、建筑与人口
影响而非直接画在地图上。
参考：[Dev Diary #60 – Modding](https://www.paradoxinteractive.com/games/victoria-3/news/victoria-3-dev-diary-60-modding)、
[Dev Diary #57 – The Journey So Far](https://www.paradoxinteractive.com/games/victoria-3/news/dev-diary-57-the-journey-so-far)、
[Dev Diary #38 – Trade Routes & Tariffs](https://www.paradoxinteractive.com/games/victoria-3/news/dev-diary-38-trade-routes-tariffs)。

按这个分离，下一批拆分的既定顺序是：

1. 引入显式 `StateRegionId` 成员关系，同时保留 `StateId` 表达政治 subdivision。这是
   德国式分裂州与州级经济查询的前置条件。
2. 在地理之上加市场拓扑层（`MarketCapital`、港口、贸易中心、运输可达性），由经济
   与地图 lens 查询，不嵌进省份渲染。
3. ~~继续拆 Vulkan 后端~~ 已完成（§6 渲染后端）；保持门面为短命 facade，直到出现
   更强的后端接口值得立。
4. 把 `RenderSnapshot` 扩成 map ownership / mode / living-instance payload，
   这是在真实 POP 数量下提高模拟吞吐所需的同步边界。

这些步骤的目标始终是"可复用的框架"：不把国家数值或事件内容搬进 C++，也不在外部
内容包到位之前重写可用的 `World` 聚合。

## 10. 完成定义

引擎框架完成的判据是全部四条同时成立：`.coreworld` 读取、拓扑到 `World` 的组装、
外部 CoreScript 内容绑定和存档/校验契约均保持稳定；工具只能通过公共 Core API
工作，不能把作者化数据写回引擎源码。

当前结论：**纯引擎框架，可供外部游戏项目接入，不包含可发布游戏内容。**

## 11. 文档索引

顶层：本文（边界与权威）、`FINAL_ENGINE_AUDIT.md`（验证状态，冲突时以此为准）、
`ROADMAP.md`（能力计划）。

领域契约（仍是各自子系统的权威，标题已去版本号）：
确定性 `DETERMINISM.md`；调度 `JOB_SYSTEM.md`；脚本 `CORE_SCRIPT.md`；
内容契约 `SCRIPT_FIRST_CONTENT.md`、`MOD_RUNTIME.md`；
逻辑域 `ECONOMY_ARCHITECTURE.md`、`FINANCE_BANKING.md`、`RESEARCH_ARCHITECTURE.md`、
`NOTIFICATION_RUNTIME.md`；
地图与渲染 `WORLD_COMPILER.md`、`POLITICAL_MAP_ARCHITECTURE.md`、
`LIVING_MAP_ARCHITECTURE.md`、`MAP_MODE_ARCHITECTURE.md`、`TERRAIN_ARCHITECTURE.md`、
`VULKAN_BACKEND.md`、`GPU_TIERS.md`；
UI `SCRIPTED_GUI.md`、`UI_THEME.md`；
性能 `PERFORMANCE_BUDGET.md`、`PERFORMANCE_DESIGN.md`。

同一主题只允许一个权威文档；新增总览类文档前，先看能否并入本文或对应领域契约。
