# 客户端性能与资源优化

目标是让活动房间、可见单位和当前界面决定常驻资源规模。世界图像按帧取用，按页回收。常驻 UI、CPU 解码缓存、DT1 像素和进程私有提交分别计量。

## CPU 分析边界

以下是当前源码的静态分析和优化候选，不是采样热点排名。尚未用本轮诊断构建或运行游戏，不能估算 CPU 降幅。优化必须保持 25Hz 玩法、随机消费、预测／动画时钟、消息处理次序、原始图像与 PL2 混色语义，不以降频、合帧或删效果替代。

- 单机的进程 CPU 包含嵌入宿主、客户端、网络、资源和音频线程；TCP/IP Host 的宿主在后台，不能只看前端 `host` 阶段来排除它。Join 的进程才不包含本地权威模拟。
- [主循环](../../src/app/frontend.cpp)的 `present` 包含 `EndDrawing` 的提交、交换缓冲和限帧等待。阶段计时是墙钟，不是 CPU 时间；GPU 阻塞也可能发生在 `world` 内。
- [依赖配置](../../cmake/Dependencies.cmake)已关闭完整与末尾忙等；[窗口初始化](../../src/app/application.cpp)目标为 60 FPS。前端快照也已按修订缓存，不能再次把这两项列为未实现优化；实际运行 EXE 是否包含它们仍须核对。
- [物品解码](../../src/client/remote_inventory.cpp)已有 `itemRevision` 门控；[地图更新](../../src/client/remote_town.cpp)已有版本、观察点和定时更新条件；[图集](../../src/presentation/graphics/world_art.cpp)已有按需上传及预算。候选是细化失效与复用，不是重新增加一层全局缓存。

## 优化候选

优先级表示建议调查／实施顺序，不表示已经证明的耗时占比。先采样最高阶段，再在所属模块实现最小等价优化。

| 优先级 | 源码证据与触发条件 | 优化方向 | 判别方法与语义风险 |
| --- | --- | --- | --- |
| P1 | [RemoteCombat::update](../../src/client/remote_combat.cpp#L644)每个显示帧 `catalog_.clear()`，遍历技能表、查等级／数量并重新构造技能名称；状态和单位另有修订检查 | 将技能目录独立为按职业、技能等级／数量及游戏代次失效的派生数据；静态名称／分类预编译 | 看 `combatProjection` 的稳定均值。不可整体跳过 `update`，单位状态／归属仍需及时消费 |
| P1 | [RemoteUiClients::Impl::update](../../src/client/remote_ui_clients.cpp#L121)虽有 `ProjectionKey`，但含全局 `online.revision`，变化时一并调用人物、地图、库存、交互和佣兵投影 | 按角色属性／技能、物品、任务、商店、佣兵等真实输入拆分失效键；复用现有修订，缺少专用修订时先明确发布者 | 看 `uiProjection` 与单位活动的关联。事务 `pump`、倒计时、移动观察位置仍每帧推进，不能缓存受理／超时结果 |
| P1 | 同一 UI 路径在仅观察点变化时更新 `inventoryView.dropLocation` 并提升库存修订；[SceneView::refreshInventory](../../src/presentation/scene_view.cpp#L442)据此遍历物品并复制整个库存 | 分离位置／操作反馈与物品内容修订，避免走路时仅因投放位置改变就复制全部物品 | 相同库存比较站立与走路的 `uiProjection`。必须保留光标、捡放音效、待处理操作及交互代次校验 |
| P1 | [drawWorld](../../src/presentation/world/world_renderer.cpp#L62)逐帧遍历全部 `terrain.instances`，屏幕裁剪在循环内；[warpTileVisibility](../../src/presentation/world/warp_visibility.hpp)还分配全长掩码，即使未选出口 | 按房间／空间块索引筛选候选瓦片，再以原 DT1 边界裁剪；出口掩码按地形代次和选中出口缓存或稀疏表达 | 看 `world` 是否随已加载地图增大而上升。不能只按脚点裁剪高墙，必须保持稳定绘制次序、屋顶／出口与探索可见性规则 |
| P1 | [LightingView::prepare](../../src/presentation/world/lighting_view.cpp#L269)逐帧填充光场、计算遮挡传播和多光源叠加、四邻域均值并上传纹理；分配尺寸只增不减 | 首先按量化位置、光源有序输入、环境／任务光、碰撞版本、视口和原点复用完全相同结果；进一步缓存静态遮挡或区分活动尺寸与容量 | `world` 高只是入口线索，需后续函数采样确认光照占比。必须保留整数舍入和光源累加顺序，不能以固定低频更新替代相同输入复用；UI Lighting Off 仍执行 prepare，不是隔离 CPU 光照的开关 |
| P2 | [RemoteScene::Impl::draw](../../src/presentation/remote/remote_scene_draw.cpp#L152)逐帧构造飞弹目标／锚点，对单位状态再遍历 `skillRows` 匹配反击技能，并多次扫描单位集合 | 挂载资源准备时构造 state 到候选技能的有序索引；帧内复用单位静态元数据与临时数组容量；无消费者时能否免构造需核对全部副作用 | 对比城镇与密集怪物／状态场景的 `world`。不能剔除屏外仍参与飞弹、声音、光照或移动的单位；多个匹配项的先后仍需一致 |
| P2 | [PaletteBlendView::drawLit](../../src/presentation/world/palette_blend_view.cpp#L486)普通组件进入／退出 shader；地形光样本、cellX 等 uniform 变化会刷新批次；透明墙／PL2 混色逐精灵复制目标背景 | 先计量实际批次刷新／背景复制，再考虑把每绘制项参数放入顶点或参数缓冲，减少同语义批次的 uniform 更新；背景依赖的重叠绘制仍串行 | `world` 墙钟高但主线程 CPU 低时尤其需要 GPU／驱动采样。shader 反查调色板的 256 项循环是 GPU 工作，不可直接当 CPU 热点；不可按纹理重排遮挡和混色 |
| P2 | [RealmSession::tick/read](../../src/network/realm_session.cpp#L716)加锁，变化时复制整个 OnlineView；前端再复制一份。后台每 10ms `service()`，前台 tick 也执行 service | 先量化快照复制和锁等待；候选为按领域修订共享不可变快照／增量发布；在保证单机交付与请求响应时序后再考虑统一 service 调度 | 看 `network` 和进程／主线程 CPU 差值。不可简单返回后台可变 view 或删除前台 service；命令后本帧快照刷新是既有契约 |
| P2 | [WorldArt::pixels/upload](../../src/presentation/graphics/world_art.cpp#L117)冷缓存会同步读 MPQ、按方向解码、淘汰及上传；`resolve` 还有帧记录容量限制 | 先判定是否为缓存抖动，按真实工作集改善复用／淘汰；必要时后台准备纯 CPU 解码，GPU 上传仍在上下文所属线程 | 同路线第二遍是否仍出现长帧，结合 `atlasUploadBytes/atlasEvictions/directionBytes/frameRecords`。不盲目扩大所有缓存；所有者更换会使累计计数下降 |
| P3 | [SceneView::observeWorldSounds](../../src/presentation/scene_view.cpp#L82)重建 NPC 声音身份表；[scene_streams](../../src/presentation/audio/scene_streams.cpp#L148)每个活动流以 5ms 定时等待更新 | 身份表按出生／消失／身份变化维护；音频在实际线程采样证实后考虑就绪通知和缓冲期限驱动 | 进程 CPU 显著高于主线程时才优先调查。静音不等于停止解码／轮询，调整不能造成音频断续或延迟 |
| 诊断干扰 | [observeExplored](../../src/app/debug/navigation_search.cpp#L705)在 `D2X_NAVIGATION_DUMP` 开启时每 250ms 扫描全部已知瓦片的 25 个子格；[GamePacketLog](../../src/app/frontend.cpp#L154)逐包 JSON 编码并 flush | 性能基准仅启用帧日志；需要完整取证时另跑一轮。后续导出可按脏地形块／障碍修订增量处理，包日志可研究有界缓冲 | 导航导出当前记入 `ai`，即使 AI 未运行；异步写盘不代表主线程快照构造免费。不得据完整诊断 CPU 推算普通 Play 开销 |

世界绘制还会新建 draw／roofs／items 数组并稳定排序。容量复用和静态瓦片有序列表是后续局部候选，但应排在量级更大的遍历、光照和提交之后；不能用不稳定排序改变同深度先后。单机 `host` 高时应转到宿主领域计量，不在客户端用减少模拟步数掩盖问题。

## CPU 采样

已有 `--frame-profile` 增加了每个采样区间的全帧均值／最大值、超预算帧数、进程及调用线程 CPU 增量，并拆分 `town/uiProjection/combatProjection/world/uiDraw/graphicsMaintenance`。实现见[帧记录](../../src/app/frame_profile.cpp)与[前端阶段](../../src/app/frontend.cpp)。本轮源码未构建／运行；旧 EXE 不会产生新字段。

先用 Release 版本，保持相同角色、资源、窗口尺寸和设置。只开帧记录时直接运行实际待测 EXE，例如在仓库根目录：

```powershell
$frameLog = Join-Path $PWD ("artifacts/cpu-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + ".jsonl")
Remove-Item Env:D2X_NAVIGATION_DUMP -ErrorAction SilentlyContinue
& .\build\bin\d2x.exe --mpq .\assets\mpq2 --frame-profile $frameLog
```

该例使用程序默认角色目录；需要不同角色目录时沿用平时的 `--host-saves`。程序路径以实际构建／部署产物为准，不混用旧包。移除环境变量只影响当前终端及子进程。正常 Save & Exit 后关闭程序，让缓冲日志完整落盘；不必提供角色存档或账号凭据。

1. 菜单空闲 30 秒；入局预热后城镇站立 30 秒。记录各阶段大致开始时间。
2. 同一路线行走往返各 30～60 秒，区分首次加载和热缓存；再记录密集怪物／施法场景。
3. 同一位置分别开关包裹、自动地图，保持窗口尺寸不变；另作较小窗口对照，不把视觉设置改变当最终优化。
4. ESC 暂停、失焦、最小化分别观察。默认单机失焦会暂停宿主，因此这些差值不能直接归因于绘制；若使用 `--no-pause-on-focus-loss` 要明确记录。已有 LAN Join 条件时可额外采集，但不同房间／地图不是严格 A/B。
5. 只有需要收包／地图异常关联时再用 `Play-Diagnostic.cmd`，得到独立目录下的 `frames.jsonl`，并与仅开帧日志的基准区分。AI 自走另作一组，不与手动场景混算。

阅读日志时先看 `interval.processCpuOneCorePercent` 与 `mainThreadCpuOneCorePercent`：100 表示一个逻辑核忙满，进程值可能超过 100，不是任务管理器按全机核数归一化的百分比。进程减主线程只能粗分后台总成本，不能区分音频、网络、宿主、资源或驱动线程。再按区间 `meanMilliseconds` 找稳定负载，按 `maxMilliseconds` 与资源计数找卡顿。多个区间合并要按 frames 加权，不直接平均均值。

阶段耗时互斥，`scene` 现在是拆出 world／UI 后的剩余工作；加载／异常／备用界面分支可能仍记入 scene，不能直接比较新旧日志的 scene 数值。`present` 高且线程 CPU 低通常是等待，不应当作纯计算热点。`framesOver16_67Ms` 包含正常限帧唤醒误差，不等于掉帧数。阶段最大值可能来自不同帧，不能相加解释某一慢帧。

诊断每秒或完整帧达到 100ms 时输出，32MiB 封顶；区间可能小于一秒。`diagnosticCollectionMs` 只覆盖该记录的采样／上下文组装，不含其后的 JSON 序列化和写盘；这些仍计入后续进程 CPU 区间而不属于正常帧阶段。最后不足采样周期的区间可能没有输出。若 `world` 仍占主导，下一步采用原生 CPU／GPU 采样或在该阶段继续细分光照、列表构造和绘制提交，而不是凭聚合耗时选择算法。

## 计量边界

幕内常驻资源与退局后的进程提交分别计量。逻辑RGBA、实际图集页、CPU存活分配、堆保留和驱动延迟回收不是同一个指标；驱动显存需要独立测量。

## 原版与参考实现对设计的约束

- **组件与颜色可分别复用。** 本机 1.13c D2Client 逐 COF 层取 cell，本体与阴影分别绘制。D2Glide 已核实的路径分别上传 P8 索引纹理与调色表。D2X 可让不同外观共享组件像素，绘制时再应用层序、颜色平移和混色。[零售组件链](../modules/CHARACTER.md#显示边界) · [Glide 索引纹理与调色表](../architecture/DIABLO2_GLIDE_RENDERING.md#31-原图仍是索引图)
- **解码与上传可用不同粒度。** Diablerie 默认在首次访问方向时解码，用图集矩形和轴点定位各帧。D2X 可按方向复用前帧数据，只上传用到的 cell，并让逻辑尺寸、脚点和选取框独立于图集坐标。[Diablerie 的 GetSprites 与 MakeFrames](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/IO/D2Formats/DCC.cs)
- **缓存按占用淘汰。** OpenDiablo2 按调用者给定的权重淘汰最久未用项。D2X 分别按 CPU 存活字节与 GPU 已分配页计费，图集空洞计入整页成本，绘制批次仍引用的页须固定。原版缓存容量尚未核实。[OpenDiablo2 的 Insert 与 Retrieve](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2common/d2cache/cache.go)

## 当前实现

| 资源 | 取用与回收 |
| --- | --- |
| 地形 | DT1 仅为可见瓦片上传 R8 索引。以挂载内的库编号和瓦片序号跨同幕区域复用，世界层以弱引用登记 DT1，活动地图或选择器持有解码像素。 |
| 角色与物件 | 先读头部边界，DCC 按方向、DC6 按 cell 解码。COF 按原顺序绘制组件，染色在着色器取表，投影阴影单独缓存。播放速度和技能序列引用源帧。选择框和脚点保留原坐标。 |
| 特效与物品 | 飞弹、覆盖层和地面翻转动画按帧上传。背包、商店和材料页在实际请求图标时取帧。需要直接取色或 PL2 混色的路径保留相应颜色／索引表示。 |
| 字体与规则表 | 原版字形按颜色保存实际使用的帧，轮廓字体只栅格化 ASCII 和已遇到的字符。同一 MPQ 挂载的规则表按类型相等性查找，场景与 UI 共用同一准备模式的实例。 |
| 预算 | 场景与特效两个缓存各有 128 MiB 图集页软预算、32 MiB 解码缓存。当帧用到的页固定到批次结束。申请新页时先淘汰冷页，另每 120 帧回收至少 600 帧未使用的页。单次超大解码或当帧工作集可超过预算。 |

计量入口见[图集](../../src/presentation/graphics/world_art.cpp)、[瓦片库](../../src/world/map.cpp)及[帧分析参数](BUILD_AND_RUN.md)。图集统计整页容量，含空洞。独立纹理、渲染目标、驱动占用和宿主数据各有不同的统计范围。

场景和UI通过同一挂载缓存复用规则表，按类型哈希与相等性查找，不依赖局部与外部RTTI类型的排序。入口见[挂载缓存](../../src/resources/archive.hpp)。

## 验收

1. 固定 EXE、种子、角色与分辨率，同幕同路线往返 10 次。预热后存活字节和图集页数进入稳定区间，同时记录冷／热帧耗时与重复上传量，识别淘汰过度造成的卡顿。
2. 复走区域 73。首轮工程目标为场景 GPU 图像页峰值 **≤256 MiB**，按已分配页容量统计；UI、渲染目标及驱动指标另列。核对换装与染色、尸体末帧、首领死亡时序、透明墙、阴影和鼠标选取。
3. 同进程重复进退局，在相同菜单空闲时点比较存活对象与私有提交，再做单进程五幕对照。记录各幕后低水位、全程峰值和退出耗时；跨幕下降与单次退局均不足以验收长期稳定性。
