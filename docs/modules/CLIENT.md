# 客户端契约与原协议基线

更新：2026-10-10。原服与自研宿主共用完整客户端，只有连接选择不同。当前Windows客户端为Release包；有限自研／原服冒烟、启动耗时及包身份见[基线](../../BASELINE.md)。

技能客户端修改边界由[COMMON](../gameplay/skills/COMMON.md#技能迁入规范)维护；弓弩／箭袋及宠物投影的原版修复与死亡图未核实项见[亚马逊](../gameplay/skills/AMAZON.md#原版依据与客户端差异)。

## 唯一客户端链

RealmFrontend与所有游戏页面都读取RealmSession的原协议副本。输入统一为SceneController → RemoteUiClients／RemoteControl／RemoteCombat／RemoteInventory → 原MCP／D2GS；回包统一为RemoteWorld／RemoteInventory／RemoteTown → RemoteUiClients／RemoteScene → SceneView。GameClients、IGameConnection、EmbeddedConnection、LocalGame、GameScene及单独的角色服务DTO已删除。

IByteTransport提供原字节流，TcpStream与MemoryTransport实现相同connect／send／poll／close。内存队列有锁、有界，支持网络worker与宿主调度线程分离；重连清旧消息。客户端不引用GameHost、PersistentCharacter或存档路径，也不直接接收权威快照。自研宿主通过原1.13c包提供已实现行为，不新增客户端协议分支。
InventoryView允许缺失尚未接入的容器；共用腰带绘制／输入／格区必须先查询存在性，缺失时不绘制或提供交互格，缺背包时明确拒绝打开库存面板。客户端不能为满足UI假设而伪造容器或库存状态。

## 既有基础批次客户端边界

以`df6f6c9`为提交基线，库存／装备、人物成长、世界／多人和普通近战需求均由自研服务端输出原包完成，不以修改原服客户端消费者为前提。RemoteWorld／RemoteUiClients／RemoteCombat／RemoteInventory、D2GS解码、SceneController与原消息／输入链保持该基线；SceneView弹体纯计算提取是下表中用户另行授权的例外。本节为该基础批次差异范围；后续职业技能按原版证据授权的公共改造见文末。未执行原服运行回归，不能当作运行认证。

已逐项列出的客户端改动与确认边界：

| 改动 | 作用与边界 |
| --- | --- |
| RealmFrontend与app/frontend的TCP/IP入口 | 用户已确认保留：首页按钮、Host／Join原图页面、默认127.0.0.1及连接／返回菜单；选角、大厅和入局后仍走既有原协议链；LAN专用字体／图片只在打开该页时加载，不增加原服启动资源依赖 |
| RealmSession.connect_realm的gamePort参数 | 用户已确认保留：只为自定义LAN游戏端口服务；原服LoginOptions已有的端口配置和原包消费不变 |
| client/character_projection的两个属性名 | 用户已确认保留独立修复：当前MPQ的ItemStatCost为item_poisonlengthresist（ID 110）及item_absorbfire_percent（ID 142），没有旧名poisonlengthresist／fireabsorb；按原表名称读取已收到属性，不增加自研分支或补算未收到值 |
| presentation/world/client_missile_view的公共计算提取 | 用户已授权：环形散射条件／方向、结束环形子弹体、整数转向与墙面裁剪移入gameplay纯函数；客户端仍传自身Clt参数和正向帧，相同墙面掩码／浮点运算顺序保留，未改原包、输入、命中权限或增加自研分支；行为差异仍须另行确认 |

app/debug/server_commands的参与者选择是宿主管理入口；network新增TCP监听用于服务端，地址枚举用于LAN入口；world/generated_area新增recipe仅由宿主内容准备消费。新增纯装备／近战计算没有替换客户端原有输入、结算来源或显示路径。服务端尚未实现的规则不能通过减少原服客户端支持来适配。

## 视图与命令

| 端口 | 两种服务端共用的原协议适配与消费者 |
| --- | --- |
| IActorClient | RemoteUiClients 适配 RemoteControl／RemoteCombat／RemoteInventory；SceneController 统一移动、施放、交互、拾取、按住／松开／失焦手势 |
| IInventoryClient | 原服物品解码形成 InventoryView，复用物品显示公式、面板、拖放与预览；提交原库存协议，客户端占格预览不结算物品 |
| ICharacterClient | 原服已知属性／技能等级 → CharacterProjectionInput → projectCharacterDisplay；人物、技能树、提示和底栏读取同一结果 |
| IQuestClient | 原协议本人状态／旗标／洞穴剩余数 → QuestProjectionInput → projectQuestDisplay；任务页、完成动画和新日志提示共用 SceneView／SceneController |
| INpcClient | 原协议对白、货架、NPC提示 → 只读NPC／Shop／Hireling视图；共用菜单／交谈／商店／佣兵面板，语义命令由原协议端口提交 |
| IMapClient | 原服当前位置／旅行回执及共同地图 → MapSceneView／TravelMenuView；地图交互、传送点和面板共用控制器 |

端口与投影只包含显示值和必要目标；视图按版本复制，UI 不保留权威引用。OnlineIntentContext 与物品 handle 保留投影时的场景／物品版本；切区、关闭交互等变化取消旧意图。报价未知与 canRequestSale 提交资格分开，客户端不自算原服价格、费用、奖励或保存。

## 共用显示入口

技能资格的 MPQ 导入、固有技能清单和学习／主动技能判断共用 skill_eligibility；怪物目标选择、按住锁定和发送前校验共用 RemoteCombat.monsterTargetEligible，表现中的死亡判断共用 onlineMonsterCorpse。协议适配仍核对场景代次、单位分配、选中 owner、范围和地图绑定。

人物与任务投影编入 d2x_client，使用显式只读事实和当前 ClassicData／MPQ，不依赖 RealmSession、GameSession、设备、GPU 或本地执行器。RemoteUiClients 负责解码状态适配及协议命令，不再内嵌任务文字选择规则。未收到状态或缺必要输入保留未知；不把游戏共享旗标当成本人完成记录。

任务显示、通知与未知状态规则统一见[任务系统](../gameplay/quests/SYSTEM.md#唯一显示链)，具体面板生命周期见[NPC／任务接口](NPC_QUEST.md)。任务资格、NPC服务和奖励由所连接服务端决定；客户端不从地图种子、公共旗标或旧执行器补推进。

世界沿 WorldDrawView → SceneView.drawWorld，动画沿 ActorAnimationCatalog／ActorAnimationState；自动地图沿共同 AutomapCatalog／AutomapExploration → drawAutomap。删除无人使用的旧小地图状态／投影、IMapAssetSource 和旧资源预热，原服地图适配保留必要坐标／资源事实。声音沿 SoundCatalog → PresentationSoundEvent／SoundActorView／ItemDropSoundEvent → SceneAudio → SoundBank，不在两端解释 MonSounds；物品原表落地声音及25Hz触发帧同样只在公共配置入口读取。

Single Player的暂停策略由app/frontend在菜单输入处理后传入RemoteScene；单机宿主沿既有EmbeddedRealm.pump暂停，不增加协议消息。RemoteScene暂停移动预测、人物／怪物／物件动画、弹体与效果时钟，SceneView.advanceWorldPresentation同步冻结地面物品翻转及世界提示动画，世界声音流同步暂停。SceneView.refreshUi只推进菜单与面板UI；暂停期间及恢复首帧更新墙钟锚点但不增加世界时间，避免恢复追帧或将暂停识别成长帧加载而清除效果。共享房间／原服不因ESC菜单或失焦冻结世界表现。此修复已随当前客户端构建入包，未专项运行认证。

底栏只加载原联机minipanel，显示／命中共用一组按钮；NPC、库存、人物、任务、地图、选项等面板没有Local备用实现。已接服务的范围见对应模块，未接服务或缺失原服状态明确不可用，不调用本地服务补齐。帮助页移除旧本地Save／Load、授予金币／经验等提示；原服调试暂停及在线command入口保留。

组队操作沿app/frontend → SceneView／PartyView只读名册与按下／释放 → RealmSession.party_action → 原0x5E；不要求可见世界单位，原75／8B状态与8D队伍ID确认入队／离队。P面板显示同队状态、原区域与分隔条，世界HUD显示原职业队友头像和0x7F生命条；RemoteTown把可见玩家位置或0x90公开坐标投影为同队自动地图标记，不创建空间单位或揭示房间。原资源、动作与暂缓项见[联网模块](NETWORK.md#组队邀请客户端)和[自动地图](../gameplay/world/AUTOMAP.md)。本批只完成客户端，不扩充自研宿主队伍规则。

## 保留范围与限制

局前使用同一RealmFrontend原图与MCP角色列表、分页、双击、建删选角。Single Player按钮仍用原3WideButtonBlank／TBL5106，组装层连接嵌入Realm并自动建单人房；客户端不读取D2S。首页TCP/IP Game使用原背景／Host、Join按钮／IP弹窗，Join默认127.0.0.1；RealmFrontend只发连接意图和显示应用提供的本机地址，Host／Join随后回到同一选角；Host自动建房跳过大厅，Join沿同一大厅加入。设备接口枚举和监听属于network／hosting，应用负责传输选择及返回菜单。自研尚未实现的玩法请求保留原协议，不通过本地执行器补齐。

GameSession、Simulation、SkillRuntime、InventoryService、客户端本地任务／AI／奖励执行源码及d2x_session目标已删除。gameplay／items保留两端适用的纯函数和值；服务端调用纯任务阶段规则，客户端不能调用它推进权威记录。PersistentCharacter是纯领域保存值，CharacterSaveData为其别名；客户端库不链接persistence，产品经嵌入宿主链接。旧法杖插入面板没有原服生产者，其空状态／资源／绘制入口已删除，原协议插杖流程仍未实现。原MPQ、reference、旧包、mvp和用户文件保留。

完整细节与原服协议限制见[联网模块](NETWORK.md)、[NPC／任务](NPC_QUEST.md)、[人物](CHARACTER.md)、[库存](INVENTORY.md)及[地图](MAP.md)。运行证据与源码／包差异统一见基线和联网模块。

## 女巫公共客户端补齐

当前修改边界：有本地参考依据、用于匹配原版的修复已获用户授权；禁止为兼容自研宿主单独改变客户端。两种连接只在底层字节来源不同，不按宿主种类选择技能、动画或属性算法。公共提取与30项证据见[女巫技能](../gameplay/skills/SORCERESS.md#30项公共职责核对)。

本人已知FCR进入公共施法时序；原服未发送累计属性时，从已完整解码的本人装备／套装／状态列表计算，未知值保持未知，其他玩家不推算隐藏属性。普通SC、seq6／seq12共用原事件与序列定义；冰封球按remaining散射／转向，保留Clt参数。资源路径留在SkillSpec，公共数值以SkillRuleSpec输入resolveSkill，服务端调用相同计算。完整路径量化及原版各自的地面生成程序仍由两端适配，尚未统一成完整技能执行器。

本轮经用户授权完成女巫30项服务端与客户端改造。RemoteCombat按原表统一敌我／物件／物品目标资格，地面物品从原9C／9D物品副本读取而非伪造普通单位；心灵传动登记原仓库／传送点开窗等待，仍由77／63回包确认。应用面板随原存储状态清理；RemoteScene统一处理seq6循环姿态、持续Inferno及转向、A3目标落雷；RemoteScene按原CltDo26／28在目标点重建火墙及中心弹体，Blaze按原状态与显示移动跨格留火；ClientSend不阻止这些原本本地生成的程序，原73同步与预测按不同来源有界配对。地面物品位置亦用于本人预测，不能等待原服通常不回送的本人4C／4D。ClientMissile补暴风雪13／19和连锁16，以及ReturnFire合格碰撞的ChillingArmor本地图像。Hydra依旧是原monster单位和技能337。原服与自研都使用这些代码，不读取自研服务端状态或私有协议。显示接触不能扣血／扣蓝或修改存档，完整像素／声音认证范围见[女巫技能](../gameplay/skills/SORCERESS.md)。

## 亚马逊公共客户端补齐

本轮遵循同一授权边界，具体原Clt函数／DLL RVA、30项范围和公共计算见[亚马逊](../gameplay/skills/AMAZON.md)。Shared sequence／weaponVolley、引导、分裂候选、整数圆盘／环形和GUID继任用于原客户端程序，不读取服务端领域状态。本人IAS／武器速度／Pierce仅用已知属性及已解码装备；远端隐藏值不推算。Srv与Clt的牺牲火期限、目标候选及转向参数分别取原表。

RemoteScene补CltDo18–22、ClientMissile补原尾迹／毒烟／引导及Hit12／14／25。RemoteWorld消费原13字节7A宠物归属，归属名册与可见monster生命周期分离；9D保留ownerType，孔内／公开装备按实际主人清理。States的gfxtype=2人物伪装使用原动作映射及既有COF合成器，未添加自研宿主显示分支。头像HUD尚未接，不能将世界图形支持解释为完整宠物界面。

亚马逊30项已构建打包，列出的自研及原服代表路径完成有限运行；证据及未认证边界见亚马逊专题，其他技能的历史证据不能认证其新增路径。

## 通用十项原版修复

原CltDo2补药瓶及左手的物品弹体映射，地面寿命计算与服务端共用纯函数；原3F准备鉴定光标，事件revision区分重复来源／入局代次并等待真实来源投影；Unsummon按原7A及PetType的本人归属／许可筛选。原服和自研仍消费同一原消息，不推算服务器扣费或伤害。普通木桶按原OperateFn5和已发送0x13预测本人隐藏KK，复用原动作渲染；破坏／掉落仍等服务端。依据与未认证边界见[通用技能](../gameplay/skills/GENERAL.md)。
