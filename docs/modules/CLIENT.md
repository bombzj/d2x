# 客户端契约与原服适配基线

更新：2026-10-07。产品只保留联机调用链。当前源码已移除所有 Local 客户端、旧 SceneView.advance／sessionRestored／localSession、旧本地物件／尸体命中、物品提示、任务事件消费者及探索侧文件入口；d2x_presentation 不再链接 d2x_session。最终依赖清理及交付证据统一见[基线](../../BASELINE.md)和[联网模块](NETWORK.md#最终依赖清理)。

## 视图与命令

| 端口 | 唯一产品适配与共用消费者 |
| --- | --- |
| IActorClient | RemoteUiClients 适配 RemoteControl／RemoteCombat／RemoteInventory；SceneController 统一移动、施放、交互、拾取、按住／松开／失焦手势 |
| IInventoryClient | 原服物品解码形成 InventoryView，复用物品显示公式、面板、拖放与预览；提交原库存协议，客户端占格预览不结算物品 |
| ICharacterClient | 原服已知属性／技能等级 → CharacterProjectionInput → projectCharacterDisplay；人物、技能树、提示和底栏读取同一结果 |
| IQuestClient | 原服本人的状态／旗标／洞窟剩余数 → QuestProjectionInput → projectQuestDisplay；任务页、完成动画和新日志提示共用 SceneView／SceneController |
| INpcClient | 原服对白、货架、NPC 提示 → 只读 NPC／Shop／Hireling 视图；共用菜单／交谈／商店／佣兵面板，语义命令由原服端口提交 |
| IMapClient | 原服当前位置／旅行回执及共同地图 → MapSceneView／TravelMenuView；地图交互、传送点和面板共用控制器 |

端口与投影只包含显示值和必要目标；视图按版本复制，UI 不保留权威引用。OnlineIntentContext 与物品 handle 保留投影时的场景／物品版本；切区、关闭交互等变化取消旧意图。报价未知与 canRequestSale 提交资格分开，客户端不自算原服价格、费用、奖励或保存。

## 共用显示入口

人物与任务投影编入 d2x_client，使用显式只读事实和当前 ClassicData／MPQ，不依赖 RealmSession、GameSession、设备、GPU 或本地执行器。RemoteUiClients 负责解码状态适配及协议命令，不再内嵌任务文字选择规则。未收到状态或缺必要输入保留未知；不把游戏共享旗标当成本人完成记录。

任务完成记录可点击查看原说明；首次收到的每项状态建立完成动画基线，后续已知状态转换才排队动画／日志提示。洞窟剩余数缺失显示 ?，不代入零；真墓符号、全部后续幕特殊说明及缺失协议仍按未确认边界处理，不从地图种子或旧任务执行器推算。任务资格、NPC 服务和奖励全部由 D2GS 决定。

世界沿 WorldDrawView → SceneView.drawWorld，动画沿 ActorAnimationCatalog／ActorAnimationState；自动地图沿共同 AutomapCatalog／AutomapExploration → drawAutomap。删除无人使用的旧小地图状态／投影、IMapAssetSource 和旧资源预热，原服地图适配保留必要坐标／资源事实。声音沿 SoundCatalog → PresentationSoundEvent／SoundActorView → SceneAudio → SoundBank，不在两端解释 MonSounds。

底栏只加载原联机 minipanel，显示／命中共用一组按钮；NPC、库存、人物、任务、地图、选项等面板没有 Local 备用实现。未接入的佣兵、赌博、任务物品服务或原服状态继续明确不可用，不调用本地服务补齐。帮助页移除旧本地 Save／Load、授予金币／经验等提示；原服调试暂停及在线 command 入口保留。

## 保留范围与限制

GameSession、Simulation、SkillRuntime、InventoryService、本地任务／AI／奖励执行源码及 d2x_session 目标已删除。gameplay／items 保留联机显示、地图、原资源报告与独立 D2S 工具所需纯函数和值；CharacterSaveData 移到 persistence，字段与编码不变。产品不链接 persistence。旧法杖插入面板没有原服生产者，其空状态／资源／绘制入口已删除，原服插杖流程仍未实现。原 MPQ、reference、旧包、mvp 和用户文件保留。

完整细节与原服协议限制见[联网模块](NETWORK.md)、[NPC／任务](NPC_QUEST.md)、[人物](CHARACTER.md)、[库存](INVENTORY.md)及[地图](MAP.md)。下面仅保留此前本地迁移的运行证据，不能认证当前源码。

## 历史本地迁移验证（2026-10-03）

2026-10-03 Windows Release 构建成功，游戏完成链接。使用已有调试管道与 UI 输入入口，未新增测试脚本、用例或专用程序。

- 普通女巫／区域 1／整局 seed 210：真实左键地面输入经新适配器提交，25 固定步后位置由 `(153.5,68.5)` 到 `(153.9375,66.9375)`，路径结束；生命 40、法力 35，无人物外观错误。
- 旅行区域 2 后使用初始装备提供的 Fire Bolt，5 步后施法剩余约 0.32 秒；截图已查看，人物施法动作与原场景正常。该观察不认证完整伤害／弹道或全部动作分支。
- 临时 D2S 独立加载后回到第一幕城镇；跨到区域 40 再保存／恢复后，人物在 `(153.5,203.5)`，生命 40、法力 35，外观错误为空。两次实例均退出 0、stderr 空；施法与鲁高因截图已查看。

证据：`artifacts/refactor-p1-build.log`、`artifacts/refactor-p1-20261003/` 内状态 JSON、日志与截图，均不纳入源码。仅读写该目录的临时角色档，原 MPQ 与用户存档保留。完整战斗、多人和 Linux 运行尚未验证。
