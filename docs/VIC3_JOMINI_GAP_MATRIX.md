# Core 1.0 — Victoria 3 / Jomini 功能缺口矩阵

> 审计日期：2026-08-26（Asia/Shanghai）  
> 审计方式：仅本机、只读、静态抽样与既有日志观察；未启动或控制 Victoria 3。  
> 目标：提取可由公开内容脚本、definitions、GUI 资源和运行日志观察到的**抽象引擎能力**，用于评估通用 Core 引擎；本文不复现专有源码、内容文本、历史数据、平衡数值或具体事件/角色/国家内容。

## 1. 审计边界与快照

### 1.1 本地安装与用户数据

| 项目 | 本地证据 |
|---|---|
| Steam App | `529340` |
| 安装目录 | `<VICTORIA_3_INSTALL>`（本地只读安装） |
| 游戏内容根 | `<VICTORIA_3_INSTALL>/game` |
| 用户数据根 | `<VICTORIA_3_USER_DATA>`（本地只读用户数据） |
| 本地版本快照 | launcher `1.13.11`；Steam manifest build `24799966` |
| 内容选择结构 | `content_load.json` 暴露 enabled mods、disabled DLC、enabled UGC 三类集合；本次只统计集合基数，不记录 mod 标识 |
| DLC 描述结构 | 17 个 `.dlc` 描述文件；可观察到路径、校验、多人同步、存档兼容、平台标识等元数据类别 |

未读取存档负载、崩溃 dump、launcher SQLite、console history 或任何用户命令内容。只统计这些目录/文件的存在、数量和大小；日志仅作结构与抽象系统分类，不摘录日志正文。

### 1.2 基础内容规模

以下均为 `game` 基础层的只读统计；DLC 资产层另有 2,842 个文件，本矩阵不把 DLC 美术包当作基础脚本语料重复计算。

| 根目录 | 文件数 | 字节数 | 可观察职责 |
|---|---:|---:|---|
| `common` | 3,091 | 25,208,910 | definitions、规则、scripted 内容、历史初始化 |
| `events` | 328 | 5,171,421 | 事件内容运行时输入 |
| `gui` | 207 | 6,028,997 | 声明式 GUI、布局、快捷键 |
| `map_data` | 28 | 98,909,735 | 地图定义、栅格、状态区域和拓扑输入 |
| `localization` | 1,877 | 116,637,674 | 多语言内容 |
| `gfx` | 19,159 | 10,160,670,804 | 模型、材质、贴图、动画和渲染资产 |
| `content_source` | 62 | 5,371,790 | 内容源/工具链输入 |

`common` 中有 75 个 definition 类别自带 Markdown schema/readme（合计 189,940 字节）。本文只将其存在视为“定义格式有显式契约”的证据，不复制其中说明。

### 1.3 代表性 definition 目录

| 领域 | 本地目录及文件数（含同目录 schema 文档） |
|---|---|
| CoreScript / modifier | `defines` 9；`modifier_type_definitions` 16；`static_modifiers` 68；`script_values` 30；`scripted_triggers` 25；`scripted_effects` 40；`scripted_lists` 1；`scripted_guis` 4；`scripted_buttons` 51；`scripted_progress_bars` 15 |
| 内容运行时 | `decisions` 34；`journal_entries` 173；`messages` 7；`map_notification_types` 1；另有 `events` 328 |
| AI | `ai_strategies` 5；`objectives` 3；`objective_subgoals` 6；另有战略区域 stance 定义 |
| Politics | `interest_groups` 8；`parties` 12；`laws` 26；`institutions` 2；`political_movements` 8；`government_types` 10 |
| Diplomacy | `diplomatic_actions` 49；`diplomatic_plays` 2；`treaty_articles` 35；`subject_types` 1；`war_goal_types` 35；另有 power bloc、lobby、country formation 定义 |
| Warfare | `combat_unit_types` 1；`commander_orders` 3；`mobilization_options` 2；`naval_mission_types` 2；`ship_types` 2；另有 battle condition、unit group、ship modification、formation flag 定义 |
| Economy | `goods` 2；`buildings` 15；`production_methods` 16；`company_types` 23；`pop_needs` 1；另有 building/PM group、charter、prestige goods 定义 |
| Society | `pop_types` 16；`cultures` 1；`religions` 1；`discrimination_traits` 5；另有 trait group、social class/hierarchy 定义 |
| Research | `technology` 5 |

文件数不是系统深度的直接度量，但能证明这些领域是独立、可扩展、数据驱动的 definition 类别，而不是单一游戏主循环中的硬编码分支。

## 2. 脚本与 GUI 抽样

### 2.1 语料与计数方法

- 脚本语料：`common`、`events`、`notifications`、`data_binding`、`interface`、`map_data` 下 3,365 个 `.txt`，共 31,870,174 字节。
- GUI 语料：206 个 `.gui/.layout/.shortcuts`，共 6,028,375 字节。
- 统计是大小写不敏感的词法/行首模式计数，不是 Jomini AST 解析结果；注释、嵌套和同名内容字段可能造成少量高估。因此数字只用于确认能力是否被广泛使用，不用于还原专有内容。

### 2.2 CoreScript 可观察语法面

| 抽象能力 | 命中次数 | 命中文件 | 结论 |
|---|---:|---:|---|
| 具名 scope 引用 | 16,859 | 648 | event target / saved scope 式跨块引用是常规能力 |
| scope 保存 | 6,006 | 533 | 执行帧需要可命名、可覆盖的 scope binding |
| ROOT/THIS/PREV 式链式导航 | 2,238 | 320 | 需要父/根/前序上下文和属性链 |
| `any_*` 迭代 | 14,962 | 740 | 集合查询是核心热路径之一 |
| `every_*` 迭代 | 1,844 | 344 | 批量 effect/全称条件需要稳定顺序 |
| `random_*` 迭代 | 3,978 | 500 | 随机选择必须接受重放稳定 seed |
| `ordered_*` 迭代 | 302 | 103 | 需要稳定排序、tie-break 和截断 |
| iterator `limit` | 19,626 | 857 | 迭代过滤不能只靠原语内部硬编码 |
| iterator `order_by` | 292 | 101 | 排序值应由 scripted value 提供 |
| 变量写操作 | 4,595 | 579 | 同时存在普通、临时与全局变量类别 |
| 变量条件测试 | 5,546 | 566 | 未定义、存在性、比较语义需要明确 |
| 变量引用 | 1,426 | 213 | 值表达式需要变量寻址 |
| 临时值保存 | 26 | 6 | scope 之外还需要 typed value binding |
| variable-list 修改 | 43 | 11 | 列表本身是可变脚本状态 |
| list 迭代 | 118 | 13 | typed collection 需进入统一 iterator IR |
| modifier 增删查 | 7,135 | 543 | modifier 是跨领域通用运行时，不应仅是数值工具类 |
| modifier block | 1,649 | 162 | 需要条件式/聚合式 modifier 表达 |
| 参数占位 | 235 | 14 | scripted trigger/effect 需要参数化调用 |
| weighted/random list | 432 | 134 | 权重值、fallback、稳定 RNG 是内容语义的一部分 |
| AI 权重/效用字段 | 4,767 | 282 | 内容定义普遍携带 AI 评估，而非独立硬编码 AI 表 |
| define 引用 | 630 | 93 | 常量表需类型化、命名空间化并进入内容 hash |

此外，独立的 scripted trigger/effect/value/list/GUI/button/progress-bar/rule 目录表明“脚本可复用单元”不仅服务事件，还横跨 definitions、AI 与 UI。

### 2.3 Event / Decision / Journal / Notification 抽样

| 语料 | 抽象结构证据 |
|---|---|
| 328 个 event 文件 | 约 4,093 个 trigger、2,169 个 immediate、4,840 个 option、724 个 AI 权重字段；同时大量使用 scope 保存、变量、modifier、延时/持续时间和通知挂钩 |
| 34 个 decision 文件 | 可见性/可用性、执行 effect、AI 评估、冷却/时间、scope/变量和通知挂钩均有使用 |
| 172 个 journal 文本文件 | completion、failure、on-action、进度、分组、时间、默认 pin/可见性、通知、scope/变量均有独立语法证据 |
| message/notification 语料 | notification type、分组、图标/纹理、持续时间和交互/路由元数据独立于事件日志存在 |

这意味着“将事件写入 gameplay log”不能替代通知系统；Journal 也不能只建模成带 completion 的长寿命 Event。

### 2.4 Scripted GUI 抽样

| 抽象能力 | 命中次数 | 命中文件 |
|---|---:|---:|
| 类型/模板定义 | 57 | 43 |
| 模板复用 | 8,221 | 177 |
| 常规 widget/container | 5,754 | 162 |
| 列表/网格/滚动容器 | 1,584 | 122 |
| chart/graph 类 widget | 6 | 6 |
| data context | 1,875 | 118 |
| 属性/函数 binding（词法估计） | 27,732 | 181 |
| visible 条件 | 7,992 | 150 |
| enabled 条件 | 814 | 95 |
| click handler | 3,427 | 137 |
| tooltip | 3,727 | 142 |
| animation/state | 1,448 | 101 |
| shortcut | 78 | 31 |

可观察目标是：声明式模板 + typed data context + 响应式属性 + 命令回调 + tooltip + 虚拟化容器 + 图表，而不只是 immediate-mode quad/text 绘制。

## 3. 既有日志可观察行为

用户 `logs` 目录有 60 个文件、约 10.1 MB，采用 current + `.1`–`.5` 轮换。只根据文件名、大小、行数和抽象关键词做结论：

| 日志证据 | 可确认的抽象能力 | 限制 |
|---|---|---|
| `dedicated_server*.log` 非空且持续轮换 | 有独立 simulation/tick 服务端观测流 | 不据此推断网络协议或多人完整性 |
| `debug/error/warning/game*.log` 非空 | 内容加载、脚本、地图、UI 等问题有分级诊断与轮换 | 计数受当前启用内容影响，不作为原版质量判断 |
| `gui.log` 非空 | UI 交互/状态有独立诊断通道 | 不摘录具体交互或路径 |
| `custom_automated_stats*.log` 非空 | 自动化统计/运行指标有专用输出 | 不读取具体指标值 |
| `code_revisions*.log`、`system*.log` 非空 | build/revision 与系统配置可追踪 | 仅确认通道存在 |
| `ai.log`、`checksum.log`、`ecs.log`、`graphics.log`、`multiplayer.log`、`ui_animation_stats.log` 等通道存在但本次为空 | 日志路由预留了 AI、checksum/OOS、ECS、图形、多人和 UI 动画等域 | 空日志不能证明相应运行时已在本次会话执行 |

对 Core 的直接要求是结构化、可筛选、带稳定对象 key 和 tick 的诊断事件；脚本调用栈、AI 决策 trace、内容来源链、子系统 checksum 差异和 UI binding 错误应能被工具消费，而不是只写自由文本。

## 4. 状态与优先级定义

- **Complete**：Core 已提供可复用、数据驱动的通用运行时，覆盖本地证据所代表的抽象能力，并满足稳定 key、save/load、checksum、确定性测试要求。
- **Partial**：存在可运行基础，但范围、内容接口、状态契约、系统深度或测试不足。
- **Missing**：没有集成运行时；只有 ID/record/绘制原语/未来注释不算实现。
- **P0**：阻塞内容可表达性、权威状态安全、存档/多人确定性或多个上层系统。
- **P1**：大型历史策略游戏的核心玩法深度。
- **P2**：生产 UI、内容工具、可观测性和 3D 完成度。
- **P3**：扩展性、编辑体验或视觉/性能精修。

## 5. Vic3/Jomini → Core 缺口矩阵

### 5.1 CoreScript 与通用内容运行时

| 能力 | Core 证据与现状 | 状态 | 优先级 | 建议的通用接口 / 验收点 |
|---|---|---|---|---|
| Scope 类型与导航 | `ScopeType`、`ScopeResolver`、`ScriptExecutionContext` 已有 Country/State/Province/Pop/Market 与 ROOT/FROM/PREV/THIS、owner/parent、saved scope；现有类型面远小于外交、政治、战争、公司、角色等内容域 | Partial | P0 | `ScopeTypeRegistry` + `ScopeHandle(type, stable/runtime id)` + 每类型 resolver/parent/children descriptor；编译期验证合法链；运行时只持紧凑 type/op ID |
| Iterator / collection traversal | 已有 any/every/deterministic-random 的基础 scope traversal；本地语料还广泛使用 ordered、limit、order_by、count 和 typed list | Partial | P0 | 统一 `IteratorProgram{source, filter, order_value, direction, offset, count, mode}`；稳定 key tie-break；高基数域接 CSR/SoA view，不构造临时对象向量 |
| 变量、临时值、event target、typed collection | `ScriptExecutionContext` 已有词法调用帧参数/变量、稳定 key 的 event targets、同质 typed collections（scope collection 还校验 Country/Pop 等子类型）和确定性随机计数。活跃 Event/Journal 实例持有该 context，`GCT1` 存档扩展对其有界编码、引用校验、checksum 与原子恢复；完成实例释放 context | Partial | P0 | 补 Date/Duration/全局或领域持久变量、可变 collection/list 的完整操作和更多 scope 类型；将当前 gameplay 专用 codec 收敛到通用 authoritative-store 契约 |
| 参数化 scripted trigger/effect/value | Script 与 scripted value 已支持具名 typed signature，包括 Number/Symbol/Key/Bool/Scope 及 Country/State/Province/Pop/Market 子类型、required 和默认值。全文件加载后 linker 检查未知调用、缺失/多余/重复参数、静态类型/scope 错误、未声明参数引用与调用环；运行时也重新校验外部调用 | Partial | P0 | 补显式返回类型、Date/Duration/Collection 参数、常量折叠与更完整的值表达式；将调用参数进一步压缩为 slot 而非每次组装 vector |
| 表达式和值系统 | 当前保留数值 fast path，并有有限 scripted value source/multiply/add；不足以覆盖变量、属性、聚合、条件值和多领域 modifier | Partial | P0 | 紧凑 typed value bytecode；常量折叠；同 scope 简单路径继续直读 SoA column；复杂表达式才走 VM；禁止热路径字符串反射 |
| Modifier definitions / instances / stacking | `ModifierGraph` 有依赖与 dirty 传播，但未成为 World 权威状态，也未与 scripted modifier、duration、stacking、来源、save/checksum 集成 | Partial | P0 | `ModifierDefinitionRegistry` + SoA `ModifierInstanceStore`；稳定 source/key、起止 tick、stack group、add/mul/cap pipeline；派生值依赖图与实例寿命分离 |
| weighted/random/ordered 语义 | deterministic random iterator 的调用点 salt 已由稳定 program key + 结构路径生成，不依赖 SymbolTable 插入顺序或源码行号；同一调用点用持久 draw counter 产生确定序列。weighted list、ordered select、fallback、权重 trace 仍未完成 | Partial | P0 | 统一 selection IR；RNG key 进一步纳入通用系统 salt/scope stable identity/tick 契约；补跨 worker、存档续跑与内容重排长时回归 |
| on_action、延时与调度 | Event auto update 可轮询；没有通用 typed on-action bus、delayed effect/event queue 与日/月/年 pulse registry | Missing | P0 | `GameplaySignalRegistry` + 稳定排序 subscriber；`ScheduledScriptStore` 进入 save/checksum；触发只写 deterministic command stage |
| 脚本诊断与 profiling | 有 parse/compile/link diagnostics 与 `core_content_check`；parser 拒绝非有限/溢出数值，并有 AST 节点上限与 128 层深度保护；编译器追踪稳定名称哈希冲突；VM 公开调用有非权威工作预算。仍缺调用栈、跨 mod 内容来源链、热点/绑定 trace 和 OOS context diff | Partial | P2 | 结构化 `ScriptDiagnostic{package,path,line,object,key,scope,stack}`；开发构建采样 primitive/iterator 成本；发布热路径零字符串查找 |

### 5.2 Event / Decision / Journal / Notification

| 能力 | Core 证据与现状 | 状态 | 优先级 | 建议的通用接口 / 验收点 |
|---|---|---|---|---|
| Event runtime | `GameplayDefinition/Instance` 支持 potential、allow、opening effect、option allow/effect、cooldown、auto-trigger 与选择日志。每个实例现有单调 64-bit `GameplayInstanceId`；待选事件保留 ROOT/FROM、变量、targets、collections 和 RNG counters，并进入 save/checksum。定义按 stable key 恢复，不依赖注册顺序 | Partial | P0 | 补 queued/delayed/hidden/timeout/chaining、显式 expiry 和 AI option policy；将实例存储从 AoS vector 迁到通用 stable-instance store |
| Decision runtime | 已有 allow/effect/cooldown 和执行入口 | Partial | P1 | 独立 shown/possible/valid/cost/confirm/effect/AI 评估；按 `(definition key, scope stable key)` 存 cooldown；UI query 不得修改权威状态 |
| Journal runtime | 已有 potential/completion/effect 与持久 instance；活跃 Journal 保留通用 `ScriptExecutionContext`，completion/effect 可观察打开时保存的 targets/variables/collections，存档续跑一致 | Partial | P0 | 补 failure/timeout/progress/current-goal/on-open/on-complete/on-fail/pulse、group/pin/visibility；支持明确的同 definition 多实例策略 |
| Notification runtime | 已有数据驱动 `notification` definition 与独立 runtime：title/body localization key、icon/category、low–critical 优先级、stack/suppress/replace 去重、lifetime、source/map target、potential 及带 allow/effect 的 actions。单调 64-bit instance ID、状态机、稳定 definition/action key 的 `NTF1` save/load、checksum、原子恢复与续跑回归均已落地 | Partial | P1 | 当前仍缺 typed localization arguments、声音/portrait payload、通用 gameplay-signal 路由、UI inbox/toast/map-focus 集成、用户过滤与非权威展示分层 |
| 内容呈现元数据 | Notification 已使用 localization/asset/category stable key；Event/Decision/Journal 的通用 localization、portrait/background/icon/sound/tooltip payload 仍未建模 | Partial | P2 | 内容只存 localization/asset key 与 typed presentation arguments；渲染/UI 层解析，不把专属游戏概念硬编码进 Core |

### 5.3 AI

| 能力 | Core 证据与现状 | 状态 | 优先级 | 建议的通用接口 / 验收点 |
|---|---|---|---|---|
| Utility action 与长期 plan | `UtilityAiEngine` 有 action、plan、priority、completion、commitment、cooldown、稳定 tie-break 与 save/checksum；这是有效基础，但 plan 仍是 action allow-list | Partial | P0 | `GoalDefinition`/`GoalInstanceStore` + decomposition graph + success/failure/replan 条件 + commitment + deadlines；所有 key 按内容 stable ID 恢复 |
| Goal decomposition / blackboard | 没有目标树、子目标依赖、资源预约、预测状态、失败归因或决策 trace | Missing | P0 | `PlannerContext` 只读快照；`PlanProposal` 输出 commands/reservations；稳定 best-first/HTN 选择；有扩展预算和 deterministic trace hash |
| 经济/建设/贸易 AI | 没有需求预测、建设队列、投入品约束、贸易/船队、预算或投资规划器 | Missing | P1 | 各域 planner 通过统一 `PlanningService` 读取 query API、提交 reservation/command；不能直接跨域写 store |
| 外交/政治 AI | 小量脚本 primitive 可改善关系、建盟、开 play、立法；没有谈判、联盟组合、政府/选举/运动评估 | Missing | P1 | proposal scoring、red-line、obligation/offer bundle、coalition/government solver；内容提供 utility，C++ 提供通用搜索与约束 |
| 战争/海军 AI | 没有 theatre/front assignment、兵力/补给预算、目标优先、舰队任务和入侵规划 | Missing | P1 | theatre goal → front/fleet task decomposition；路径/补给/风险 query；命令按稳定 theatre/front key 排序提交 |
| AI 可观测性 | 只有结果状态/checksum，无生产级 why/why-not trace | Missing | P2 | 每次决策记录候选、硬约束、utility 分解、选中原因、预算与 stable tie-break；trace 可关闭且不进权威 checksum |

### 5.4 Politics / Diplomacy / Warfare

| 能力 | Core 证据与现状 | 状态 | 优先级 | 建议的通用接口 / 验收点 |
|---|---|---|---|---|
| IG / party / government | `GrandStrategyStore` 有 IG、party、government records；weekly reference tick 仅按支持/approval 聚合 legitimacy/stability | Partial | P1 | definition 与 SoA instance 分离；POP→IG 支持、ideology/trait、party affiliation、government coalition、合法性来源均数据驱动 |
| Elections / government formation | 无选举日程、投票权/选区、席位、coalition negotiation、执政/反对派状态机 | Missing | P1 | `ElectionStore`、`GovernmentFormationStore`；规则插件定义投票制度但通用流程/存档/校验由 Core 提供 |
| Laws / institutions | 有 law hash、简单 enactment progress/support、institution level record；没有 law group/variant、阶段事件、stall/setback、institution investment/effect | Partial | P1 | stable law definition key；enactment phase machine；institution budget/level/effects；与 modifier 和 Journal/Event signal 集成 |
| Movements / revolution | 无 movement、petition、radicalism、support、civil-war/revolution state machine | Missing | P1 | `PoliticalMovementStore` + escalation/de-escalation；revolution 生成参与者、领土、war linkage；所有阶段可脚本化 |
| Relations / treaties | 有 bilateral relation、枚举 treaty、create/break；article 只是单 hash，无实例参数、义务、期限、谈判或多边图 | Partial | P1 | `TreatyStore` + `TreatyArticleInstanceStore`；多参与方、typed article payload、start/end/violation、proposal/ratification/cancel flow |
| Subjects / access / blocs / lobbies | 有 PowerBlocRecord、访问类 TreatyKind 与 ColonyRecord，但没有成员、subject contract、liberty desire、lobby、bloc principle/cohesion runtime | Missing | P1 | 通用 relationship graph；subject/treaty/bloc article 都使用可组合 rule instances；避免为某游戏硬编码枚举爆炸 |
| Diplomatic play / war goals / peace | 有 opening→maneuvering→countdown→war→resolved 和单一 war-goal hash；无参与者阵营、maneuvers、sways、多个目标、支持/中立、peace settlement | Partial | P1 | `DiplomaticPlayStore`、participant/war-goal/offer SoA；每阶段 command set、预算、AI negotiation 与 Event hooks 数据驱动 |
| Colony / unification | Colony 只有线性 progress；无竞争、殖民能力、claim、统一候选/formation 条件与领土转移事务 | Missing | P1 | 通用 claim/formation project；完成时通过 validated transaction 批量迁移 ownership/relations/content flags |
| Army / formation / commander | 只有 country/location/manpower/organization ArmyRecord；无 unit、formation hierarchy、commander、mobilization、orders、reinforcement | Partial | P1 | 高基数 SoA unit/formation stores；stable formation key；composition CSR；command/order queue；不在脚本 VM 内逐单位反射 |
| Theatre / front / battle / occupation | 有 state-based front、force 比较、battle progress/casualty、war score；无 theatre topology、推进路径、战线分裂合并、occupation、terrain/tactics | Partial | P1 | `TheatreGraph`、front segment、battle instance、occupation map；front jobs 写局部 lane 后稳定归并 |
| Supply / logistics | 无 supply network、depot、infrastructure throughput、attrition、convoy dependency | Missing | P1 | 通用 capacitated transport graph；按 theatre 增量求解；结果缓存为只读 supply snapshot 给 battle/AI |
| Navy / fleet / blockade / invasion | 有静态 NavyRecord/ship design；没有 fleet/sea node、mission、interception、naval battle、blockade、convoy escort、invasion | Missing | P1 | `SeaRegionGraph`、FleetStore、NavalMissionStore、Blockade/ConvoyAllocation、NavalInvasion state machine；统一命令/事件接口 |

### 5.5 Economy / Society / Research

| 能力 | Core 证据与现状 | 状态 | 优先级 | 建议的通用接口 / 验收点 |
|---|---|---|---|---|
| 基础 POP/building/market loop | `PopStore/BuildingStore/MarketStore` 是 SoA；生产、消费、价格、工资/利润/税收按 market 分区并行，固定点与稳定归并基础较好 | Partial | P1 | 保持现有热列；补 labor matching、stockpile、market access 与可替代消费；所有 definition 从内容 pipeline 进入而非 demo/C++ 手工注册 |
| World market / trade / tariff / convoy | 有 TradeRouteRecord，但 weekly economy 不处理跨市场流、关税、船队容量、贸易竞争或路线盈利 | Missing | P1 | market graph + route SoA + convoy allocation；先稳定求容量/成本，再并行写各 market lane；tariff 作为 policy/modifier 输入 |
| Ownership / company / investment | 有 company、ownership stake、investment pool records，只有投资池周贡献累加；无股份约束、利润分配、HQ/charter、投资决策与建设队列 | Partial | P1 | `OwnershipStore` 保证份额不变量；company balance sheet、dividend、investment project、construction reservation；稳定交易 ledger |
| Banking / credit / currency | 无银行、贷款、利率期限、违约、货币、汇率、资本流 | Missing | P1 | double-entry `LedgerStore` + credit contract + currency definition/exchange market；金额保持定点，结算顺序稳定 |
| Culture / religion / literacy / demography | POP 有 culture/religion/literacy 列；除经济派生值和脚本 setter 外没有出生、死亡、教育、同化、皈依 | Partial | P1 | demographic cohort tick；rates 来自 scripted value/modifier；按 province/POP chunk 计算 delta 后稳定应用 |
| Qualification / profession / migration | POP 有 profession/qualification；MigrationFlow 只倒计时，不转移人口；无职业资格增长、吸引力、国内/国际迁移和 cohort split/merge | Partial | P1 | typed qualification vector/compact sparse profile；migration proposal → capacity/acceptance → deterministic cohort transaction |
| Discrimination / citizenship | 无 trait/acceptance、法律与文化宗教组合判定、工资/政治/迁移效果 | Missing | P1 | `AcceptanceQuery` 由 culture/religion traits + law rules 编译；输出标准化等级和 modifier，不在各系统重复条件树 |
| Technology / research | 已落地 `technology/research_rules` 内容解析和 mod replace：稳定 key、category/era/cost、前置缺失/重复/环检测、unlock keys、Country potential/on-researched 脚本；权威 `TechnologyRecord` 插入序充当队列，创新由人口识字率单遍 SoA 聚合并以整数确定性推进；沿用 World save/checksum，定义重排后按 stable key 恢复，专项 `research_tests` 已通过。仍缺扩散/追赶、并行项目、领域 modifier、完整 AI/UI | Partial | P1 | 保持现有高质量 foundation；补 research category budget、spread/diffusion、ahead-of-time、unlock 消费者 registry、AI planning 与 UI query；队列实例最终应有显式 stable instance identity |

### 5.6 UI / 3D / Mod / Save / Determinism

| 能力 | Core 证据与现状 | 状态 | 优先级 | 建议的通用接口 / 验收点 |
|---|---|---|---|---|
| Strategy UI draw foundation | `UiDrawList` 有 quad/texture/MSDF text/polyline/batch/scissor/hit test、nine-slice 与 panel shadow；固定/变高行虚拟化已有数值 helper；FontAtlas 有离线读写 | Partial | P1 | 保留 CPU draw contract，增加 retained widget tree、layout、focus/input/navigation、style/theme、accessibility 和生命周期 |
| Scripted GUI / data binding | 已有游戏无关 `ScriptedGuiSchema/Compiler/Blueprint`：游戏注册 typed data contexts/properties 和 commands，编译器在加载期解析 binding path、template/screen、widget tree、list/grid/chart 元数据，输出稳定 key 与紧凑 ID/连续数组；有类型/环/冲突诊断与顶层重排 checksum 测试 | Partial | P1 | 尚未接入 `DefinitionDatabase` 的事务发布、retained runtime、响应式 dirty propagation、focus/input/accessibility、经验证 command dispatcher 与生产 renderer |
| Tooltip / charts / virtualized lists | 已有 tooltip 翻转/视口夹取布局、chart range 与有界 polyline downsampling、O(log n) 变高行虚拟化；Scripted GUI 已能编译 list/grid/chart 元数据。仍无 tooltip AST/breakdown、历史 ring buffer、选择/focus 保持与实际 widget runtime | Partial | P1 | `TooltipDocument` 支持条件段和数值 breakdown；非权威 time-series store；keyed diff + selection/focus 保持 |
| 3D / live renderer | 有 Vulkan 1.3 swapchain、pipeline、terrain/political/living/UI shader contract 与 CPU render plans；当前 live backend 主要绘制验证用 fullscreen/triangle/quad，未绑定生产世界资源 | Partial | P1 | world streaming→GPU residency→descriptor/material→indirect draw 全链；真实地形/海洋/边界/建筑/单位、光照/大气/阴影/后处理；GPU validation + 截图 A/B + 帧预算 |
| Asset/content cooker | 有 `.coreasset/.corearch/.coreworld`、material/world cooker 和 readback checksum；内容类别之间仍缺统一 package manifest 与依赖图 | Partial | P2 | 一个 package manifest 描述脚本、definitions、localization、assets、world chunks、依赖和兼容范围；cook 输出逐资产来源与 hash manifest |
| Mod overlay / load order | 已有通用 `.coremod` manifest：规范化 stable mod ID、严格 SemVer/版本范围、required/optional dependencies、conflicts、load_before/after 与 priority。`build_mod_load_plan` 用确定性拓扑排序/稳定 ready-queue tie-break，报告循环和关系错误；VFS 只接受完整 plan，禁止事后 ad-hoc mount，plan/package hash 纳入有效 content hash | Partial | P0 | 仍缺显式 replace-path/patch/extend/remove 语义、逐 definition 来源链、compiled cache、签名/信任政策、安全热重载与 multiplayer compatibility class |
| Definition breadth | DefinitionDatabase 原生已覆盖 Country、script/localization、gameplay/AI、Research 与 Notification；经济和多数大战略 definition 仍由 C++ 手工构造或只有 record key hash | Missing | P0 | 可注册 `DefinitionSchema`；每个领域提供 parser/validator/binder；内容层只持 stable key，运行时建立 dense ID remap；未知字段严格诊断 |
| Compiled content cache / editor reload | 有启动解析与检查工具，无按 content hash 的 compiled cache、增量重编译、语义 diff、编辑器安全热重载 | Missing | P2 | `.corecache` 由 engine ABI + schema version + package hashes 键控；热重载仅 editor，权威游戏需事务式迁移或拒绝 |
| Save/load 当前覆盖 | Save codec 对现有 World、gameplay、AI/Research、Notification 与 clock 状态有完整性校验和事务 decode；restore 成功会清 pending commands/旧 replay，失败保持原状态；runtime digest 覆盖 clock 与 notification。`GCT1` 持久 GameplayInstanceId + 活跃 script context，`NTF1` 持久 NotificationInstanceId 并按 stable definition/action key 重绑定。多数 GrandStrategy 实例仍按 vector position ID，格式仍是单体手写 schema | Partial | P0 | chunked `AuthoritativeStoreRegistry`；每 store 自报 schema、stable-key remap、serialize/validate/checksum/migrate；加载先 staging、全局引用解析后一次 commit |
| Version migration | 已有 v1/v3 与旧 v4 变体的只读兼容路径；没有领域级迁移图、mod key rename/alias、缺失内容策略和迁移报告 | Partial | P1 | migration graph 按 store schema；stable key alias/tombstone；dry-run report；任何失败保持目标世界不变 |
| Replay / desync | `ReplayJournal` 已有独立版本化小端 codec、stable command wire ID、payload/semantic checksum、事务 decode、安全上限和 checkpoint match/desync API；年度 checkpoint 使用 engine checksum。命令 payload 仍主要是 Country + double，不能覆盖未来所有 typed command，也没有子系统差异定位 | Partial | P0 | typed command envelope + stable target key + deterministic payload；记录外部输入和必要 seed；周期 subsystem/store checksum；首个 divergence 输出 key/column diff |
| C++23 / SoA / JobSystem 热路径 | Economy、地理、POP/Building 与 JobSystem 契约扎实；GrandStrategyStore 仍是多个 AoS vector、顺序扫描和 reference tick，新增系统尚未普遍进入稳定 job DAG | Partial | P0 | 高基数权威状态一律 SoA；冷 definition 与热列分离；CSR 关系索引；固定 grain、lane staging、稳定 reduction；禁止 worker ID 进入 RNG |
| 跨平台确定性 | 核心经济用定点，但 Country/AI/script value 仍含 double；checksum 能发现差异但不保证不同编译器/架构不产生差异 | Partial | P0 | 权威数值采用定点/整数或明确的 deterministic math；浮点仅展示/非权威或经量化提交；建立 Windows/Linux、不同 worker 数长期续跑 gate |

## 6. 权威状态统一验收契约（P0）

任何新增权威 store 在合并前必须同时满足以下条件；“有 record”但缺任一项都只能标为 Partial：

1. **稳定身份**：definition 使用 namespaced stable key；运行时实例使用可重建的 stable instance key，dense ID 只作本局加速缓存。
2. **Save/load**：版本化、定长/有界 framing；加载到 staging；范围、引用、重复 key、不变量和内容依赖全部验证后原子提交。
3. **Deterministic checksum**：覆盖所有权威列、容器顺序语义和跨 store 引用；提供子系统 checksum 以定位 desync。
4. **专项回归测试**：round-trip、损坏/截断、未知/重复 key、内容注册顺序变化、旧 schema migration、不同 worker 数续跑、save 后续跑一致。
5. **热路径契约**：高基数 SoA；稳定遍历；并行写 disjoint range 或 deterministic staging；无 tick 内字符串查找、无无界分配、无共享可变 RNG。
6. **内容驱动**：规则、阈值、定义组合和 UI 元数据来自 schema 化内容；Core C++ 只提供通用状态机、查询、事务和性能 kernel。

建议把上述约束实现为可注册的 `AuthoritativeStoreDescriptor`，由统一测试 harness 自动检查 serialize/deserialize/checksum/validate，而不是依靠每个领域开发者手工记住四套入口。

## 7. 建议实施顺序

1. **P0 契约层**：在已有 Gameplay/Notification stable instance identity 与 context save/checksum 的基础上，建立通用 store registry、chunked save/checksum 和 typed command/replay；补齐 CoreScript 值表达式与 modifier runtime。
2. **P0 内容层**：在已有确定性 mod manifest/load plan 上补 definition schema registry 与 replace/patch/extend/remove；落地 on-action/scheduler、Event/Journal 完整状态机与通知信号桥。
3. **P1 领域层**：先建立 SoA 与事务边界，再补 Politics、Diplomacy、Warfare、Economy/Trade、Society、Research 的规则深度；不要继续向单个 AoS `GrandStrategyStore` 堆 record。
4. **P0/P1 AI 层**：goal/decomposition/reservation 框架与领域 query/command API 同步建设；避免 AI 直接依赖内部 vector 布局。
5. **P1/P2 表现层**：将已编译的 scripted GUI blueprint 接入 retained/reactive runtime、validated commands 和 tooltip/chart/virtualization read models，再把现有 render plans、streaming、assets 接入真实 Vulkan draw；建立视觉 A/B 和 GPU 帧预算。
6. **持续 gate**：每新增 store 同时提交 save/checksum/migration/determinism tests；每新增脚本能力提交 compile diagnostics、运行预算和 content-hash tests。

## 8. Core 证据路径索引

- CoreScript：`src/core/scripting/`
- 内容与 mod overlay：`src/core/content/`
- Event/Decision/Journal/Notification runtime：`src/core/gameplay/`；通知内容编译见 `src/core/content/NotificationContent.*`
- AI：`src/core/ai/`
- Technology/Research：`src/core/research/`、`src/core/content/ResearchContent.*`
- 通用大战略 records/reference systems：`src/core/grand_strategy/`
- Economy/POP/Building/Market：`src/core/economy/`
- Geography/scope index：`src/core/world/`
- Save/replay：`src/core/save/`；专项契约测试见 `tests/replay_journal_tests.cpp`、`tests/state_contract_tests.cpp`、`tests/runtime_save_tests.cpp`、`tests/notification_tests.cpp`
- Job/determinism：`src/core/jobs/`、`src/core/simulation/`
- Strategy UI：`src/core/ui/`；scripted GUI 契约见 `docs/SCRIPTED_GUI.md`、`tests/scripted_gui_tests.cpp`
- Mod manifest/load plan：`src/core/content/ModManifest.*`、`src/core/content/VirtualFileSystem.*`；语义契约见 `docs/MOD_RUNTIME.md`、`tests/mod_pipeline_tests.cpp`
- Vulkan/地图/terrain/living renderer：`src/core/render/`
- 回归测试：`tests/`

## 9. 结论

Core 已经具备有价值的底座：C++23、typed ID、多个 SoA store、确定性 JobSystem、固定点经济 kernel、编译脚本 fast path、VFS/content hash、原子 save decode、基础 Event/Decision/Journal/AI plan，以及世界/资产/渲染管线的分层。本轮又落地了 typed script signatures/linker、持久 gameplay context 与 stable instance ID、通知 runtime/save、确定性 mod load plan 和 scripted GUI 编译基础。但与本地 Vic3/Jomini 可观察的能力面相比，最大差距仍不是再增加若干 record，而是四个横切层尚未完全闭环：

- CoreScript 已能参数化与在 gameplay 链中持久，但值表达式、modifier、更多 scope/iterator 与通用持久容器仍不完整；
- mod manifest/load plan 已落地，但 definition schema + 语义 merge + compiled cache/content tooling 仍未全管线闭环；
- 所有权威 store 的稳定身份/save/checksum/migration/测试统一契约；
- 各领域真正可运行的 SoA state machine 与能分解目标的 AI。

因此当前总体判断仍是 **Partial foundation**，不能标记为可独立替代 Clausewitz + Jomini。优先完成 P0 横切契约，才能让 Politics/Diplomacy/Warfare/Economy/Society/Research/UI 的后续扩展不反复重写存档、脚本和确定性边界。
