# 客户端契约与原协议基线

更新：2026-10-08。原服与自研宿主共用完整客户端，只有连接选择不同。Windows Release已构建打包；单机有限冒烟、当前及旧包身份见[基线](../../BASELINE.md)。

## 唯一客户端链

RealmFrontend与所有游戏页面都读取RealmSession的原协议副本。输入统一为SceneController → RemoteUiClients／RemoteControl／RemoteCombat／RemoteInventory → 原MCP／D2GS；回包统一为RemoteWorld／RemoteInventory／RemoteTown → RemoteUiClients／RemoteScene → SceneView。GameClients、IGameConnection、EmbeddedConnection、LocalGame、GameScene及单独的角色服务DTO已删除。

IByteTransport提供原字节流，TcpStream与MemoryTransport实现相同connect／send／poll／close。内存队列有锁、有界，支持网络worker与宿主调度线程分离；重连清旧消息。客户端不引用GameHost、PersistentCharacter或存档路径，也不直接接收权威快照。自研宿主通过原1.13c包提供已实现行为，不新增客户端协议分支。
InventoryView允许缺失尚未接入的容器；共用腰带绘制／输入／格区必须先查询存在性，缺失时不绘制或提供交互格，缺背包时明确拒绝打开库存面板。客户端不能为满足UI假设而伪造容器或库存状态。

## 视图与命令

| 端口 | 两种服务端共用的原协议适配与消费者 |
| --- | --- |
| IActorClient | RemoteUiClients 适配 RemoteControl／RemoteCombat／RemoteInventory；SceneController 统一移动、施放、交互、拾取、按住／松开／失焦手势 |
| IInventoryClient | 原服物品解码形成 InventoryView，复用物品显示公式、面板、拖放与预览；提交原库存协议，客户端占格预览不结算物品 |
| ICharacterClient | 原服已知属性／技能等级 → CharacterProjectionInput → projectCharacterDisplay；人物、技能树、提示和底栏读取同一结果 |
| IQuestClient | 原服本人的状态／旗标／洞窟剩余数 → QuestProjectionInput → projectQuestDisplay；任务页、完成动画和新日志提示共用 SceneView／SceneController |
| INpcClient | 原服对白、货架、NPC 提示 → 只读 NPC／Shop／Hireling 视图；共用菜单／交谈／商店／佣兵面板，语义命令由原服端口提交 |
| IMapClient | 原服当前位置／旅行回执及共同地图 → MapSceneView／TravelMenuView；地图交互、传送点和面板共用控制器 |

端口与投影只包含显示值和必要目标；视图按版本复制，UI 不保留权威引用。OnlineIntentContext 与物品 handle 保留投影时的场景／物品版本；切区、关闭交互等变化取消旧意图。报价未知与 canRequestSale 提交资格分开，客户端不自算原服价格、费用、奖励或保存。

## 共用显示入口

技能资格的 MPQ 导入、固有技能清单和学习／主动技能判断共用 skill_eligibility；怪物目标选择、按住锁定和发送前校验共用 RemoteCombat.monsterTargetEligible，表现中的死亡判断共用 onlineMonsterCorpse。协议适配仍核对场景代次、单位分配、选中 owner、范围和地图绑定。

人物与任务投影编入 d2x_client，使用显式只读事实和当前 ClassicData／MPQ，不依赖 RealmSession、GameSession、设备、GPU 或本地执行器。RemoteUiClients 负责解码状态适配及协议命令，不再内嵌任务文字选择规则。未收到状态或缺必要输入保留未知；不把游戏共享旗标当成本人完成记录。

任务完成记录可点击查看原说明；首次收到的每项状态建立完成动画基线，后续已知状态转换才排队动画／日志提示。洞窟剩余数缺失显示 ?，不代入零；真墓符号、全部后续幕特殊说明及缺失协议仍按未确认边界处理，不从地图种子或旧任务执行器推算。任务资格、NPC 服务和奖励全部由 D2GS 决定。

世界沿 WorldDrawView → SceneView.drawWorld，动画沿 ActorAnimationCatalog／ActorAnimationState；自动地图沿共同 AutomapCatalog／AutomapExploration → drawAutomap。删除无人使用的旧小地图状态／投影、IMapAssetSource 和旧资源预热，原服地图适配保留必要坐标／资源事实。声音沿 SoundCatalog → PresentationSoundEvent／SoundActorView／ItemDropSoundEvent → SceneAudio → SoundBank，不在两端解释 MonSounds；物品原表落地声音及25Hz触发帧同样只在公共配置入口读取。

底栏只加载原联机 minipanel，显示／命中共用一组按钮；NPC、库存、人物、任务、地图、选项等面板没有 Local 备用实现。未接入的佣兵、赌博、任务物品服务或原服状态继续明确不可用，不调用本地服务补齐。帮助页移除旧本地 Save／Load、授予金币／经验等提示；原服调试暂停及在线 command 入口保留。

## 保留范围与限制

局前使用同一RealmFrontend原图与MCP角色列表、分页、双击、建删选角。Single Player按钮仍用原3WideButtonBlank／TBL5106，组装层连接嵌入Realm并自动建单人房；客户端不读取D2S。自研尚未实现的玩法请求保留原协议，不通过本地执行器补齐。

GameSession、Simulation、SkillRuntime、InventoryService、本地任务／AI／奖励执行源码及 d2x_session 目标已删除。gameplay／items 保留联机显示、地图、原资源报告与独立 D2S 工具所需纯函数和值；PersistentCharacter是纯领域保存值，CharacterSaveData为其别名；客户端库不链接persistence，产品经嵌入宿主链接。旧法杖插入面板没有原服生产者，其空状态／资源／绘制入口已删除，原服插杖流程仍未实现。原 MPQ、reference、旧包、mvp 和用户文件保留。

完整细节与原服协议限制见[联网模块](NETWORK.md)、[NPC／任务](NPC_QUEST.md)、[人物](CHARACTER.md)、[库存](INVENTORY.md)及[地图](MAP.md)。运行证据与源码／包差异统一见基线和联网模块。
