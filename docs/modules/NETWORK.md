# 联网入口与底层基线

开发方向已改为[全面联机](../architecture/MULTIPLAYER.md)：放弃单机兼容，普通 UI 不暂停游戏；显式测试管道仅冻结客户端表现。M0–M3 已开始源码实施：联机唯一产品入口、持续网络 worker、稳定客户端快照、协议／请求上下文诊断、入局取消／超时恢复、导航上下文和记忆凭据快捷入局。本批已 Windows Release 构建并更新 `dist/current`，有限原服证据见本页“测试暂停与快捷入局”；历史证据不认证完整新调用链。完整协议、多人与全部玩法阶段仍未完成。

更新：2026-10-07。局前、服务器角色加载／保存退局、五幕共用纯C++地图、地图交互／旅行及探索已有运行包。后续行走／回城门／NPC交谈和27种物品／城镇服务command已Release构建，并完成城镇有限原服冒烟及新进程保存回归；`dist/current`已含此前底层、登录记忆、连续移动显示、共用游戏UI及本批动作／效果，有限原服冒烟范围见下文。五幕1–136共用离线地图核心，122／364组原DLL地形对照及既有五幕旅行／奥术门户往返通过。普通攻击／技能请求、战斗／状态副本与成长command已Release及有限原服冒烟，属性／技能／经验保存重入通过；共用UI已接基本PvE输入、库存／货架和成长；本轮补接连续输入、原攻击／施法动作、基础直线弹体和状态叠层，以及沉沦魔／僵尸和NPC动作；已Release构建、打包并进行有限原服冒烟。复杂技能表现、精确价格、交易／赌博／雇佣、完整任务状态／资格与未验物品组合仍待完成。

RemoteTown逐条重放原服有序房间／玩家位置事件，精确检查房间锚点，输出实际DT1与活动碰撞。五幕事件丢失或生成失败会停止显示／移动并给出 `nativeMapReason`，要求重新入局；不回退近似预设。用户明确拒绝原DLL地图运行依赖；开发对照方法见[实施计划](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。

行走回退／房间引用修正：联网地图只由原0x07／0x08持有视野引用；本人坐标只选择当前房间／区域，不再叠加D2Game管理客户端房间的CLIENT_IN_ROOM引用。此前两条生命周期混用，使视野引用被误释放，出现`Unbalanced native room reference release`。依据D2MOO DrlgActivate、D2Game Clients及既有1.13c客户端派发证据修正，保留引用失衡诊断。表现预测不再按“估算走完＋1秒”撤销；导航和表现共用15秒无原服位置进展期限。独立角色实际复现原服约四秒才补终点样本，而旧预测两秒多就回退；修正后等待迟到样本保持终点。0x18／95／96末尾按有符号current−tTargetCoord解码，保留原服路径目标；原协议无请求序号，较新代次不是最新拖动意图的ACK，仅目标与当前意图一致（含相邻格）时适配显示终点。路径上迟到采样保留显示进度，旧目标不覆盖新方向；不当成小数坐标或新动作。共用碰撞路径按原格中心规划，0x15、受击／停止／死亡仍服从原服。`scene.playerDisplayPosition`和`units.verifiedDestination/pathVerificationRevision`供比较表现与原服，不能作玩法权威。构建及有限原服证据保留在忽略的`artifacts/online-act1-monsters-20261006`；营地迟到采样不再回退、传送至冰冷之原及一次分段移动到达，累计26次0x07／15次0x08无引用错误。受击阻挡的另一段未计为通过；完整五幕、网络延迟／丢包与动态阻挡仍未认证。

最新绕障、共用弹体及死亡改动已Release构建并入包。有限command冒烟观察到火弹、NPC对白、真实死亡文字、回城位置／资源和保存后尸体取回；发现并修复弹体表标注值加载失败及回城状态确认缺口。用户随后报告鼠标重复行走、NPC悬停／点击问题；最新修正按用户要求仅检查代码并打包，未做运行复验，不能以此前command证据认证鼠标操作。

## 流程与入口

自己游玩的后续源码：对象0x51保留InteractType，神坛按MPQ Shrines.Code与原TBL ShrId绑定真实名称；对象0x0E明确的TARGETABLE标志优先于MPQ Selectable回退。技能选择器F1–F8绑定发送原0x51，0x7B恢复16个原生槽中的前8个；本局偏好与服务器已读绑定分开，发送没有即时ACK，物品来源技能尚未开放。每局ProtocolReady后一次原0x40请求任务日志；任务0x28保存角色私有48项任务字，0x29独立保存本局公共任务字；0x52日志全量和0x5D增量正确区分任务编号／记录槽，0x50读取邪恶洞穴剩余数量等原进度。27项面板投影原服状态与MPQ文字，未核实的细分文字仍未知；旅行资格只读角色私有任务字。完整五幕任务、共享资格、奖励、凯恩石柱顺序和塔拉夏墓符号仍待收口。鉴定修正为原0x20准备、0x27目标，0x3F准备回包不代表成功；只等物品更新／消耗确认，不自动重试。

普通启动：主菜单 → Battle.net → 登录或注册 → Realm → 服务器角色 → 创建或加入游戏 → D2GS 协议加载 → 重建当前幕活动地图 → 服务端世界显示。Single Player 产品入口已移除；旧本地角色／读档／地图参数明确拒绝。客户端不读取本地 D2S，也不启动本地 GameSession。

| 页面／操作 | 当前源码 |
| --- | --- |
| 登录／注册 | 原 MPQ 图形、字体和文案；校验两次密码一致，SID 0x3D 成功后自动 SID 0x3A 登录；提交后记忆账号密码，下次进入登录页可直接Login；修改用户名立即清空当前和已记忆的密码；拒绝／超时后保留输入以便修改 |
| Realm | 首次自动选择配置项；未找到则显示服务器列表；Change Realm 关闭 MCP、重新取列表，选择后取新票据 |
| 角色 | 服务器列表分页；七职业创建、资料片固定开启、非 Ladder；Hardcore 先显示原警告；删除先确认，成功刷新；死亡专家与未知状态角色不能进入 |
| Create | 名称／密码／说明、人数 1–8、等级差 0–99；普通／噩梦／地狱依据服务器 native 进度解锁；成功自动取票加入 |
| Join | 真实房间列表、人数、选中说明；滚轮浏览、按名与密码加入；再次点 Join 刷新，列表等待可取消 |
| 等待／返回 | 独立 worker 持续收包／心跳，建局队列显示位置；取消列表返回大厅；创建／加入／连接游戏／握手等待取消时关闭旧 MCP／GS，保留账号连接并重新取服务器角色列表；已登录游戏的加载取消提交原0x69并有界等待退局；大厅 Quit、正常退局重新取票返回选角 |

`ProtocolReady`须原服加载完成、幕／难度／种子和本人GUID可用、本人单位名称／职业与所选服务器角色一致且坐标已分配；场景另须 `nativeMapReady`、`playerDisplayed` 与活动碰撞准备成功。左键提交移动或点击真实地图对象／NPC，R／空格或底栏按钮切换跑／走；跑走偏好在换区／换幕后保留。Esc先结束对白或关闭交谈／传送点面板，再打开退出菜单。NPC对白和菜单复用既有字体／菜单绘制；回城门创建本批提供command入口。新闻、广告、频道／聊天、账号设置、Ladder、转换角色和影片等非主流程入口暂缓。

角色名 2–15 字符，首字符英文字母，其余英文字母、连字符或下划线；classId 为 0 Amazon、1 Sorceress、2 Necromancer、3 Paladin、4 Barbarian、5 Druid、6 Assassin。初始属性、装备和 D2S 由原服生成；客户端仅提交 MCP 0x02 的职业／状态与名字。删角用 MCP 0x0A，command 要求 confirmName 完全匹配。注册账号／密码为 2–15 可打印 ASCII 字符，更细名字限制由服务器拒绝码说明。

肖像仅绘制已保存 native 1.13c、无组件染色且原 COF／DCC 完整的外观；legacy 新角、染色或未核实组合保留真实身份，肖像暂不绘制。建角使用当前 MPQ 七职业前端动画。

创建／加入／握手超时同样重新获取服务器角色列表，不自动重发建房或加入。加载超时先请求原保存退局；退局等待超时后关闭旧游戏连接并重新取角色列表，错误明确标记保存结果未知。协议损坏、账号连接失败等仍结束会话，失败提示关闭后回登录页。本批实际观察加载超时后原生退局等待及再次超时返回角色页、保存结果未知；创建／加入／握手取消和其余恢复分支尚未逐项连服验证。

## 共用游戏界面

底栏、背包／装备／腰带、人物／技能树／技能选择、箱子／方块／金币、NPC对白／菜单、普通商店、传送点、任务日志、佣兵和Esc／选项面板共用既有SceneView／SceneController及原MPQ资源加载入口，地面物品原图／品质色标签／拾取热区、鼠标光标和敌人血条同样共用；联网独立HUD已移除。联机通过RemoteUiClients实现同样的IActor／IInventory／ICharacter／INpc／IQuest／IMap端口，不构造离线GameSession；资源、几何、面板状态和命中处理不另写联网版本。网络 worker 在资源加载、窗口等待和界面绘制期间持续收包、心跳和更新权威副本；资源回调不再重入会话。客户端线程通过 tick 发布快照，后台线程不修改表现正在借用的容器。

已绑定底层命令的界面操作：左右技能选择与基本PvE施放、属性／技能加点、物品拖放／装备／武器组／腰带／容器转移／交换／堆叠／装书／镶嵌／鉴定、金币、合成、普通NPC交易／维修／凯恩鉴定及传送点。组合库存操作逐步等待服务器光标赋值；每步保留 UI 投影时的 OnlineIntentContext（连接／游戏／区域／本人／交互代次及 NPC），发送端在锁内复验；关闭后重新交谈同一 GUID 也使旧队列失效；快照保留原GUID／revision，失败或超时中止，未确认的位置、金币、属性和技能不写入副本。UI关闭消费原手势，侧栏变化同时调整世界视口，换区保留同一套面板对象，退局后释放角色快照和待处理命令。

未知原服属性和攻击面板计算标为`?`，不按单机规则补造。商店价格暂交原服决定并明确提示，quote 返回未知，canRequestSale 单独表达能否提交出售请求；UI不显示伪造的零金币价格。任务日志投影原生私有记录／日志状态，细分文字和完整资格仍有缺口。佣兵资料／服务、交易／赌博、任务物品特殊服务和分堆协议未接，入口禁用或反馈限制；原界面实现仍共用，后续只补适配器。F1–F8原生绑定及重入恢复见本页流程入口。完整技能效果、球体状态变色和物品染色仍待接。

地面金币取原数量，落地图读取当前Levels.Pal；敌人名字读MonStats.NameStr／TBL，生命按原0–128刻度并区分0x0C的暗金标志。物品Take组合等待真实光标回包及既有请求间隔，缺回包按会话超时结束、不自动重试。回城卷轴／书右键通过RemoteControl创建门户。联机Esc菜单不暂停服务端，Save and Exit提交退局；死亡回城与尸体取回接口已接，确认与验证边界见本页死亡章节。

本批Windows Release已编译、打包并进行有限原服冒烟，实际范围见下节；共用UI资源加载改为直接读取MPQ的Objects表，避免角色数据表集合缺该表时抛出map::at。底栏、营地、NPC对白与基础施法已查看截图；背包等全部面板的鼠标组合操作未逐项认证。当前入口和布局见[经典HUD](../gameplay/ui/CLASSIC_HUD.md#单机联机共用游戏界面)。

## 第一幕怪物联机接入

当前MPQ的Levels第一幕mon／nmon／umon池含57个身份；按下表逐类核对MonStats、MonStats2、Skills、MonSeq和Missiles。第一幕固定金怪、血鸟、铁匠、格瑞斯华尔德和安达利尔使用同一原服副本。原服负责追击／逃跑、攻击命中、元素伤害、复活／召唤、精英事件、经验／掉落和任务；联网不实例化本地怪物AI或结算器。

| 逐项范围 | 联机入口／当前表现 |
| --- | --- |
| fallen1–4、zombie1–3、skeleton1–3、fetish1 | 原0x67–6D移动／模式与攻击／受伤／死亡；SkeletonRaise按所属怪物SkXmode读取原MonSeq，复活以原服活模式确认 |
| fallenshaman1–4／Bishibosh | Resurrect与ShamanFire读取17步seq_shamanresurrect、A2释放帧；火球显示只消费原技能，复活尸体及阵营由原服决定 |
| foulcrow1–2、crownest1–2 | 飞行移动消费原身份尺寸／碰撞；Nest使用31步seq_nestlay，产怪只显示原服新增单位 |
| brute1–3、goatman1/2/3/5、corruptrogue1–4、cr_lancer1–3、hellbovine | 原组件及普通近战动作／目标／生命；精英和牛王仍用真实身份，不套本地AI |
| quillrat1–4 | MonStats.MissA2原尖刺、难度MonsterSkillBonus、A2释放；原aip3和SEIS表现种子控制额外射线 |
| cr_archer1–4、sk_archer1–3 | MissA1原箭、当前原动作释放；不在客户端判定命中或扣血 |
| bighead1–4、skmage_fire1–2、skmage_ltng1–2、Boneash | 原MissA1/A2闪电／火弹／毒弹及爆炸图；诅咒、毒伤和抗性完全由原服状态／生命驱动 |
| wraith1–2、arach1 | 原移动、攻击、死亡；SpiderLay普通A2，叠层取原服状态，客户端不添加毒伤 |
| vampire5 | VampireFireball／VampireMissile原弹体；ClientSend火墙maker／静止火焰和陨石center消费0x73的等级／剩余帧，客户端程序6／5／9共享原MPQ图形 |
| Blood Raven | 所属Nest使用20步seq_bloodravencast，Quick Strike使用7步seq_brquickstrike和原raven1；原服创建zombie2并处理死亡连锁，客户端不补造召唤 |
| Countess、Smith、Griswold及其他第一幕固定金怪 | CountessFirewall原0x73 maker及非ClientSend火焰显示；其余普通动作和原服诅咒／词缀／任务回包；0xAC固定hcIdx提供原TBL名称和三难度Utrans，8–37读取全局RandTransforms减8索引 |
| Andariel | 原seq_andarielspray18步、九个释放事件及D2MOO SrvDo088整数方向／偏移；AndyPoisonBolt原A1与原弹体，伤害及任务完成等待原服 |

冰冻原States.freeze停止表现路径和动作，不修改服务端位置；原hide尸体不再绘制，shatter使用共有原冰碎／冰融图及声音。TargetCorpse悬停和命令均检查当前MonStats2.corpseSel与原hide／udead状态，不能选血鸟／女伯爵等禁选尸体。动作通知过期不重放，移除／换区清理表现缓存，ClientSend弹体不从技能重复生成。

边界：未认证所有随机出生和战斗分支。火墙随机起帧／衰减、陨石精确客户端散布／斜向落轨、安达利尔及血鸟死亡专用演出、随机精英名称／染色、完整状态速率／透明和空间音频仍缺完整D2Client证据，明确暂缓；不以自造演出替代。scene.effectLimitations会记录遇到的不支持客户端程序，未出现诊断不等于全部行为验收。女巫30技能既有玩法模块没有重做，也不能据此宣称全部联网视觉完成。

本批已统一Windows Release构建及打包。用户报告行走回退及房间引用失衡后，暂停逐种怪物冒烟，转为角色行走修正；营地入局和有限野外行走不能认证上述全部怪物行为。材料入口为忽略目录artifacts/online-act1-monsters-20261006，未新增测试脚本、用例或专用程序。

## 世界绘制收拢（2026-10-06）

正常入口只调用RemoteScene的原服数据适配和SceneView::drawWorld。新增presentation/world/world_draw_view.hpp作为同步帧输入：只借用当前地图及已解析的Sprite，单位身份已转为EntityId，位置为当前地形的局部子格；不借用服务端执行器，不在跨帧缓存中保存这些指针。RemoteScene保留原服动作／外观／显示位置和鼠标意图的适配；资源解码已继续收拢到下节统一动画，本项不重做女巫技能或原服战斗结算。

world_renderer.cpp统一原DT1地板／下层墙／阴影、墙与单位排序、Objects.OrderFlag、配对墙方向、Warp与Pops／屋顶透明、人物阴影、掉落、客户端弹体／叠层、NPC提示。NPC提示前后层位于所属单位排序位置，随后接受屋顶和世界光照；交互文字、小地图及HUD在光照后绘制。侧栏裁剪与摄像机沿SceneView.worldViewport，返回实际可见DT1实例供AutomapExploration适配层揭示。删除RemoteScene的地形Graphics／tile缓存／排序与paint循环，以及旧SceneView.draw／drawTerrain／drawActors和无消费者的区域纹理缓存，不维护两个世界绘制入口。

SceneAssets统一读取原Levels、Objects.Lit0–7／RGB、MonStatsEx对应MonStats2.Light／RGB及Overlay灯光，地形上传共用graphicsForAct的原调色板。SceneView直接复用已有LightingView、室内遮光Grid及PL2明暗表，灯光从原服当前物件模式、存活单位与客户端效果位置投影；玩家使用既有基础半径13和已收到item_lightradius增量，未知装备加值不在客户端重算。新局清空瞬态环境，切区失效光照和纹理输入，同局的环境推进保留。未对齐原服昼夜时钟，日蚀任务状态尚未接入；天气、Flicker、动态Overlay半径及原客户端精确距离衰减仍沿[光照限制](../gameplay/world/LIGHTING.md)。

本项只做Windows Release构建和固定dist/current打包，未启动客户端、参考服或冒烟，画面与操作由用户验收；构建日志位于忽略目录artifacts/world-render-refactor-20261006。动画、世界手势／游戏内UI及人物／技能显示已继续收拢至下节统一入口，怪物音效已继续收拢至下节共用配置／播放模块，其他旧依赖仍待后续项；当时d2x_presentation仍链接d2x_session；该链接已在任务／面板及旧入口清理中解除，见下节。D2S、探索侧文件和地图生成规则未改。

## 统一世界动画（当前源码）

第二项沿已有Graphics合成器改造，统一入口为SceneView.actorAnimation／objectAnimation → SceneAssets → presentation/actors/actor_animation.*。content/character/actor_appearance.hpp只包含Token、Weapon与16个已解析组件代码，RealmPortraitParts沿用这个值类型。ActorAnimationRequest只包含外观、图形模式、原表变换和已核实的序列号，不借用OnlineUnit、GameSession、装备事务或网络容器。所有原图／GPU纹理继续由SceneAssets的graphicsForAct持有，缓存键包含幕调色板、组件、模式、武器、调色变换、阴影和末帧／序列标志；同一原图不再在RemoteScene另建Graphics。目录及帧组只读且跨帧稳定，单位动作时钟继续按既有新局／切区、单位移除及显示中断流程清理；测试暂停沿既有显式表现暂停入口。

角色和怪物／NPC读取原AnimData速度与释放标志，MonSeq按当前MPQ的mode／frame／dir／event采样原帧；非零方向覆盖及嵌套序列保持不可用，不自造近似；原声消费者从同一目录读取序列的首模式，不因原图暂缺而丢失原事件映射。闪电沿既有seqnum=12的19步SC采样和第7步释放，参考本地D2MOO SequenceTbls核对后迁移，不重做女巫技能。DD缺图只允许本人／本怪物DT末帧。MonStats2空组件、原服组件位流、rank／SuperUnique身份和Utrans转换继续由原服适配投影，真实身份、冻结／隐藏尸体和已有原声事件不改。

物件与地图装饰共用Objects的Mode／Draw、FrameDelta、FrameCnt、Start、CycleAnim、偏移与OrderFlag／DrawUnder。恢复原COF完整合成优先；没有COF时只读取同Token／模式的原TR DCC或DC6。FrameCnt和Start限制在原图实际帧区间，循环从有效起始帧推进，一次动画和死亡末帧显式停住，不靠GpuAnimation.frame的隐式取模。ActorAnimation.sample／sampleFacing集中方向和帧选择；ActorAnimationState集中原服动作代次、接收年龄补偿及冻结／解冻时钟。动画结束只切显示姿势，不改原服模式、生命或位置；本人施放补偿和回包取消／校正沿既有RemoteScene流程。

删除RemoteScene独立Art合成、Objects动画表、AnimData／MonSeq及序列缓存；删除hero_assets.cpp、monster_assets.cpp、hireling_assets.cpp、无调用者state_overlay_view.cpp和旧物件／门户／叠层帧缓存。旧GameSession版SceneView／SceneAssets构造器及离线预热入口也移除。剩余旧表现命中辅助只把只读外观／时刻交给同一个目录采样；旧localSession引用和d2x_session链接已在后续任务／面板清理中移除，历史包事实仍以各批范围为准。局前角色预览、HUD动画属于各自界面资源，此项不重新实现其行为。

2026-10-07已随人物／技能提示统一Windows Release构建并更新dist/current；未运行客户端、原服或冒烟，未编写测试脚本、用例或专用程序；入包不等于动画验收。装备逐层染色／透明、未接入玩家序列及完整原客户端加速／时序仍沿已有未认证边界，不因收拢缓存宣称补齐。地图、D2S、探索侧文件及规则指纹不变。

## 统一鼠标与游戏内UI交互（当前源码）

入口是SceneController::handle → RemoteScene::frame → SceneController::handleWorld。前者统一原面板／快捷键的优先级及按下至释放的捕获；绘制适配器只返回WorldInputView：归一化EntityId命中、原服全局subtile投影、显示观察点、当前左右手技能／来源和有效目标集合。控制器持有唯一的世界手势、锁定目标、技能选择及续发节流；RemoteScene删除Gesture、lockedTarget、repeat／pendingMove、请求反馈接口和FrameInput依赖。旧GameSession控制器构造／click／handle和独立地图点击分支删除，无消费者LocalActorClient也从源码和CMake移除；其他旧会话表现辅助与最终依赖退场仍属后续项。

左键按下按拾取、攻击／Shift原地施放、交互、行走处理；右键按下进入施放。目标在本次按住期间锁定，失效、技能／来源改变、释放或UI接管结束本次手势，不在仍按住时重选目标；行走经过怪物不会变为攻击。保留原0.12秒续发节流、行进中固定世界目的地、拖动改目标、显示到达后按住续走和拒绝后的移动重试。新按下清上一手势的捕获，关闭面板的同次按下不会穿透；技能快捷键切换后等待已按住按钮释放。失焦取消本地续发／导航并清面板手势，恢复焦点不复用按住输入；慢资源加载丢弃缓冲按键，但保留正在按住的行走。换局／换区和死亡清理旧输入，死亡ESC继续提交原复活请求。地图暂不可绘制时仍由同一个控制器处理面板／ESC／失焦，世界输入不可用，不复用旧命中。

IActorClient.move/control返回客户端请求是否接受，用于首次／Hold和移动重试；不是原服ACK。RemoteUiClients负责归一化ID／坐标到原请求的转换及当前领域上下文，RemoteControl仍负责原服移动／靠近资格，RemoteCombat仍动态读MPQ资格并负责原技能请求，拾取继续交给RemoteInventory。绘制的observer只作投影提示，原服位置、副本、移动校正和显示插值独立；未增加本地伤害、消耗或任务结算。

游戏内背包／腰带／装备、角色／技能树／技能选择、任务、NPC／商店／佣兵入口、传送点、底栏和游戏菜单继续使用共用控制器。原0x31对白确认及TradeOpen从NPC控制器经TalkToNpc意图提交，自动对白和菜单话题共用dialogueTextTopic；方块右键经UseItem，由联机适配器按MPQ opensCube转成CubeOpen，面板等待原服storage回复后开启；显式CloseStorage清未发送组合及无ACK的协调等待，同次关闭箱子再开方块使用关闭后的当前交互上下文；待确认传送点的ESC经MapClient.closeTravel。前端保留原服状态到面板的同步、登录／选角／建房和设备采集，不再拥有世界手势或上述玩法UI请求转发。未实现的赌博／组队／聊天／任务服务等仍按已有不可用提示，不因合并输入宣称新增支持；未重做女巫技能。普通ESC／失焦不暂停原服或网络，既有调试pause/resume保留。

依据本地Diablerie PlayerController::FlushInput／Update的释放隔离、D2MOO PlrMsg原0x05–11按下／Hold、0x12射流状态终止和0x31对白消息核对请求边界，未复制参考源码。2026-10-07已随人物／技能提示统一Release构建／打包，未运行或原服冒烟；完整鼠标／面板组合待用户验收。删除库存中无原服端口的旧单机插杖执行入口及手势调用；原服插杖UI仍未接入，保留现有能力边界。没有新增测试脚本、用例或专用程序；地图、D2S、探索格式及规则指纹不变。

## 统一人物和技能提示

第四项从原显示代码收拢至 client/character_projection.cpp → content/character/character_display.cpp。RemoteUiClients 仅投影原服已知属性、基础／有效技能等级、选择、绑定和难度，共用生成器读取已导入的 MPQ 定义并生成 CharacterView；角色面板、技能树、技能菜单和 HUD 共用这些提示。删除 LocalCharacterClient 和联机适配器内的简化提示生成分支，不保留单机能力。法力、当前／下一等级、前置要求、持续时间等复用原描述／纯公式；缺装备、加成、宠物或未支持描述输入时显示 `?`，原服未确认的技能等级不冒充 0。0x94 完整清单明确已知零等级，经验门槛读取原 Experience／CharStats。详情及剩余数值限制见[人物模块](CHARACTER.md#本人-ui-投影)。

同时修正本人移动到普通施法的显示交接：LocalCast 起手时将当前连续显示位置保留为停止位置，清除旧显示路线和旧请求的推进资格；取消移动请求不再触发回退到最近整数坐标。迟到的已走路径采样不重拉角色，旧原服行走目标不重启行走；新移动、受击／死亡动作及明确位置校正仍沿原权威流程，0x15／原位移中断直接清理交接。依据本地 D2MOO PlrModes::PLRMODE_StartXY_AttackCastThrowKickSpecialSequence（仅改路径目标并切动作）及 PlrMsg::sub_6FC81D20（通常不通知本人），不通过延长插值掩盖回退。原协议没有样本时间戳，迟到路径识别依赖显示历史与路径方向，未认证全部延迟和路径组合。

本项仅构建／打包，不运行游戏或原服、不做冒烟、不编写测试程序。提示生成不调用旧单机执行器；伤害、扣蓝、升级、任务奖励及保存由原服结算。D2S、规则指纹与 MPQ 资源不变。2026-10-07合并批次Windows Release构建成功，dist/current包含动画、鼠标／UI、提示及施法位置交接修正。构建记录为忽略目录artifacts/character-hints-refactor-20261006，最终增量日志build-release.log；前期合并接口编译失败的日志保留，已补齐声明／移除旧执行路径后成功链接。构建仍有既有聚合初始化及GCC optional／容器警告，不宣称无警告。包内EXE／网络DLL与构建产物SHA256一致；未做运行认证。

## 统一音效定义和播放规则

第五项采用唯一 MPQ 配置入口 content/audio/sound_catalog.*。原 Sounds、MonSounds、MonStats、Skills、CharStats 在这里解析为只读声音定义；MonStats.hcIdx 映射真实身份，空 MonSound 保持原无声单位，不使用敌对替身的声音。攻击声选择／Att1Prb、Att2Prb，武器声／Wea1Vol、Wea2Vol，各动作延迟，Skill1–4，脚步数量／概率和待机 NeuTime 均从当前 MPQ 导入。延迟及待机使用既有25Hz单位；武器音量沿参考 MonSound／AudioManager 的覆盖参数，普通声音使用 Sounds.Volume，保留已有全局增益。

RemoteScene 只把原服动作、技能及0x2C升级通知转换为 PresentationSoundEvent，并提供 SoundActorView 的身份、动作版本、可听范围、移动／冻结状态和共用动画周期；不再读取或解释 MonSounds，不持有音效随机流、待播队列或脚步／待机时钟。SceneView 交给唯一消费者 presentation/audio/scene_audio.*：选择、概率、延迟、动作中断、单位移除、视口范围及周期声音在同一模块处理。技能起手与释放声使用配置和公共动画释放时间；本人已有施放补偿及原服强制同步的防重复流程保留，不重做技能。新局／换区清除待播与周期状态，长加载或显式表现暂停不补播旧事件。

删除旧 actors/monster_audio.cpp、SceneAssets.MonsterAudio、旧 SceneView.advance 中独立角色／怪物声音及脚步／待机推进，以及 RemoteScene 内 MonSounds 解析、队列和概率逻辑。UI语义声、客户端弹体命中／释放、碎冰也经同一 SceneAudio 注册／播放入口，SoundBank只保留原声组和设备后端；一次性声音按原 Sounds 身份合并缓存／Compound／Stop Inst／Defer Inst状态，不因不同事件名称再次加载。循环弹体声音继续使用原PCM混音器、WAV smpl循环、原淡入／淡出及实体生命周期；不另建联机混音器。

脚步沿已有“当前移动动画周期／FsCnt”节奏，加入原 FsPrb 和 FootstepLayer；不宣称已还原原客户端精确帧相位。非零 FsOff（如原 Duriel／Vile Mother）语义未被参考实现确认，保留定义并停用相应脚步、给出限制，不自造偏移；角色的地面材质／装备轻重与普通武器音效选择缺少已确认输入，不沿用旧固定 heavy_run_dirt／单个挥击 WAV。UMonSound、CvtMo／CvtSk／CvtTgt、Init／Taunt／Flee等未接原事件的规则仍未完成。原Sounds中当前后端不支持的一次性循环／Duration／淡入淡出程序继续明确报告，不用普通片段替代。NPC对白、背景音乐及其他尚未接入的声环境不在本项新增范围；不宣称全部原客户端音频程序已对齐。

2026-10-07 Windows Release合并构建并更新dist/current。只读MPQ核对及构建／打包记录位于忽略目录artifacts/audio-unification-20261007；未启动游戏或参考服、未运行或冒烟，未编写测试脚本、用例或专用程序。声音和时序实际验收交用户；包内EXE及网络DLL与构建产物SHA256一致。构建保留既有GCC optional／容器和聚合缺省初始化警告，不宣称无警告。原服玩法、地图、D2S、探索侧文件及规则指纹不变。

## 任务面板与旧本地入口清理

2026-10-07 当前源码：任务页、NPC／商店／佣兵、库存、人物、地图和菜单均沿同一 SceneView／SceneController；删除未使用的 LocalInventory／LocalQuest／LocalNpc／LocalMap 客户端及其商店／佣兵投影、d2x_local_client 目标。原服本人状态与旗标适配为 QuestProjectionInput，client/quest_projection.cpp 唯一选择当前 MPQ 标题、说明和原图槽；缺洞窟数量显示 ?，不读地图种子、游戏共享旗标或旧任务执行器。原服始终负责进度、资格、NPC 服务、奖励与保存。

共用任务 UI 允许已完成条目查看说明；首次未知→已知的条目建立动画基线，已知状态转换触发完成动画与日志通知。原 newquestlog 文本由共用资源入口读取；不重放首次收到的已有完成记录。未接入的真墓符号、后续幕特殊文字／协议及佣兵／任务物品服务继续保留限制，不由本地模拟补齐。

删除 SceneView.localSession／advance／sessionRestored、旧本地事件消费者／HUD／物件与尸体命中／物品提示、旧小地图探索状态／投影／预热和 IMapAssetSource；清除无消费者的本地调试命令及存档侧文件代码。minipanel、库存关闭、弹体声音和佣兵面板不再按单机／联机切换路径；当前在线测试暂停、原服 command 及已有显示补偿仍保留。表现库改为链接共用 content／world，解除 d2x_session；人物／任务纯显示函数编入 d2x_client。

任务规则参考核对 D2MOO Quests.cpp 的本人 0x28／0x52 与日志状态、OpenDiablo2 quest_log.go 的原字符串选择，文字／图片继续来自当前 MPQ。本项最初只改源码；随下节最终依赖清理统一构建、打包与有限冒烟。旧宿主曾隔离在默认不构建目标，现已删除；D2S 编码、地图与规则指纹不变。

## 最终依赖清理

2026-10-07：删除已被替代的 Local 入口及 GameSession、Simulation、SkillRuntime、InventoryService、本地任务／AI／战斗／奖励执行源码，移除 d2x_session 目标。共用 gameplay／items 只保留联机显示、几何、原资源报告和独立存档工具的纯函数及值类型；混放的元素伤害显示、技能等级和库存错误文案独立提取，内容加载移除依赖库存服务的装备计算入口。人物／任务投影位于 d2x_client；presentation 链接 client／content／world／remote_scene，客户端不链接 persistence。

删除无原服生产者的旧法杖插入面板、状态和资源加载，缺失原服插杖流程仍明确未实现；删除旧本地调试输入、旧 CLI 别名、旧地图／动画／音效重复缓存及单机切换分支。LocalCast 名称表示本人施法显示衔接，保留原服预测／校正用途，不是本地结算。测试 pause/resume、在线 command 和独立资源／存档工具保留。

CharacterSaveData 定义移到 persistence/character_save.hpp，字段与 D2S v96 编码不变，不更改格式、语义或规则指纹。原 MPQ、reference、旧压缩包、mvp 和用户文件保留；参考源码／原表／资源不纳入提交。完整依赖图见[架构](../architecture/OVERVIEW.md)。

Windows Release 构建及 d2x／d2x_assets 链接成功；构建图无 d2x_session，两个可执行文件均未发现 GameSession／Simulation／SkillRuntime／InventoryService 符号。2026-10-07 已更新 dist/current，实际包内 EXE SHA256 为 `A7B767F8AFE9E0195F64EFC132BB0A3B8604CDA0CE86985A3B879520AE58B3E2`，协议 DLL 为 `E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。证据在忽略的 artifacts/dependency-cleanup-20261007，使用既有程序／调试管道／本机参考服，不编写测试脚本、用例或专用程序。

有限冒烟只操作新建独立账号 Clr010757／一级女巫 CleanOctSor，未读写用户测试角色：

| 观察 | 本轮证据与实际边界 |
| --- | --- |
| 入局与世界 | 注册、建角、非 Ladder 单人建局成功；营地本人可见，Warriv 原图可见，鼠标点击收到原服对白；城镇大地图、人物／技能树／任务／库存与 ESC 菜单截图已查看。初始营地 renderedUnits=9、unavailableUnits=2，不能认证全部城镇单位／完整自动地图选项；野外该采样 unavailableUnits=0 |
| 原服移动 | 共用鼠标输入从 `(4393,4548)` 移动到 `(4400,4548)`，原服确认；经出口分段行走到 Blood Moor `(4528,4565)`，地图／人物正常显示。不是完整寻路验收 |
| 施法与交接 | 重新选择 Fire Bolt 后原服 owner 确认为 0xFFFFFFFF；command 和鼠标右键均耗蓝35→32，施法与原弹体截图已查看。同次鼠标移动→施法显示位置 `(4538.186,4557.405)` 保留，650ms 后未回旧位置，原服位置为 `(4538,4557)`；未覆盖延迟／丢包、全部技能或怪物击杀 |
| 普通菜单／测试暂停 | ESC 菜单时 presentationPaused=false，原服收包8297→8761字节；显式 pause 期间收包8761→9564，resume 成功。原服未暂停 |
| 原服保存重入 | 通过 ESC 原菜单 Save and Exit 返回角色页；再次建局成功，一级／40血／35蓝／7件物品和已确认 Fire Bolt 选择保留。仅同进程重入，未追加新进程认证 |
| 独立存档工具 | 首次保存前原服建角文件的无效头／校验被拒绝；原服保存后的 save-info 正常解码 D2S v96。读前／读后 SHA256 均为 `3AAD8DF3FAB4BD3F499276F922AF7BCF86F31FB36751F78D34D2DA637DA74D28`，文件未修改；CharacterSaveData 定义与搬迁前相同 |
| 收尾 | 正常退局返回 CharacterSelection，客户端退出码0、stderr为空；本轮启动的参考服已通过已有 Stop.ps1 正常停止。没有改原资源、reference、旧包、mvp 或用户文件 |

本轮观察到两项限制：`(4469,4579) → (4506,4580)` 的长距离绕墙请求停在 `(4478,4576)`，随后报原服位置无进展超时；从门口 `(4476,4565)` 分段通过桥梁可出城，前一条路径不计作通过。新角色原服初始 Fire Bolt 选择 owner=0，普通施法资格暂拒绝；重新选择后收到 owner=0xFFFFFFFF 可正常施放，重入保留该确认。没有为这些情况恢复本地执行器或猜测来源。声音设备与资源路径随普通窗口运行，无错误限制采样；未人工听音，不认证完整音效／时序、多人、Linux、任务领奖或升级。



## 联机游玩表现与输入（当前源码）

NPC／小地图改造：MonStats2启用的组件若变体列表为空且原服组件索引为0，按D2Common空组件代码省略该层，不再丢弃整个人物。当前MPQ的Warriv1和Charsi启用S1但S1v为空，原TR图保留；非零非法索引仍拒绝，不采用替身或猜测变体。RemoteScene已删除独立COF／组件路径预检，资源组合和完整性由共用Graphics::composite处理；显式组件不回退成其他装备原图。

小地图不再维护联机绘制器及DC6／城镇拼图缓存：RemoteTown把原服房间、坐标和单位转成共用MapSceneView／AutomapDrawView，AutomapExploration沿原规则负责城镇全揭示和野外可见格记忆，AutomapCatalog统一原格标记及配对墙方向，SceneAssets统一原图缓存，SceneView::drawAutomap统一大小图、侧栏视口、平移、淡化、名称及人物标记。首次入城只准备已有原AutoMap房间的DT1标记或原整图变体，揭示规则不再单独实现；连续区／隔层和原服可见格选择仍由网络适配器提供。同局返回保留、新局清空，D2S及本地探索侧文件未改。小地图、NPC合成及世界绘制已收拢，远端动作／事件适配仍有独立入口。已随行走修正统一Release构建／打包，营地人物／NPC和野外地形有限运行正常；完整小地图选项及NPC图层未逐项认证。

单人行走／任务／成长：长按左键在原服到达当前目的地后续走，行进中固定世界目标，拖动鼠标才改目标；释放、失焦和UI接管终止续发。分段行走及NPC靠近只在原服位置确实朝当前目标推进时刷新15秒无进展期限，持续前进不因整段耗时被取消。基础动作资格按当前CharStats.Skill 1–10与Attack准备，不伪造技能等级；学习校验原reqlevel＋已学基础等级。PlayerClass／CharStats的Expansion分隔行不占职业编号，扩展职业的选择、经验表、显示移动速度及受伤声音采用实际源行。

任务话题与自动对白都提交原0x31，已确认话题不重复提交，多段自动对白按未确认集合依次显示。NPC原TBL开头独立数字行是原a1npc SPEED元数据，对白显示移除该行；通用字符串查询保留原文。0x8A原服NPC任务提示复用MonStats2高度和原npcAlert，共用资源构造入口加载该原overlay；交谈／单位移除／换幕清理，不能代表客户端任务资格。27项日志完成状态驱动共用完成动画。原0x2C事件2播放MPQ cursor_level_up；原绝对生命／法力／耐力stat替代较旧紧凑采样，由MPQ ValShift换算显示。原0x1A／1B经验增量和0x19金币增量允许从零基线开始，0x1C经验绝对值仍替换；升级加点／回复完全由原服决定。沿用已有女巫技能模块，没有重做技能整页。

已统一Windows Release构建／打包，最终build/bin与dist/current EXE的SHA256一致，证据保留在忽略目录artifacts/online-progression-20261006。有限原服冒烟确认连续长按到达后续走及释放、营地／鲜血荒地换区、NPC原任务提示和连续0／75对白分别确认、清除数字元数据后的原对白截图、火弹击杀僵尸／硬毛老鼠、经验84→117→210→276并正常保存重入、金币0→1、法力神坛回复、怪物伤害及ESC死亡回城40血35蓝。最终包另确认慢走超过16秒仍继续推进并到达终点、换区和真实击杀沉沦魔使经验276→348。初次NPC提示资源缺失导致除零崩溃已修复，旧崩溃证据保留。升级实际发生／点数分配、任务完成动画／领奖、扩展职业及完整五幕任务未认证；用户接手后续实机测试，当前测试角色已正常退出。没有增加测试脚本、用例或专用程序。

单人PvE补齐：怪物原Action及Skill通知都消费MonStats.MonSound／MonSounds，攻击语音概率、攻击／武器／受击／死亡延迟读当前MPQ；Sounds原声组、Compound及Stop／Defer由共用SoundBank处理，按可见范围提交，打断、移除、换区和表现暂停恢复丢弃过期待播事件。女巫等技能读取stsound／dosound及延迟，本人请求和强制同步去重；0x0D角色受伤／死亡动作消费当前职业原声组。尚无完整空间衰减、武器额外音量和循环技能声音，缺原资源／不支持声音程序进入effectLimitations。

生命界面区分ESC回城和死亡存档重入：ESC仅发原0x41并等原服恢复位置／资源；直接退局或离开期间死亡后的新局保留原服生命／法力采样。SCmd把生命固定点值右移8位，初始零整数采样配合原服存活动作时，HUD显示1血而不是进入死亡等待；副本仍保留原始0，真正死亡由动作／模式驱动0血。不在客户端补满生命或法力。

女巫Lightning／Chain Lightning的原seqnum=12已接D2Common 19步SC采样和第7步释放；图形／基础帧速仍取本人实际装备及AnimData。Lightning沿现有原cltmissile发射，Chain Lightning的连锁目标跳转仍未接，不画假连锁；其余SQ包括Inferno仍待核实。绑定快捷键不再中断已开始的本人显示动作。敌人悬停／锁定／弹体目标统一核对原服alignment状态，怪物生命0x0C的暗金位先剥离再判断死亡；请求适配器也使用同一生命语义。

MonsterMsg虽然给S1／SEQUENCE相同的静态动作号12／13，但SEQUENCE在此前已经改发技能通知或直接返回，不会发该原动作包。因此原12／13明确映射MONMODE_SKILL1=8及Skill1原声，沉沦魔／僵尸这类S1动作不再错误回退到入局姿态；真正序列仍按技能证据另行解析。

本批Windows Release构建／打包，实际包与build/bin EXE SHA256一致；未增加测试脚本／用例／专用程序。忽略目录`artifacts/online-pve-20261006/`保留独立一级女巫FoundationSor的原服证据：Fire Bolt真实耗蓝，Zombie class5受击／死亡／尸体由command确认；鼠标按住右键火弹击杀Fallen class19，原13号单位收到动作8／9。两类怪物原A1／A2目标均为本人，生命实际下降并进入死亡；ESC原0x41回城资源恢复曾观察40／35。直接死亡退局后最终包新进程原服NU／Alive、生命整数采样0与法力35，HUD截图明确1／40血，最大法力35；鼠标取回法杖与护身符，Akara原服恢复40／35，正常退局D2DBS确认保存。尸体取回不补满生命。图形／原声组资源及提交路径无effectLimitations，声音没有人工听验；闪电序列没有相应技能角色实战，完整鼠标失焦／长按组合、持续射流、连锁及其余职业未认证。

参考D2GS另有一次后续握手未进入角色加载，以及一次watchdog明确记录可能死锁、保存所有房间后结束连接；这些失败不计通过。确认无在线角色后仅恢复本机D2GS，最终新进程重入和保存成功。客户端不修改参考服规则／角色档案，未认证其长局稳定性；离开期间服务器死亡的精确时序尚未单独制造，直接死亡退局路径已验证。

本轮Windows Release已构建并更新`dist/current`。原服继续执行移动、AI、施放、伤害、掉落、NPC服务及保存；客户端只提交原命令，位置／生命／法力／库存按回包更新，动作及基础弹体属于显示层。

- 玩家：技能0x4C／4D／99／9A更新当前动作、技能等级及目标；与0x0E实际模式、普通玩家动作字节分开，按Skills.anim和原COF／AnimData绘制普通攻击、施法、受击、格挡、死亡。连续相同动作按actionRevision重新起帧；动画结束只回到显示待机，不修改原服副本。死亡共用职业基础组件，缺DD时停在原DT末帧。
- 本人动作：D2MOO PlrMsg::sub_6FC81D20通常不向本人回发技能动作，故已发送的Cast请求单独驱动显示层，复用Skills.anim／castoverlay和基础cltmissile；Hold不在释放帧前反复重启动画。近战按当前MPQ武器rangeadder与目标尺寸等候原服靠近，位置显示复用已有活动碰撞；受击、死亡、移动及换区结束旧动作。强制本人技能同步替换显示动作且避免重复效果。原0x0D动作19是位置校正，不再误当死亡；原死亡／尸体仍依8／9及实际PLRMODE处理。发送成功不是施法成功回执，显示不扣消耗、不计算命中或伤害。
- 输入：首次按下锁定走路、交互、左技能或右技能；走路经过敌人不会自动攻击，单位目标按GUID锁定。首个实际发送成功后才转为Hold；松开、失焦、UI接管、技能改变或目标死亡／消失结束发送，并提交原0x12。停止不覆盖待确认的学习／选择／加点请求；它仅有原服地狱火停止语义，不承诺撤销任意技能。尸体技能按MPQ TargetCorpse选择。
- 沉沦魔／僵尸：保留原classId、MonStats／MonStats2组件、TransLvl调色板，显示原服行走、攻击、受击、死亡与尸体。无组件位表示所有组件索引为零，不能因资源有多个候选而拒绝；特殊怪物／佣兵标志按SCmd的有界前缀消费，不再让单位消失。固定金怪名称／三难度染色已接原0xAC；随机精英完整名称与染色仍待核实。本轮不新增客户端AI或掉落模拟。
- NPC：使用真实身份及同一原服动作／连续路径显示，服务仍走既有靠近、对白、菜单、交易／维修／鉴定和旅行命令；中立单位不替换成怪物。停止0x6D清掉旧坐标与单位目标，避免NPC继续朝过时目标显示移动。攻击资格仍由MPQ和原服alignment状态检查。
- 物品：复用同一套地面原图／标签、光标、背包／装备／腰带、箱子／方块与商店UI及27种原命令。0x16由原服执行靠近后拾取；客户端记录请求中的地面GUID供连续移动显示，拾取后以物品回包更新库存。拾取、施放取消旧NPC靠近意图，避免迟到的交谈抢走输入；不先向本地背包生成物品。
- 效果：共用SceneAssets的MPQ弹体定义、原DCC／PL2／帧率／方向／透明度、SceneView.drawMissile／drawSpellOverlay与命中碎片、视觉生命周期。按castoverlay、0x11及已解码States.overlay1／2显示叠层；状态由原服解除。单机飞弹的运动／伤害仍由玩法驱动，联机只生成显示实例；共享资源和绘制不调用离线战斗执行器。
- 发射链（最新源码）：按原AnimData释放帧创建，本人已发送请求补足原服省略的技能通知，强制同步避免重复。ClientSend只消费0x73的原位置、首路径目标、剩余帧、等级与穿透计数；该包没有弹体GUID。支持pCltDoFunc=1／5／6／8／9／18／19／20，按25Hz推进，速度取原Vel／VelLev的75%定点结果，加速度每五tick更新。充能弹复用chargedBoltPath；牙／多重箭复用整数扇形，新星复用原64方向格点；普通弓弩／投掷取真实装备的MPQ弹体映射。闪电／骨矛按CltSubMissile及分段数生成拖尾，冰封球按客户端参数发射、旋转和末端散射，撞墙不产生末端散射。
- 接触表现（最新源码）：共用Grid的原尺寸地形／对象阻弹射线及单位相交几何，显示插值也检查阻挡；只选择原表和已解码alignment允许的PvE目标。CollideKill、原服穿透计数与命中特效只控制画面，穿透后仍检查同段墙体。未收到穿透次数时不掷装备穿透概率。没有本地命中／伤害／抗性／生命／法力／状态／掉落结算；显示接触不代表原服命中。施法打断、单位移除和换幕取消尚未释放的显示弹体，已飞出的效果独立结束。

尚未认证完整原版客户端表现：引导箭／骨魂追踪、连锁跳转、炮轰序列、持续射流、毒云／地面药瓶、其余客户端程序和慢速箭等速度状态仍待接入，不替换成普通直线飞弹。火球CltHit1附加爆炸密度、CltHit14碎冰数量／偏移及完整客户端随机流缺少原客户端证据，当前显示主爆炸与既有一份碎冰适配。精确定点路径、完整动态照明／空间音频、装备／状态对动作和移速的完整修正、装备逐组件染色／透明、精英名称与染色、佣兵／宠物及复活闭环仍有限。scene.effectLimitations列出遇到的不支持程序／数量公式／0x73标志，不表示未列项均已认证。历史冒烟不认证最新弹体源码；怪物优先沉沦魔和僵尸，其它真实身份沿原资源尝试显示，缺资源不构造替身。

本轮有限冒烟使用实际`dist/current/d2x.exe`、既有command和本机参考服，仅操作独立19级女巫`bomb2/NetCombatSor`，未编写测试脚本／用例／专用程序。证据在忽略目录`artifacts/online-play-smoke-20261006/`。登录、非Ladder建局、营地／底栏和Warriv对白／关闭通过；按碰撞绕行出营地进入Blood Moor，Teleport位置与区域切换确认。Frozen Armor状态10及本人SC／原叠层、Fire Bolt本人SC／原直线弹体已截图检查。沉沦魔行走／攻击／死亡、僵尸行走／攻击／受击与普通Attack击杀收到原服更新；僵尸非致命生命比例11，死亡为0，场景本人可见且该战斗截图缺外观计数为0。

原服掉落127支Bolts拾入本人背包，新进程重入保留数量127；后续拾取僵尸掉落Stamina Potion、使用腰带生命药剂（服务器删除物品，生命恢复64）、消耗原回城卷轴创建实际门户并返回营地通过。正常退局，D2DBS确认CHARSAVE／CHARINFO并解锁；既有save-info读回19级、体力已分配3点、未用属性87及经验500002。修复后的基础效果和保存分别用新进程复验；参考D2GS有一次后续入局不返回握手，只有建局／取票成功，未算通过，确认无人入局后仅重启D2GS恢复。此证据不认证参考服长期稳定性。

本轮主要通过command与状态／截图检查，未认证鼠标按住、松开、失焦的完整手势，全部面板组合操作、地狱火停止效果、其余职业／复杂技能及精英染色。NPC对白当前直接显示TBL原文，首行数值尚未按原语音文本格式整理；选角日志还有`sohdbrstnhth.dcc`缺组件提示，实际入局法杖角色与上述场景显示通过。27种物品／城镇command仍共用原协议入口，修理等无需逐个点击UI；本轮不重复此前全部商店／维修组合。

导航新增源码：绘制帧的上下文显式传到 RemoteControl／RealmSession；分段行走和 NPC 靠近保留发送后的交互代次，切区、死亡、交互变化或单位失配时取消待发部分。待确认的物品操作阻止移动、旅行和切换交互，门户创建与物品操作互斥；心跳继续运行。原服已开始的移动不能靠取消本地意图撤销；原0x12仍只保留既有停止语义。本批未构建或认证这些新行为。

## 联机死亡与尸体（最新源码）

- 死亡依据原0x0D动作8／9和0x0E的PLRMODE_DEATH=0／DEAD=17，保留Dying／Dead。资源回包只以正生命到零的实际变化补足死亡开始；死亡存档重入时的初始零生命不能覆盖原服站立动作、锁住尸体取回。正生命回包不能自行解除既有死亡。D2MOO `sub_6FC82360`可省略生命零值通知，故不能只用life==0驱动界面；动作19仍是校正。只读`world.dead/deathPhase/deathRevision`共用于UI、移动、战斗、物品和交互资格。
- 共用单机死亡文字、原字体、死亡动作／DD缺图时DT末帧及UI关闭逻辑。死亡结束持有手势、分段移动、待发施法与物品组合，关闭所有玩法面板／对白并屏蔽输入；原服仍持续运行，怪物和其他玩家照常显示。地图暂时不可用时仍显示死亡提示并接收Esc。
- Esc／`online-resurrect`只建立回城请求，先等待服务端DEAD，再单次发送原0x41；收到原角色重新分配／存活站立模式及位置，或0x41之后的三项绝对资源恢复与0x15位置校正组合，才解除死亡。旧资源样本清掉后读取原服绝对属性／后续0x95，不把生命、法力或耐力设为本地最大值。`world.respawnRequest`给出WaitingForDeath／Sent／Confirmed／TimedOut及sent；重复Esc不重复发包，超时保留死亡状态、不自动重试，迟到的原服确认仍可消费。Hardcore继续操作走正常退局，不能复活；角色死亡位与尸体持久化由原服保存。
- 原0x8E记录／解除corpse GUID与owner的关系，独立于本人死亡状态和空间单位分配，跨区保留归属、重新加载真实可见单位。尸体使用同一职业原DD／DT末帧、名字与MPQ `corpse`文字、高亮；本人仍在死亡位置时避免自身与新尸体重叠绘制。`world.corpses`给出GUID、owner、owned与当前可见坐标；只有本人的真实可见尸体成为`scene.mapTargets`的type0／corpse目标。
- 点击尸体或`online-recover-corpse`（unitId）复用共同绕障／分段靠近，原服距离≤8后发送0x13 type0交互。accepted不等于装备已取回；库存、装备、经验、金币及0x8E解除／单位删除都等待原服回包。空间单位暂时隐藏不擅自删除归属；部分回收后原服未解除的尸体继续保留。未开放其它玩家尸体的loot权限或交易入口。

本批没有改变离线死亡结算、D2S或规则指纹。包内独立一级女巫NetDeathSor实际受怪物攻击死亡，死亡文字和移动拒绝通过；command回城让原服恢复40生命／35魔法／74耐力并返回营地。初版未清除客户端死亡状态，后补接三项绝对资源更新与0x15的组合确认，修正后未重新完成同局死亡回城验证。保存后新进程营地尸体出现、0x13取回法杖及0x8E解除通过；一次重入加载超时，仅重启D2GS后恢复。真实Esc按键、完整鼠标操作、多尸体／满背包部分回收及Hardcore未认证。材料在忽略目录artifacts/online-death-smoke-20261006，后续运行由用户验证。共同规则见[玩家死亡](../gameplay/characters/PLAYER_DEATH.md)。

## 战斗与成长底层

普通攻击、左右技能选择、坐标／单位施放、单次Hold请求和原0x12停止接口由command调用；共用游戏UI已接基本PvE施放、选择和成长。技能、目标、职业／前置／等级／属性／最大等级读取当前MPQ，服务端仍检查最终资格。单位目标首批限PvE敌对怪物／合格尸体，NPC、中立／友方、玩家与佣兵目标拒绝。普通Attack依据D2Common原初始化规则识别，innate独立标记，不伪造技能回包等级。

- 0x21分开保存基础与装备加成，有效等级为二者之和；0x94更新基础等级但保留已收到的加成。0x23分别维护左右选择及原owner。普通技能0x3C使用owner=-1；初始选择未确认正常owner时先显式选择，不支持充能物品owner。
- 0x3B学习／0x3A属性加点等待基础等级／绝对属性变化；Confirmed表示观察到对应服务端结果。Pending互斥物品、回城门及其他成长请求；100ms限流、死亡／换幕中断、超时结果未知，不自动重试。
- 施放／停止没有通用原生ACK，记录SentNoAck。客户端不扣生命／法力、不算伤害、不按施放意图生成战斗单位。Hold仅提交一次，后续调用方决定是否持续提交；0x12原处理仅清地狱火状态，不能声称通用撤销技能。
- 接原0x0C命中、原玩家动作／死亡、0x67–6D怪物原动作及权威位置、0x11叠层、0x4C／4D／99／9A／A3技能和0x73飞弹消息。原怪物动作字节转换为MONMODE，12／13歧义保留wireAction；0x67是目标位置，0x68是当前位置和目标GUID。生命比例保留原字节尺度。
- 战斗事件队列最多256条，按sequence有序；0x73无权威飞弹GUID，保留类型、所有者、当前位置／首路径目标和pierce，不伪造实体；首路径点改名为missileDestination，避免误当出生点。已接范围与表现限制见本页“联机游玩表现与输入”。
- A7／A8／A9／AA状态消息按单位有序保留；RemoteCombat依当前States／ItemStatCost的Send Bits、Send Param Bits、Signed解码，不套D2S Save Add或ValShift。状态队列最多64条，缺前缀／未知参数明确decoded=false，等待完整AA恢复，不猜状态。
- 1A／1B经验正增量、1C绝对经验；绝对等级与可用点仍读原1D–1F。全部职业技能、PvP、宠物／佣兵目标和充能／特殊物品技能仍有限；死亡／回城／尸体的有限证据与未复验修正见上节。

本批未改离线GameSession、D2S、地图或规则指纹；Windows Release已通过。证据在忽略目录`artifacts/online-combat-20261006/`。仅新建bomb2/NetCombatSor，先确认原服初始法杖Fire Bolt基础0／加成1及技能选择；正常退局后以既有离线成长／学习入口准备该独立角色的19级D2S用于赶路，原服账号和其他角色未改。原服实测体力一次加3点确认、Fire Bolt 2→3学习确认、Frozen Armor状态10及原属性、普通Attack和Fire Bolt击杀、Zombie非致命生命比例85、玩家受伤、Teleport权威位置和经验500000→500001。Hold／0x12已提交，但地狱火状态开关未观察到，不能据发送认证停止效果；1B／1C、其余技能／飞弹回包布局已核对reference，未逐包实测。D2DBS确认保存，既有save-info读回体力3点、未用属性87、经验500001；新进程重入先遇参考服加载超时，仅重启D2GS后成功；读回体力13／未用属性87、Fire Bolt 3／Inferno 1、未用技能11及经验500001，证据认证有限战斗／成长持久变化，不认证参考服长期稳定性。已入包；本轮动作／效果与游玩新增冒烟见上节。

## 配置与职责

复制 [配置模板](../development/online.example.json) 到 online.local.json，或使用 `--online-config <路径>`。originalClientDirectory 相对配置文件解析，含同一 1.13c 的 Game.exe、Bnclient.dll、D2Client.dll，只读计算版本和 CheckRevision，不执行原 DLL。默认 authentication=pvpgn，SID_AUTH_CHECK 零 key，本机服无需 CD-key；严格服可显式设 keys。打包只复制 PvPGN 连接设置，私有配置不提交。

UI登录记忆由应用层[OnlineLoginMemory](../../src/app/online_login_memory.hpp)管理，按配置文件路径隔离。Windows使用当前用户系统凭据管理器，Linux使用配置旁权限0600的`.credentials.local`文件；密码不写入连接JSON、会话只读视图、日志或D2S，记忆文件已排除Git。修改用户名时保存新账号及空密码；返回菜单／取消登录保留输入，注册页返回恢复已记忆信息。`online-login`命令不改变UI登录记忆。保存失败会显示提示，但仍提交登录请求。已随共用游戏UI批次编译／打包，未追加运行验证。

本机账号入口 127.0.0.1:6112，Realm D2X-Local，游戏端口 4000；Realm 地址由 SID 返回。原版目录 D:/Downloads/Diablo II V1.13C。测试账号 **bomb / 1qaz2wsx**、**bomb2 / 1qaz2wsx** 经用户授权仅用于此测试服。参数见 [command](../development/DEBUG_PIPE.md#联网命令)，服务启动见 [D2GS 计划](../architecture/MULTIPLAYER.md#本机部署与验证入口)。

| 代码 | 职责 |
| --- | --- |
| [contracts/online.hpp](../../src/contracts/online.hpp)、[online_world.hpp](../../src/contracts/online_world.hpp) | 只读会话与服务端单位／房间／位置／装备外观前缀；未知字段 optional |
| [app/frontend.cpp](../../src/app/frontend.cpp) | 会话所有权、配置、tick、页面路由与命令提交 |
| [RealmFrontend](../../src/presentation/frontend/realm_frontend.hpp)、[RemoteScene](../../src/presentation/remote/remote_scene.hpp) | 原图、字体、局前／世界显示与输入，只读副本并返回意图 |
| [remote_world.cpp](../../src/client/remote_world.cpp) | 有序回包归并，无 MPQ、GPU、存档或本地模拟 |
| [RemoteTown](../../src/client/remote_town.hpp) | 五幕有序房间事件重放、锚点校验及活动地形快照，复用 DS1／DT1，不生成本地单位 |
| [RemoteControl](../../src/client/remote_control.hpp) | 当前MPQ／活动地图目标资格、移动范围、NPC靠近后交谈及回城门技能选择；UI与command共用，不模拟玩家位置或NPC规则 |
| [online_items.hpp](../../src/contracts/online_items.hpp)、[RemoteInventory](../../src/client/remote_inventory.hpp) | 原服物品与请求值契约；客户端依当前MPQ解码品质／属性与布局，并校验物品command；不调用离线库存或保存 |
| [RemoteCombat](../../src/client/remote_combat.hpp) | MPQ主动技能／学习／目标资格及原状态位流只读投影，提交原服战斗／成长请求；不运行离线技能、伤害、AI或保存 |
| [native_act_layout.hpp](../../src/world/outdoor/native_act_layout.hpp)、[native_map.hpp](../../src/world/native_map.hpp) | 共用五幕布局及房间生成会话；GS 有序重放和活动地形快照已接源码，完整规则及跨区未验收 |
| [online_commands.cpp](../../src/app/debug/online_commands.cpp) | 同一会话的菜单管道；accepted 与异步成功分开 |
| [RealmPortraitCatalog](../../src/content/character/realm_portrait.hpp) | MPQ 原表动态重建外观编号 |
| [RealmSession](../../src/network/realm_session.hpp) | SID／MCP／D2GS 状态机、认证、角色／房间、取票、加载、心跳与退局 |
| [TcpStream](../../src/network/tcp_stream.hpp) | Asio DNS／TCP、期限、有限队列、拥有线程 poll；关闭后旧回调不交付 |
| [protocol/](../../src/network/protocol/wire.hpp) | 边界检查、framing、认证、D2GS 压缩与拆包 |
| [Networking.cmake](../../cmake/Networking.cmake) | 固定 Asio 1.30.2／BNCSutil 快照，独立网络／协议／远端客户端目标；[来源／许可](../resources/THIRD_PARTY.md) |

网络公开头仅有项目值类型和标准库；Asio／WinSock／BNCSutil 留在实现，网络不依赖玩法、世界、存档、MPQ 或 GPU，保留 C++20／CMake Windows／Linux 路径。

## 测试暂停与快捷入局（本批有限原服验证）

正常启动始终运行。ESC、菜单、背包、对话与失焦只处理输入，不暂停原服、网络、场景时钟或音频。显式开启调试管道后可用 `pause/resume`：保留画面并停止界面输入与表现推进，回执为 `presentationPaused`、`networkRunning=true`、`serverPaused=false`。恢复时重建表现、清旧手势和临时弹体，再显示当前副本；单位动作按客户端收包年龄推进，避免场景重建从旧动作首帧重播，该时间不是原服真实动作起时；退局或代次变化也结束测试暂停。原服仍会造成伤害、死亡及其他玩家动作，暂停截图不能作为原服停止时间的证据，不提供原服 `step`。

先用 UI 记忆一次登录，然后执行：

```powershell
.\Play.cmd -OnlinePlay <服务器已有角色>
.\Play.cmd -OnlineCharacter <服务器已有角色> -OnlineCreateGame <新房间>
.\Play.cmd -OnlineCharacter <服务器已有角色> -OnlineJoinGame <已有房间>
```

`-OnlinePlay`／EXE `--online-play` 只需指定角色，自动生成合法房间名并建普通房间；与显式角色／建房／加入参数互斥。EXE 对应 `--online-character`、`--online-create-game`、`--online-join-game`，可合用 `--online-config`。只指定角色则停在大厅。快捷流程正常执行 SID／MCP／D2GS 认证和取票，不创建本地角色或伪造票据；建房使用普通难度、8 人、99 级差和空密码，其他设置或有密码房间继续用 UI／command。缺记忆凭据、角色不在服务器列表、拒绝、超时或人工操作时停止自动步骤，不重复建房、注册或重连。记忆按配置路径隔离，包目录的配置与源码目录为不同凭据目标。

走跑、底栏展开和自动地图偏好继续使用 `client-settings.json`。通用原子文件替换已移至 `resources/atomic_file.*`，客户端偏好不再引入 D2S 编码；原 D2S 格式、规则指纹、备份和替换算法未改。

2026-10-06 本批 Windows Release 已构建并更新运行包，使用既有调试管道／UI输入及本机参考服、独立测试账号和普通非Ladder女巫观察，未新增测试脚本／用例／程序。实际范围：

- 入局：注册、建角、选角、建房与原服地图／本人显示；旧单机参数明确拒绝。本轮另通过局前帧输入完成UI登录记忆，新进程 `--online-play FoundationSor` 自动认证／选角／随机房间入局；不认证注册、删除等全部局前页面。
- 暂停／菜单：测试 pause 前后截图 SHA256 相同，GS 接收字节持续增加，暂停期间 UI 输入拒绝；resume 恢复。ESC 显示原退出菜单时 GS 继续收包。
- 库存：既有 ui-input 驱动背包拖放和背包→箱子→背包→箱子连续 Shift 转移，确认原服位置、空 Cursor 与空队列；修正发送后仍记录上一请求序号导致后续 Place 丢弃。旧区域／交互代次被拒绝，inventory.reason 输出适配器拒绝原因。
- 传送点：waypointRequested 只表示原0x13请求，匹配原0x63且本人／对象有效才成为 waypointSource；关闭／超时清请求，无匹配请求的迟到回复不重新开菜单并计 lateWaypointReplies；原协议没有请求序号，不能区分同一对象的新旧回复。实际观察待确认 command 关闭、靠近后确认菜单及 ESC 关闭；迟到回复分支没有实际样本。原0x15本人校正清导航请求；分段导航改为读取发送后的 movement revision，避免发布前的旧快照取消路径。这两处未专门运行认证。
- 保存／异常：原菜单 Save and Exit 回角色页，正常退局有 D2DBS 成功记录；新进程背包／箱子保存重入通过。参考服曾只回复握手而不下发地图，客户端先加载超时请求保存退局，再因无确认返回角色页并记录保存未知；界面超时提示保留具体错误，不再被通用原文遮盖，提示修正未单独复验；重启参考服后恢复入局。参考服长期稳定性仍有缺口。

本轮自己游玩入口使用同一独立1级女巫、本机参考服及实际包内EXE，证据在 `artifacts/online-solo-20261006/`：鼠标按住跑动与走跑切换、近处Kashya点击开菜单、箱子靠近／开启与Shift转出辨识卷轴；原碰撞分段出营地到Blood Moor、右键火弹画面、原服伤害／死亡掉落（击杀补用GUID command）、鼠标拾取护身符、腰带右键药剂恢复生命；角色自然死亡后ESC回城、原菜单按下／同坐标松开 Save and Exit 返回角色页。新进程保留卷轴、护身符和药剂消耗，鼠标取回法杖后再次保存重入确认装备；D2DBS保存成功。普通对象按当前MPQ Selectable0–7及OperateFn投影，名称读原TBL，物件靠近复用原尺寸／碰撞交互几何并选择可到达位置；神坛交互尝试被死亡打断，不能据此认证其效果，其他箱子／井／门状态亦未逐项验证。

后续自己游玩批次继续用包内EXE与同一独立角色，证据在 `artifacts/online-solo-followup-20261006/`：Windows Release构建／打包；UI卷轴鉴定与背包右键装备真实护身符，原服消耗卷轴，保存后新进程保留已鉴定项链装备；command绑定Fire Bolt并重入收到0x7B，F1选择获原服0x23确认，UI选择器绑定F2普通攻击后重入也恢复。营地靠近Akara、原对白结束后0x5D确认邪恶洞穴日志状态1，重入角色私有任务字的STARTED位保持；补齐每局原0x40请求后，新进程收到0x52状态1，任务面板显示原MPQ邪恶洞穴说明。最终包F2恢复普通攻击选择、原菜单保存退局通过。参考D2GS两次后续房间握手未继续，客户端超时返回选角，确认只有本轮角色且已离开后仅重启D2GS恢复；另一次连续新进程重入成功，不认证长期稳定性。神坛名称／TARGETABLE源码已入包，本批未认证野外神坛实际效果。

上述为有限调试帧输入观察，不认证人工鼠标全部操作、五幕移动／多人、Linux 或完整 M1–M3。原存档格式和规则指纹未改。

## 协议覆盖目录（1.13c 当前源码）

下表区分能分帧、字段已消费和语义仍未知；没有以收发成功计为玩法成功。动态字段定义与取证入口见相应源码及[资料来源](../resources/THIRD_PARTY.md)。`online-status.protocol` 按 SID／MCP／game 分别返回包 ID、received／sent／unconsumed、协议逻辑字节数和 lastReceived，不保存或输出原始认证载荷。错误回执的 packetId 保留 dispatch 来源；未知流长度仍立即失败。

| 方向／包 | 长度／分帧 | 当前消费 | 字段／关键性与缺口 |
| --- | --- | --- | --- |
| SID 双向 | FF／ID／16-bit 总长；独立 256 KiB 累积 | 0x50／51 版本认证、3D 注册、3A 登录、40 Realm、3E 取票、0A 选角聊天环境、25 ping | PvPGN 头与 handle_bnet；密码／key／票据不入视图；4C 认证模块拒绝；频道、聊天和账号服务未完整消费 |
| MCP 双向 | 16-bit 总长／ID；独立 256 KiB 累积 | 01 启动、19 角色、07 选角、02 建角、0A 删角、03 建房、04 加入、05 列表、14 队列 | PvPGN d2cs_protocol／handle_d2cs；真实请求 ID 与拒绝码；Ladder、完整分页／过期服务未完成 |
| D2GS S→C 0xAF／01–06／8F／B0 | 原 1.13c 长度表；AF 动态压缩模式 0／1 | 握手、难度／资料片、幕／种子、加载、心跳及退局 | D2Net.dll 0xA900＋RealmSession；校验幕0–4、难度0–2和标志范围；Ladder 仍明确拒绝，secondarySeed 含义保留未知 |
| D2GS S→C 07／08／09 | 固定6／6／11字节 | 有序房间进入／离开、对象指派 | room／tile／level、对象GUID与真实身份；地图历史4096条，丢连续性拒绝重建 |
| D2GS S→C 0A–11／15／16／18／95／96 | 原表固定；16为word长度；18／95／96资源位流 | 单位增删、玩家／怪物动作、坐标／不连续移动、资源与本人身份 | 可见单位8192上限；身份与动态字段保留 optional；完整特殊移动及其他玩家私有字段未覆盖 |
| D2GS S→C 19–1F／21–23／94 | 固定与原技能列表 count 长度 | 本人绝对属性、技能选择／基础／加成、技能列表 | 状态更新／成长确认；不是通用事务ACK，完整派生显示未完成 |
| D2GS S→C 27–2A／77 | 固定40／103／97／15／2字节 | 原NPC对白、任务位载荷、商店结果和容器打开 | 任务位仅按现有对白消费，完整五幕任务副本未完成；商店精确报价待接 |
| D2GS S→C 3E／42／97／9C／9D | 3E／9C／9D byte长度；42固定6，97固定1 | 原物品位流／基础属性增量、光标清理、武器组及异步库存 | 参数宽度／物品布局读MPQ；完整染色／名称／全部属性消费者未完成 |
| D2GS S→C 4C／4D／99／9A／73 | 固定16／17／16／17／32字节 | 技能动作、目标、原飞弹通知和有界事件 | 本地 monotonic 收到时间仅作诊断，不是线上字段；全部技能客户端程序未完成 |
| D2GS S→C 51／59／60／63／82／8E | 固定14／26／7／21／29／10字节 | 对象／玩家指派、移出视野、旅行／传送点、门户、尸体归属 | 0x0A仅撤销空间指派、0x5C才移除名册；完整多人门户与尸体权限待接 |
| D2GS S→C 67–6D | 固定16／21／12／12／16／16／10字节 | 原怪物路径、动作、生命比例 | 速度／尺寸读MPQ；动作12／13等保留未知，不猜测动画模式 |
| D2GS S→C A3／A7–AA／AC | 原表；A8／AA／AC byte长度 | 状态快照／开关、位流、NPC身份与外观前缀 | 位宽由MPQ消费者解析，状态历史64条；不代表全部状态效果可画 |
| D2GS S→C 26／5B／5C／75／7F／8B–8D／90 | 26双NUL字符串，5B word长度；其余原1.13c固定表 | remote_social归并聊天原字节、名册、队伍／关系原值和公开位置 | 无发送、界面、关系资格解释或多人实机认证；7F非玩家分支仍计unconsumed；不创建视野外单位 |
| D2GS S→C 5A／5E–5F／7D–7E／81／A4–A5 等 | 原表固定 | **能分帧但未消费完整语义**，unconsumed 按 ID 暴露 | 系统事件、任务／宠物等关键后续状态；M4／M7／M8／M9仍未完成，不能归为无关消息或宣布M1全覆盖 |
| D2GS S→C 其他有长度包 | 原始固定表或已核实变长程序 | 未消费ID公开计数 | 不把能跳过当成功；未知长度／未核实变长／Warden明确失败 |
| D2GS C→S 行走／交互／旅行 | 原01–04／13／49等请求编码 | 当前地图资格＋完整坐标终点／原GUID靠近、传送点／退出 | 位置信息始终以服确认；取消只结束客户端未发意图 |
| D2GS C→S 库存／城镇／战斗／成长 | 原物品27类及技能／属性请求 | 远端领域协调器资格、限流、关联更新、拒绝／超时 | 无通用ACK／事务号；非幂等操作不自动重发；统一全部领域协调尚未完成 |

## 多人只读副本（当前源码，尚未运行）

`contracts/online_social.hpp` 与 `client/remote_social.*` 单独维护名册、聊天和关系，原包字段以当前1.13c长度表及本地D2MOO的D2PacketDef／SCmd交叉核对。0x5B确认名册身份，0x75／8B／8C／8D维护原队伍／关系值，0x7F玩家分支维护公开区域／生命比例，0x90维护公开32位坐标；先到更新保留未知身份。原生命比例、队伍ID及flags保持原值，尚未解释为UI资格；0x75的关系flags与0x5B的partyFlags分开。0x7F非玩家分支、系统事件及发送仍未实现。

空间单位与名册分离：0x0A不退出名册，0x5C移除成员及其关系；离开视野不会生成幽灵角色，也不清除未收到原删除的尸体归属。本人换幕保留本局名册／关系／聊天，退局／断线／新局清空。名册暂存上限64（缓冲保护，不是允许64人游戏），关系4096，聊天256条。聊天原语言字节只以nameBytes／textBytes输出，避免将原编码误当UTF-8；目前没有用户聊天输入、显示、组队按钮、敌对资格或发送请求，因此不算M4完成。

`online-social`／`online-chat`是完整只读快照别名，均返回world.social；操作仍以服务端回包为准。以上包尚未逐包运行验证，JS参考中的错误长度／位宽不采用（例如0x7F区域按原包16位），不将参考表纳入源码。

## 会话与协议边界

公共方法在同一个客户端线程调用，底层持锁串行提交。私有 worker 每 10 毫秒服务传输、心跳、超时及有序副本更新；`tick()` 在客户端线程发布快照，也可立即服务一次。UI 停止绘制不停止网络。Idle／Failed／Cancelled 登录或注册；RealmSelection 选 Realm；CharacterSelection 创建／删除／选择角色或切换 Realm；Lobby 列表／建局／加入。列表取消保留 MCP，迟到回复不污染后续请求；游戏请求取消退役旧 MCP／GS 并重新取角色列表，已登录游戏则先原生退局；其他账号／角色等待取消结束会话。不自动重发角色消费操作。UI／command 不把 accepted 当作服务器成功；失败阶段的迟到输入不覆盖原连接错误。

read() 借用客户端快照，有效至下一次 tick 或公共修改／随后 read；后台回包不改写该快照。connectionGeneration 管登录生命周期，gameGeneration 管游戏连接，消费者按当前代次丢弃旧包。RealmSession 已将支持的回包归并至 read().world；仅 `RealmSession(true)` 显式保留有界原包供额外消费者；默认 UI 使用已归并快照，不积累无消费者的原包。启用者仍必须持续排空。world.areaGeneration 在 LOADACT／UNLOADACT 时递增，仅清理区域实体／房间，保留本人身份、装备和属性；1.13c 首次入局在 LOADACT 前已发送这些角色数据，不能一并清空。跨幕撤销旧位置，首个 LOADACT 可保留已收到的出生位置。断线／取消／退局／新局清空全部副本，显示绑定按代次失效。所有原服动作提交复验已发布快照与后台当前的连接／游戏／区域代次及本人GUID；加载期间换区的旧输入被拒绝，调试输入队列也清除。退局和新局统一清除门户请求、NPC初始化、限流／等待计时器与心跳；新局首个pong前延迟为null，不能显示为已测零延迟。NPC明确拒绝不被后续相关更新标记覆盖。错误具有独立本地sequence，界面关闭错误后不因世界／心跳revision变化重新弹出同一错误。退局、返回选角和 Realm 切换重新取票；保存成功须由重入结果或服务日志确认。

正常关闭窗口或 command `quit` 时，已进入游戏的会话先提交 0x69，持续 tick 至退局响应／关闭或阶段期限，再注销并结束进程；不把 quit 的 accepted 当作保存回执。online-cancel／online-logout 仍属于显式关闭连接。客户端不写 Realm D2S，服务器负责保存。

- profile 为本机 PvPGN／D2CS／D2GS＋LoD 1.13c，旧式 SID 登录与 IX86ver0..7.mpq。CheckRevision 公式先检查再交解析器；原文件路径暂要求 ASCII。NLS／Warden／扩展反作弊未实现，本机 Warden 禁用，严格版本校验未验证。
- SID／MCP 独立累积半包／粘包；D2GS 压缩模式 0／1、长度头与 Huffman 解压，未知长度明确失败。长度表核对本机 D2Net.dll 0xA900、D2MOO／OpenD2；0x7A 为 13 字节，不沿 JS 示例的 3 字节。
- 加载 0x02 后发 0x6B；0x03 保留幕／种子／townArea／secondarySeed，0x04 完成、0x0B 本人单位绑定。townArea 不代表玩家当前区域，secondarySeed 尚未驱动 DRLG。
- 累积各限 256 KiB，单次解压 64 KiB，可选原包队列 4096 包／2 MiB；默认阶段期限 15 秒、心跳 5 秒。建局 MCP 0x14 记录队列位置并刷新期限；取消关闭会话，不宣称撤销已完成的服务端操作。
- 参考服空列表不发终止包时，超时返回 Lobby、清除未完成列表并保留错误，gameListComplete=false，不能把无响应当作已确认空列表。列表按角色 Hardcore 请求，Ladder 房间不展示。
- 33 字节预览接受 version 4／10／13；native 1.13c 的 14-bit client flags 给出职业／等级／状态／进度，byte 30 是公会徽章颜色。外观保留类别核对 D2Common.dll 0x9D888，具体 item code 动态读 MPQ。
- 密码、key、哈希和票据不进入视图／日志；各层清理自身副本，调用方负责输入副本。离线存档语义未改。

证据为本地 PvPGN common/bnet_protocol.h、common/d2cs_protocol.h、bnetd/handle_bnet.cpp、d2cs/handle_d2cs.cpp／handle_bnetd.cpp；布局及动画参考本地 reference 和既有离线选择器，图形文案读取当前 MPQ；参考仓库不提交。

## 原服物品与请求

物品底层此前已通过Windows Release及本机D2GS有限冒烟，本批已随共用UI打包；本轮补验原服掉落拾取及药水使用，其余组合仍沿用下方有限历史证据。`online-items`／`online-ground`读取同一快照，`online-item-action`提交27种原请求；参数见[command](../development/DEBUG_PIPE.md#联网物品操作)。

- 0x9C／0x9D保留真实GUID、所有者、服务端位置／模式及原位流。客户端序列无JM头、物品种子或Realm尾；地面／掉落模式用16-bit全局坐标，其余用原body／格子／page。0x9C本人库存允许先于0x0B到达，绑定后补本人所有者；镶嵌子物品保留type4宿主GUID。
- 解码compact、耳朵、金币、任务难度、quality 1–9、未鉴定字段省略、图形变体、词缀编号、个性化名字、数量、耐久、防御、孔数及主／套装／符文之语属性列表。尺寸、部位、属性宽度／Save Add／参数、书本配对均读当前MPQ。name当前为基础名，尚无完整特殊名称／属性提示；属性列表值为序列化单位，尚未应用ValShift，0x3E基础属性更新另列baseStats并保留服务器单位。
- 0x3E按1.13c实际变长包读取变宽GUID／数值、基础属性标志和参数，数量／耐久更新与请求关联均使用实际长度。0x0A删除物品；0x42按所属玩家清理Cursor。9C／9D移出容器／腰带按实际模式处理，合并在同包的Cursor转移保留，旧容器记录删除；IFLAG_DELETED本身不直接删除。0x97确认武器组。换幕保留本人库存及孔内子项，新局／断线清空。
- command支持拾取至背包或Cursor、拿起、放入／交换背包格、Cursor丢弃、装备／卸装及两手武器交换、腰带放入／拿起／交换／使用、堆叠、装书、卷轴／书鉴定、镶嵌及切换武器组。原拾取由服务端寻路接近，沿原50单位距离限制；不自动发放物品或改变客户端位置。需求／职业、同类堆叠细则、效果、扣减、最终装备和保存由原服决定。
- 操作前复验当前代次、GUID／revision、本人所有权、Cursor及MPQ格子／腰带／部位；未知或截断数据拒绝。只允许一个Pending物品请求，100ms限制、不自动重试；storage-close可取消等待。accepted仅入队；Updated需复查位置／数量，TimedOut结果未知，死亡／换幕／退局标Interrupted。NPC交易等待0x2A明确回执，原错误码标Rejected；关闭存储无原生确认，标SentNoAck。
- page快照为原InvPage+1：1背包、4方块、5私人箱；place使用native page=0／3／4。箱子由真实Objects.OperateFn=32单位交互，方块由原物品使用请求打开；仅0x77确认后开放对应格子。布局读当前Inventory.txt：资料片箱子6×8、方块3×4。关闭／死亡／换幕清理上下文，禁止把已打开的方块放进自身。
- 0x4F提交存取金币、关闭箱子／方块和合成，金额高WORD在前；0x50丢金币。0x19为金币正增量，0x1D／1E／1F为绝对属性，0x2A保留交易金额但不重复写钱包，避免出售双计。合成配方、产物和金币上限完全由原服决定，不接本地方块事务。
- 当前NPC交谈提交0x38 action=1生成普通货架；9C action=11／12维护服务器商店物品。单件购买0x32、出售0x33、单件／全部维修0x35、凯恩批量鉴定0x34均由原服定价／判资格，0x2A保存结果／物品GUID／金币。交易前核对当前NPC身份和货架来源；维修限原五个铁匠，鉴定限原五幕凯恩。赌博、多买、报价UI、玩家交易、佣兵装备及尸体库存暂缓。
- 回城卷轴／书创建门户仍用online-town-portal；通用use效果与目标选择由回包决定。库存／商店UI及原属性提示已接共用面板；地面原图／品质色标签及拾取命令也已接共用实现；孔内完整显示及未识别的特殊名称仍有缺口。

本批未改离线InventoryService、物品生成、D2S、地图或规则指纹。参考本地D2MOO Items／SCmd／ItemMode／PlrMsg／PlrTrade／SUnitNpc，当前MPQ优先。

### 城镇物品批次冒烟（2026-10-06）

使用既有游戏、command及本机D2GS，未编写测试脚本／用例／专用程序。仅新建bomb2账号下NetItemSor，既有角色未改。先验证服务器初始装备；方块／宝石／金币由既有单机命令产生合法临时D2S，角色退出并解锁后仅替换该测试角色，联网运行仍只消费服务器状态。这验证协议／存储路径，不认证自然掉落或任务取得物品。

| 路径 | 实际观察 |
| --- | --- |
| 本人库存与品质 | 原初始7项及扩展普通／魔法／稀有物品均解码；背包拿起／放回、帽子装备／卸装、箱子存入卷轴通过 |
| 腰带与地面 | 腰带→Cursor→丢弃→拾到Cursor→腰带→使用，服务器移除药水；旧模式删除与新Cursor模式兼容 |
| 方块 | 0x77确认后放入3颗gcr，原服输出1颗gfr；修正后的副本无残留材料，新进程重入保留产物 |
| 金币 | 存3000／取500确认钱包5501、箱子2500；丢200／拾回恢复8001，验证0x19增量 |
| 商店 | 靠近Charsi／Akara并收到原对白，真实货架全部解码；帽子购买47／出售25，修正后钱包5410／5435与回执完全相同，售出物品不留本人库存 |
| 书本与回执 | Akara购买回城卷轴自动装书，数量6→7、0x3E baseStats同步且保持ProtocolReady；全部维修收到result=2，无损法杖单件维修收到result=9／Rejected |
| 持久保存 | 原服CHARSAVE／CHARINFO成功；新进程、重新建局后帽子装备、方块gfr、回城书数量7、箱子卷轴及金额5457／2500保持。最后一轮额外买卖后正常退局，金额5435 |

证据在忽略目录`artifacts/online-items-20261006/`。冒烟发现并修复变长3E的两处固定读取、容器移出残留及交易金币双计。原D2GS曾在后续入局不返回握手，重启后恢复，未混作客户端物品验收通过。NPC靠近遇营地障碍时可能超时，可按已验证碰撞手动绕行后重试。

未实际覆盖损坏／损坏归零装备维修、凯恩任务资格／鉴定成功、全部配方、堆叠／镶嵌／42消费组合、未知品质全组合、多人争抢／交易、断线中途事务及Linux；这些不能据本次冒烟宣称通过。包仍为此前地图交互版本。

## 远端营地入局边界

该标题保留供既有链接使用；五幕1–136现已接入共同生成入口。

- 联网仅创建只读服务器单位／房间副本及地形会话，不创建本地 Region、人口、AI、任务或角色存档。属性、装备和持久保存仍由原服决定。
- 五幕种子、难度和当前MPQ驱动共用 `NativeMapGenerator`；0x07／0x08及玩家换房顺序驱动活动网格，不按最终房间集补猜。历史最多4096条，不连续或生成失败需重新入局。五幕使用同一严格路径，旧唯一预设匹配与碰撞变体回退已删除。
- 地形快照保留当前连续组件的活动房间、选定DT1、完整碰撞及原出口链；允许营地和野外连续跨区。场景位置为服务端全局subtile，快照原点可随活动房间变化，不是固定城镇偏移。
- UI／`online-move`复用当前地图与代次，发原0x01 walk／0x03 run指针终点，按单位靠近发原0x02／0x04真实GUID，至少间隔100ms。不把原版18格AStar尝试范围误当每条命令长度，不按迟到坐标切段或等待下一段回包。command按原服采样检查各轴50格；UI按同帧显示坐标检查投影范围，实际50格资格、路径和可达点仍由原服决定。阻挡目标可以提交，未加载地形拒绝；accepted仅表示发送成功，实际到达由后续回包确认。
- `online-use-exit`及`online-interact`要求原生地图可用、角色存活及当前场景真实type5出口或type2地图对象ID，发送原0x13交互；Objects.OperateFn分类门、门户、传送台、传送点和对象楼梯，接触／资格及最终区域由原服校验，不能传LvlWarp类型编号冒充单位ID。
- 服务端对象模式按当前MPQ的SizeX/Y、HasCollision、BlocksLight、IsDoor及碰撞标志刷新对象阻挡层；原地形碰撞保持。非循环物件动画从已确认模式变化起播放，不提前开门或清墙。
- 原0x63读取对象GUID和8个WORD历史、校验0x102头，Levels.Waypoint映射目的地，未解锁点拒绝。原0x49提交旅行或level=0关闭；LOADACT／房间及玩家回包确认地图／落点。原DC6／TBL／PL2面板与单机共用显示函数，不依赖本地任务或存档。残留移动回包不误关菜单；新移动／交互先发关闭请求，避免服务端保留忙碌状态。
- 自动地图复用AutomapCatalog和当前MPQ图块，记实际可见DT1实例；关闭地图仍积累。按局代次／种子／幕／区域／全局格记忆，换区／换幕返回保留，新局清空；连续步行邻区同层、楼梯／门户隔层。联网不写离线.d2xmap或服务器D2S。Tab开关、V切小图左右、方向键平移、Home居中；尺寸另有online-automap命令，完整选项／名称／队伍显示未接入。
- RemoteScene只读权威位置／模式／装备，显示原图；缺图单位明确计数，基本战斗输入和共用地面物品／背包UI已接；完整技能视觉仍待接入。物品位流由RemoteInventory独立消费。地形已选变体、Pops与亮面Warp显示不参与本地伤害或碰撞模拟。

包长度核对本机1.13c D2Net，协议语义交叉参考本地D2MOO；完整客户端动作、颜色、自动地图及逐帧视觉仍有边界。原800ctrlpnl7.dc6有七帧，场景与既有HUD仅绘制前六块。

## 行走、回城门与NPC

本节已随城镇物品批次Release构建，Charsi／Akara靠近／交谈／关闭有限实测；回城卷轴创建原门户并返回营地已在本轮实际包通过；完整旅行资格仍未实测。下方旧批次不认证新增流程，入口见[command](../development/DEBUG_PIPE.md#联网命令)。

- 行走：共用`RemoteControl`校验坐标／单位目标、活动碰撞和50-subtile请求范围。按下地面捕获世界终点，按住期间只有指针移动才更新目标，相机移动不产生新目标。远处先按共同Grid路径提交短段，原服确认接近当前短段终点后推进；不再每秒因缺回包而重发，15秒未到达结束。接近NPC后改发0x02／0x04，服务端确认原距离≤6后才0x13交谈。施放／拾取、共用UI停止、Esc、死亡／退局／换幕、目标失效或请求被替换结束旧意图。取消不撤销已发给原服的当前短段。
- 鼠标：共用FrameInput由SceneController接收，RemoteScene仅返回WorldInputView命中／投影，不再持有手势；与单机共用原图spriteHit、原字体姓名标签和高亮着色。先解析真实mapTargets再生成交互意图，绘制后统一使用选中目标，避免把NPC点击变成地面移动。普通迟到位置样本保留当前碰撞路径上的显示进度；明确停止／0x15校正仍服从原服，失效预测不再回退到旧行走动作重播。参考Diablerie的MouseSelection／PlayerController和D2MOO PlrMsg位置更新条件；最新修正仅静态检查、构建／打包，未运行认证。

寻路差异核对本地D2MOO：`Path.cpp::PATH_AllocDynamicPath`给玩家设STRAIGHT；`PathMisc.cpp::PATH_Straight_Compute`先用TOWARD局部路径，仅在目标距离平方≤18²时尝试A*兜底。18格是寻路策略范围，不是坐标命令长度；按迟到位置切18格命令会导致长点击中途等待、拖动时发送落在身后的短目标。RemoteControl已改为直接发送完整终点／真实GUID。本人显示使用共同Grid的原整数射线裁剪、三方向优先级Toward及73步上限，近距A*兜底仍用已有Grid求解器，节点顺序／原限额未完整等价。射线成功时保留请求终点，仅失败时裁剪；原射线最后扫描的相邻格不能当作成功终点。推进沿Step.cpp语义检查实际经过的格子，不能每帧对小数位移重新运行路径规划射线，否则会在清晰长路径的首次跨格时误停。普通对象靠近仍选共有原版交互几何的可到达位置再0x13；拾取0x16与技能自动追击由原服处理。本批Windows Release构建并入包，最新跨格修正仅确认包内入局，行走／持续拖动验收按用户要求留给用户，未计通过；先前长点击暴露显示停顿的样本保留于忽略的`artifacts/online-act1-monsters-20261006`。
- 显示：保留服务端位置和原动作字节；原玩家走／跑字节与PLRMODE分开处理。玩家已入队移动请求与服务端0x0F／10、NPC的0x67／68目标驱动独立显示路径，沿当前活动碰撞连续走／跑；速度读取CharStats.WalkVelocity／RunVelocity、MonStats.Velocity及怪物回包的完整速度百分比。显示与镜头共用同帧位置，不修改权威世界副本；位置回包短时平滑校正，停止／取消结束旧路径，0x15传送／校正立即落到原服位置，未获响应的请求超时回归权威位置。0x67／68保留路径类型／步数／距离，末字节仅击退分支是生命比例，行走不再覆盖生命。圆周／击退／跳跃特殊路径不按普通追踪代替；完整原客户端路径算法、装备／技能移速与全部动作／效果仍未认证。此移动显示修正已入包；本轮行走／跑动、NPC行走与传送有限观察通过，范围见上方动作／效果冒烟。
- 门户：既有type2／5的0x13交互继续由原服决定靠近、资格和落点。`online-town-portal`从当前Skills查卷轴／书技能ID，以服务端0x22数量或已报告技能判断可用，发送0x3C选右技能及0x0C施放；收到0x82分配新的本人门户或超时后恢复先前右技能。不存在本地生成门户、扣卷轴、改存档或自定落点；需要原服背包中有实际卷轴／书，完整背包UI尚未接入。
- NPC：MonStats.interact、MonStats2尺寸及NameStr绑定真实中立单位。0x13激活，匹配0x27后仅一次0x2F初始化；逐条原TBL对白只读显示，结束对白发0x31，退出交谈发0x30。0x28读取角色私有任务字，0x29独立保存本局公共任务字，旅行资格不再取公共记录；任务日志只读原服，未自行推进或发奖励；NPC治疗由原服初始化流程执行。
- 旅行：瓦瑞夫／马席夫原菜单文字来自当前TBL，下一幕资格读原服任务奖励位，返回上一幕沿原身份；提交0x38 action=0，以回包确认。普通买卖／维修／凯恩鉴定command见上节；泰瑞尔专用旅行、全部受限任务门户、赌博／雇佣／复活及任务物品服务暂缓。

NPC话题仅使用当前0x27提供的消息，菜单标签暂取原对白首行；不把离线初见／闲聊／任务服务接入联网。完整原话题标题、声音、任务日志UI及NPC提示图标未验收。交谈关闭、新移动／交互、死亡／换幕会清理临时会话；command的acknowledged仅表示0x31已入队，不表示任务完成。地图核心、原MPQ和D2S语义本批未改。

## 地图交互与探索冒烟

本批Windows Release实际包通过既有command连本机原服；独立临时角色`bomb2/MapNavSor`，证据在`artifacts/online-map-navigation-20261006/`。仅该角色的原D2S用于夹具：普通难度39个传送点位／出生幕及校验和，不改aaa／bbb／ActMapSor，不把夹具解锁当作任务流程通过。传送台复验另用既有单机命令生成99级／分配体力／原补给神殿恢复的独立D2S，匹配本轮临时角色名称并重算校验和，未解锁任务。检查后正常退局并通过MCP删除MapNavSor，原角色保留。

- 0x63实际历史头258、初始只有第零点，39条目的地按原表读取。未解锁目的地、关闭后的旅行、未分配对象和旧局代次请求均拒绝，原传送点面板截图已查看。
- 最终包以种子1685319291通过真实0x49连续完成1→40→75→103→109→1→74，全部nativeMapReady／playerDisplayed成立，返回保留历史和探索。此前种子1773422220另覆盖48下水道二层、113水晶通道、3冰冷之原及107火焰之河；后者使用生成器实际调整后的原点。
- 地下墓穴二层门199实际模式0→2；奥术圣殿74的原门户激活后，经0x13往返皇宫地下三层54，地图、区域与落点回包正常。
- 奥术圣殿传送台实际往返：种子1538180684，服务端台23交互后25501,5448→25516,5447，另一台返回25503,5447，区域74和原生重建保持正常。首轮低等级角色在接近传送台途中死亡，客户端禁止后续移动；该轮未计为传送台通过。离线独立99级D2S同进程保存／新进程加载正常，未用它认证正式联网成长或战斗。
- 鲁高因完整原城镇图跳过红叉诊断帧，单机／联机共用修正。表交叉核对本地libd2与原1.13c D2Client文件偏移0xD2DB8，实际大小图／面板截图保留。
- 参考服第三次建局曾停在LoadingGame，地图包之前没有角色取档；另一次watchdog明确关闭游戏并退出。失败快照／服务日志保留，重启仅本任务D2GS后最终往返通过。长时间稳定性仍未解决，服务器终止或角色死亡中断不计为通过。

地形生成核心本批未修改，原122／364组对照证据继续适用，未冒充重新穷举。完整彩光、原客户端逐像素自动地图锚点／色表、全部任务门户资格、所有种子／激活时序及Linux仍未认证。

## 第一幕地图与出口冒烟

2026-10-06使用实际 `dist/current/d2x.exe`、既有command及本机参考服，独立非Ladder测试法师 `bomb2/ActMapSor`；未改动 aaa／bbb。证据集中在忽略目录 `artifacts/map-oracle-20261005/`。

- Windows Release构建／打包通过，包内EXE及网络DLL与构建产物SHA256相同；正式地图不依赖原D2Common DLL。
- 第一幕原版1.13c对照122组全部通过；共享离线路径五种子各136区域加载／出口关联通过，包内第一幕39区显示、噩梦／地狱地下墓穴代表启动及临时D2S同进程／新进程重载通过，详见[地图验证](../gameplay/world/MAPS.md#本批验证)。
- 连续参考服检查以真实移动完成营地1→血腥荒地2，真实type5单位交互进入邪恶洞穴8并返回2；最终种子665010272，原生地图、人物显示和活动碰撞保持，无mapErrors。补齐原0x09出口分配回包，0x13使用真实服务器GUID。出口附近仍按服务器寻路／碰撞接近，command不是传送。
- 正常退局返回CharacterSelection；原服00:25:15日志确认独立角色CHARSAVE／CHARINFO保存成功。此项不认证联网战斗、拾取或任务变化保存。
- 先前长时间停留时参考D2GS watchdog关闭游戏；最终重新连接后连续完成往返及正常退局。服务长期稳定性问题仍未解决，断开时需检查服务日志；不将被服务端终止的检查记作通过。

## 其余幕地图冒烟

2026-10-06使用本批实际 `dist/current/d2x.exe`、现有command及本机参考服。第二至第五幕364组原版开发对照、五种子各136区加载／出口关联、包内97区显示及第五幕独立D2S重载通过，详细范围见[地图验证](../gameplay/world/MAPS.md#本批验证)。证据集中于 `artifacts/maps-acts2-5-20261006/`。

- 仅本轮临时角色 `bomb2/MapActsSor` 调整原D2S出生幕字节及校验和，逐幕建普通、非Ladder游戏。第一幕营地回归、40／75／103／109实际原生地图及人物显示通过，全部 `nativeMapReady/layoutMatched/playerDisplayed=true`、无mapErrors；第二至第五幕均收到真实移动后的位置变化。
- 地图种子依次为2031870738、536379678、1078075686、235020106、851939196；截图及入局／移动快照保留。各次正常退局回到服务器选角，原服日志确认CHARINFO保存回复。本轮未以持久任务／物品变化认证CHARSAVE差异。
- 检查后通过MCP删除本轮临时角色，原aaa／bbb／ActMapSor保留。出生幕夹具不表示原版任务门槛或正式跨幕旅行已通过；本批未实际连服走完第二至第五幕野外／室内全部区域，地形等价由独立原版事件对照认证。
- 参考服第三次建局时两次在地图包之前握手超时；重启D2GS后第三／第五幕分别检查成功。保留失败日志，服务长期稳定性仍有问题，不将失败轮次记为通过。第三至第五幕更多NPC／怪物外观仍计入unavailableUnits，缺图不替换中立单位。

## 营地入局与存档冒烟

2026-10-05，使用实际 EXE、既有 command 与参考服，未新增测试脚本或专用测试程序。证据在忽略目录 `artifacts/online-world-smoke-20261005/`：

- Release 构建通过；修复真实回包顺序导致 LOADACT 清掉已加载人物／装备／状态的问题，以及面板七帧误判资源缺失的问题。临时包编号诊断已移除。
- `bomb/aaa` 加载一级法师，生命40／法力35／体力74、装备 sst；`bomb2/bbb` 加载一级圣骑士及 ssd／buc。原东向营地以17个固定物件及服务器房间唯一匹配，最终包另通过24个固定物件匹配原南向营地；碰撞检查、本人图形、双人显示和持续心跳通过。
- 跑／走请求后，两端收到同一玩家的新位置；目标附近实际落点由服务器决定，不能要求命令坐标必然原样达到。截图查看了原营地、两个人物和原控制面板。
- 正常退局返回服务器选角，同进程及新进程再次登录／加入后，生命／法力、原属性、装备和本人图形恢复；单位 ID／gameGeneration 更新，出生位置回到营地，不保存上局站位。正常 quit 的服务端日志确认离开与 CHARINFO 保存。
- 最终包经正式 MCP 新建临时非 Ladder 法师 `NetEntrySor`，正常入局／退局后 D2DBS 确认 CHARSAVE／CHARINFO 写入；保存文件为原 D2S v96，含 sst 和原角色属性，重入重新从数据库加载。仅清理本轮创建的临时角色，保留 aaa／bbb。
- 营地移动没有改变角色的持久字段。本轮没有用联网战斗／物品变化认证 CHARSAVE 差异保存；原服会对未变化角色去重，参考 PlrSave 的数据库保存比较，不能将没有新 CHARSAVE 写入等同于保存失败。
- 离线独立临时原生 D2S 的保存、同进程 load、新进程 --load 和移动通过：经验500／等级2、金币123、已分配体力1点、Fire Bolt 等级1均保持。最终包使用正式 Fire Bolt 击杀原 fallen1，生命由3降至0、经验500→518，保存再加载仍为518。未写入用户原单机存档；该项属于离线回归，不认证联网技能或物品流程。
- 参考服曾再次出现取票后握手超时及启动 watchdog 退出，重启参考服后入局恢复；同房间连续退局重进成功。长期稳定性、人工鼠标完整流程、其他地图、复杂外观及完整玩法仍待验收。

## 本机有限冒烟

此前 2026-10-05 使用实际 `dist/current/d2x.exe` 与既有 command 管道，没有新增测试脚本或测试程序：

- `bomb` 返回已保存的一级法师 `aaa`；`bomb2` 返回一级圣骑士 `bbb`。无 key SID_AUTH_CHECK 均成功，bnetd 日志确认真实计算结果匹配 `D2XP_113C`；错误密码返回 SID 0x3A／code 2，随后正确密码可重试。
- `aaa` 建普通难度非 Ladder 房间，`bbb` 从真实游戏列表取得该房间并加入。两边达到 ProtocolReady，mapSeed／secondarySeed 相同，本人单位 ID 分别为 1／2；持续心跳后没有协议或解压错误。
- 两边正常退局，重新取得 Realm 票据并返回 CharacterSelection；D2GS／D2DBS 日志确认两人的 CHARSAVE／CHARINFO 保存成功。`aaa` 再建一局成功；最终包再次用 `bbb` 完成建局与初始化。这里只核实服务器保存回执与读取原角色，没有验证移动、经验或装备变更的保存。
- 单机独立新建法师的直入入口、status 和正常退出冒烟通过，没有写入原角色存档；stderr 仅有已知 Trees.ds1 兼容诊断。
- 主菜单、登录、已保存角色页、大厅及协议交接页已截图查看；修正选角按钮换行与大厅控件调色板／原有文字重叠。人工鼠标／键盘完整操作、新闻和 Banner、完整颜色映射及逐像素一致性仍待验收。
- 当前参考 D2CS 在没有可列房间时不会发送列表结束包（`on_client_gamelistreq` 只在 count 非零时发终止记录）；客户端 15 秒超时返回 Lobby 并保留错误，不能把无响应当成已确认的空列表。非空列表已验证。
- 参考 D2GS 曾被 watchdog 退出，另一次出现建局／取票成功但游戏握手超时；重新启动 D2GS 后最终包入局成功。服务端长期稳定性未解决，不据此宣称联机已可长期运行。遇到该现象先检查 `artifacts/d2gs-local/d2gs/d2gs.log`；确认 D2GS 已激活后重新登录，不选择 Ladder。

上述验证状态快照、构建日志与截图在忽略目录 `artifacts/online-smoke-20261005/`；服务端日志在 `artifacts/d2gs-local/pvpgn/var/` 和 `artifacts/d2gs-local/d2gs/`。最终网络 DLL 保持独立可替换，MinGW 运行库静态链接，包不依赖开发环境中的 libwinpthread DLL；原版 EXE／DLL／MPQ 不复制进包。
