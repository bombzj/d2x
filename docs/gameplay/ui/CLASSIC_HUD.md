# 经典HUD与游戏界面

当前产品只保留SceneView／SceneController／ViewState一套界面，RemoteUiClients提供原服只读值并提交语义命令。世界、自动地图、原图缓存、动画与声音也使用公共入口；没有Local适配或联机专用HUD。

## 原资源与布局

800ctrlpnl7.dc6提供800像素底栏；原球体／耐力／经验、技能槽、腰带、人物／技能树、背包／箱子／方块、NPC／商店、任务／佣兵、传送点和ESC面板读取当前MPQ原图／字体／TBL。侧栏裁剪、相机和命中共用worldViewport，数据缺失不自绘替代资源。

生命／法力球悬停数值使用当前MPQ panelhealth／panelmana格式与font16原字形，保留原灰白像素，不乘parchment黄色，不缩到12；字距按原TBL字宽。字形保持原像素大小（hudTextScale=1），不再乘W/800的hudScale；球体布局、命中及文字锚点仍随HUD适配，外层视口统一缩放最终画布。原800坐标下生命文字中心x=65、法力中心x=宽度−80，基线y=高度−95；DC6底部原点转换到字形上沿，不以球体矩形中心或固定20像素偏移定位。原1.13c D2Client RVA 0x276B5／0x27767读取两条TBL，0x276EE–0x27703／0x277AC–0x277C2确认颜色0与锚点；font16资源依据本地OpenDiablo2 globeWidget及当前MPQ。聊天font8正文／font16输入和日志标题采用同一文字尺度，换行、光标及行距同步；原边框与面板布局保留。原服数值与未知值投影沿用，不修改资源值。完整窗口尺寸及球体悬停仍缺原版对照。

底栏只加载原联机minipanel，八个按钮绘制／命中统一；P／Party Screen打开原图组队名册，Invite／Cancel／Accept／Leave共用原服0x5E请求，结果读取名册回包。同队行显示绿色名字、职业、等级、队伍状态和原区域名，按原38像素行距分组／分隔；左上角队友用MPQ七职业头像、原TRANS50混色及0x7F生命条。入口及限制见[联网模块](../../modules/NETWORK.md#组队邀请客户端)。当前MPQ的minipanelbtn帧对依次是人物、背包、技能、组队、自动地图、消息日志、任务、菜单；hudMiniActions统一真实帧与语义动作，提示读取minipanel系列TBL并显示当前实际绑定。消息日志的物理M、滚轮、PageUp／PageDown、Home／End已接入FrameInput；底栏按钮也走同一ChatView入口并报告缺资源原因。普通ASCII聊天用Enter输入／发送，顶部最多15条、每条独立10秒、新消息在下，随左侧面板边界右移；原版资源依据及限制见联网模块。面板存在不代表对应服务完成；佣兵、赌博及任务特殊服务保持明确限制。

## 装备耐久提示

右上角小人使用当前MPQ `data/global/ui/panel/duroicon.dc6`，33帧分为灰／黄／红三组，每组11个部位；保持原48×64透明画布和DC6底部原点，逐层用当前幕PL2 TRANS50混色，不重绘小人或用RGBA染色代替原帧。头／胸甲／腰带／鞋／手套及当前武器组左右手按原顺序叠加；空防具槽仍有灰色轮廓，空手省略，盾牌与箭袋外形由当前MPQ ItemTypes继承／Quiver字段选择。

RemoteUiClients从已解码且revision一致的原物品副本投影整数 `当前耐久×100／实际最大耐久`，最大值沿现有原属性／孔内修饰计算，显示百分比封顶100。只纳入当前原表有耐久、非nodurability、非stackable、无不可摧毁属性且最大／当前值与孔内属性完整的物品；未知值不作零耐久。至少一个活动装备的整数百分比≤20才显示小人，正常部位灰色，1–20黄色，0红色；这是整数截断结果，不把红色条件简化为真实耐久必须为0。备用武器不触发本人当前提示，背包／仓库／佣兵装备不参与。

原锚点为画面宽度−60、基线110；打开自动地图基线改为140，自动地图打开且有真实名册玩家对本人的0x8C关系flag8时改为183。图形与距离按现有classicPanelScale适配，打开背包／商店／依附背包的加工面板时隐藏。维修、卸装、武器组切换与新局随库存副本刷新，不新增可写耐久状态、权威请求或存档字段。

依据当前MPQ原图及本机同快照1.13c：D2Client RVA `0x27E06–0x27E1A`加载DuroIcon，`0x28680`筛选／分色，`0x28420`部位／坐标／mode1绘制，`0xC3A69–0xC3A81`隐藏分支，`0x9DB90`核对他人对本人的敌意；D2Common ordinal10865／10048／11116分别提供耐久资格、stackable、实际最大耐久。参考项目的durwarning仅为附加字段线索，不用它推断20%阈值。当前源码已接；完整装备类型、缩放／面板／多人敌意组合及原版逐像素效果仍待核对。

## 手势与输入

首次世界按下确定移动／交互／拾取／左或右技能；单位技能按GUID锁定，按住不换到另一个目标。空地行走保持世界目标，拖动才更新，到达后可续走；松键／失焦／目标失效／UI接管结束未发意图。0x12仅原地狱火停止语义，已发普通动作可由原服完成。

UI消费的按下归UI直到释放，关闭面板或改变视口不会把同次按住转成世界动作。普通面板热区与世界分流，面板、ESC及失焦不暂停原服／网络；显式测试pause另见[调试入口](../../development/DEBUG_PIPE.md)。

物理键在`app/input.cpp`换算成FrameInput语义位，与调试管道`ui-input`的键名一致：I／A／S开背包／人物面板／技能树，Q任务日志，O佣兵，P组队，M消息日志，Enter聊天，1–4用腰带、B展开腰带，F1–F8选鼠标技能（悬停时为绑定），R走跑切换，Tab自动地图、V换侧、Home居中，F12地图名字、Ctrl+F12截图，Ctrl+F1本面板，按住Alt显示地面物品名，W武器组切换。Ctrl组合保留原有调试键；GetCharPressed提供可打印ASCII及金币数字，非ASCII聊天仍明确拒绝，不据此宣称中文输入完成。方向键、Home／End、Delete编辑输入，PageUp／PageDown及滚轮提供日志翻页／滚动；完整实体键盘、窗口尺寸及组合输入仍待核对。

## 进入游戏的开门动画

Loading页不再借用游戏选择背景加Logo，改播原版`Loadingscreen.dc6`：黑底居中一张256×256帧序列，石门由缝逐渐打开后停在末帧不再循环，加载多久就保持多久。帧数据取`data/local/ui/loadingscreen.dc6`（非英语语言目录）或`data/global/ui/loading/loadingscreen.dc6`（`eng`），另一成员缺失时回退，调色板为`data/global/palette/loading/pal.dat`；两成员都缺时启动即报缺资源。进入Loading页时帧序号归零，按时间推进，约一秒播完十帧后夹在末帧。800×600画布内锚点为(272,172)，即宽高各减256后的一半。

原1.13c D2Client静态证据：RVA `0x71580`按语言选路径并加载DC6、加载loading调色板且帧序号清零，RVA `0x71760`把帧画在宽/2−128、底=高/2+128的位置并把序号推进到不超过末帧。原逐帧节奏未逐帧认证，当前按表现需要约每秒9帧推进；等服务器世界数据的提示与Leave按钮移到门下方，错误／确认弹窗仍覆盖在动画之上。连接中阶段的"Please Wait"弹窗不再在Loading页弹出，原版进游戏只有加载画面；Loading页按Escape仍退局，错误提示与确认仍可中断。

人物、技能与底栏读同一纯投影，缺值显示 `?`。资源固定点按ItemStatCost转换；初始零整数生命且原服存活时HUD1血投影不修改副本，真实死亡等原动作／模式。

## 角色面板

人物面板保持invchar6前四块原图的320×432布局、侧栏原生坐标(80,64)及统一classicPanelScale；原图自带的资源数值格灰色石纹常驻，不覆盖为自绘色块。字段标题读取当前MPQ strchr系列TBL，原font6标签与font16姓名／数值保留白色字形、DC6底部原点和TBL字宽；两行标签按原基线±4像素居中。长角色名按原字节长度切换font8／font6，资源及防御数值≥1000或超过原格宽时切换font8，未知值仍为原font16问号。姓名与经验、属性、左右动作伤害／命中、防御、三种资源、抗性与剩余点数按原格框排布；经验千位分隔，资源最大值在左／当前值在右，抗性数字无百分号。未显示原版没有的格挡／物理抗性等额外行。

剩余点数用skillpoints底图与PL2红色font6两行文案；有已知剩余点时绘制levelsocket和level加点原帧，关闭用buysellbtn第10帧圆圈斜杠图；绘制与命中共用character_panel.hpp坐标。已知负抗性用原PL2红色font16，增减属性及封顶抗性的完整颜色规则仍缺必要输入；伤害／命中／防御等未知值不复制截图或本地伪算。

当前源码补属性按钮的level第1帧及关闭按钮第11帧按压状态，按下只记录表现手势，在同一按钮松开并复验已知剩余点数后才提交AllocateAttribute；移出松开、失焦、死亡、模态接管或面板关闭取消手势。失焦、死亡及全部模态组合仍待核对。

依据当前MPQ与本地OpenDiablo2 hero_stats_panel／button，辅以原1.13c D2Client静态RVA BCEA0人物面板、DD5E0标签／DD6F0数值布局表、BD613姓名字体切换、BDB54数值字体切换及原加点记录DD810。灰底常驻由用户原版实测确认，图形仅取MPQ。原版完整像素一致性与状态色仍待核对。

## 技能菜单

Skills.skilldesc关联SkillDesc的IconCel／ListRow／ListPool及当前职业图集；48×48原图按底栏比例缩放，跳负行，压缩非空行，左右菜单各向对应侧排列。通用动作在底行，同图卷轴／书候选按ListPool合并；原表及图标身份不代表全部效果完成。

页签使用当前MPQ StrSklTree分片文案及原font16白字，按原分行居中，不使用CharStats.StrSkillTab的物品加成格式。剩余点数区读取StrSklTree1／2／3并共用文字绘制。关闭使用buysellbtn第10帧圆圈斜杠，SkillDesc的SkillPage／SkillRow／SkillColumn经CharacterView决定末行空列，优先左／右外列，否则中列；不再按职业写死预留列。MPQ未发现独立按钮坐标字段，格内锚点及y=385取本地OpenDiablo2原UI布局，绘制／命中共用skillTreeClose。七职业MPQ末行空列与参考预留位置已逐项核对，完整操作与像素仍待原版对照。

技能树保持原(右侧,64)面板锚点和48×48节点；有剩余技能点时用共用canAllocate资格决定白色／灰色，零级且满足原等级／前置／上限等条件也为白色。已知无点数时已学技能保持正常，未学技能灰色；未知资格不授权加点。点击只对canAllocate节点提交AllocateSkill，发送端仍复验，结果等待原服。禁用图标使用Sky PL2第5号灰色变色表，悬停用第3号蓝色变色表，替代RGB压暗和自绘悬停描边；技能选择器及底栏的施放资格不因此改变。

等级数字显示有效等级，已知高于基础等级用原PL2第3号蓝色、低于基础用第1号红色，其余保留白字；已知零级正常状态不画数字，未知显示问号。单个数字用font16，超过9级用fontformal10；按原DC6底部原点、TBL字宽和侧栏统一尺度绘制，基线=图标下沿+12。当前单个数字x=图标右沿+4，两位以上x=图标右沿，对齐原背景右下角小格。依据用户原版技能树截图、本地OpenDiablo2 skillicon及1.13c D2Client RVA 78AA0／78C50节点状态分支、78979图标变色、78A39–78A8C等级字体／色号／坐标；MPQ只有正常／按下的IconCel相邻帧，灰色不是另一个原帧。当前源码补IconCel+1的按压帧并沿用蓝色PL2悬停变换，关闭按钮使用第11帧；在同一节点松开并重新确认canAllocate才提交AllocateSkill。移出松开／失焦／关闭不提交，不预增技能等级。全部职业及完整技能树状态仍待原版对照。

技能选择器、底栏技能槽和技能树的提示共用skill_tooltip：当前MPQ font16原尺寸、按TBL原字宽步进、逐行居中，保留说明／等级／数值／协同区之间的空行。当前源码补选择器使用状态红字（含标题），普通提示仍白字；树的标题与协同标题使用Sky PL2原绿色字体变色表，正文按学习状态白色／红色，不以RGB乘色代替原字体索引映射。黑色半透明框按文字测宽并限制在屏幕内：选择器锚定技能图标上沿中心；技能树按用户Blaze参考图优先放在图标下沿，空间不足转到上方。长行按词换行，不缩小字体，不附加金色标题或边框。未学习且已知为零级的技能树节点使用原StrSkill17 First Level预览，不显示Current Skill Level 0；已学习节点显示当前／下一等级，底部单列原dsc3协同说明。数值、使用状态输入及未完成边界由[MPQ 描述层](../skills/DESCRIPTIONS.md)维护，人物值投影见[角色模块](../../modules/CHARACTER.md)。完整技能及装备状态仍待原版对照。

悬停菜单后F1–F8绑定，单按选择不立即施法；原0x51无即时ACK，0x7B重入恢复。菜单的选择资格与底栏施放资格分开，城镇禁施不把菜单所有主动技能染红；完整充能／物品来源和精确禁用调色板仍未认证。

## 传送点与菜单

原800borderframe、waygatebackground／icons、expwaygatetabs及Levels.Waypoint提供边框、九槽、标题／页签／原颜色。只有原0x63确认菜单及历史解锁位可旅行；请求未确认、关闭／迟到回复分别处理，不自行解锁。启动期间点击／ON动画的原版依据与时序缺口见[物件](../world/OBJECTS.md#自研传送点)。

ESC先关闭面板／对白，无面板时开原退出菜单；Options只接已有Automap Options，其他设置页未实现。Save and Exit提交原保存退局，确认后返服务器选角，超时保存未知；不会本地写D2S或暂停世界。

## 证据与限制

### 主要面板对照边界

原资源不等于原版全部布局、状态与操作已经完成。面板的共同实施入口为presentation，位置来源先查当前MPQ；D2MOO的InvTbls只解释原表布局、PlrMsg解释请求，不提供D2Client绘制实现。MPQ没有的布局常量只采用已核实的本地参考／同快照1.13c静态证据；未核实部分保留限制，不补造界面。

| 面板 | 当前原资源／代码入口 | 尚未完成的原版一致性范围 |
| --- | --- | --- |
| 人物属性 | invchar6前四帧、font6／font16、strchr系列；character_panel、character_panel.hpp | 完整属性增减／封顶状态色、悬停详情及按钮／模态组合 |
| 七职业技能树／选择器 | 职业原树图、SkillDesc位置与IconCel、原字体／PL2；skill_tree_view、skill_assets、hud_controls | 全职业全状态、连续操作及原版逐像素对照 |
| 背包／装备 | invchar6后四帧、invchar6Tab、每职业inventory.txt；inventory_panel、inventory_view | 武器标签／全部装备特殊定位与按压状态、边缘像素及各种缩放仍需原版对照 |
| 仓库／方块／金币 | bank／supertransmogrifier／dialogbackground原图及原表／TBL；storage_view、cube_view、inventory_view | 当前金币弹窗替换为原210×158图和原确认／取消按钮，长本地化文案、输入框完整选中／箭头表现及原版逐像素布局未认证 |
| 商店／赌博／玩家交易 | buysell／trade原图、原物品副本及交易格；npc_shop_view、trade_invite_view | 商店购买确认仍有自绘YES／NO及适配布局，分页及全部按压状态未对齐；不可宣称商店整体准确还原 |
| 任务日志 | questbackground／expquesttabs／questsockets／任务原动画；quest_panel、quest_controller | 完整任务动画／文字／重播节奏及所有幕状态 |
| 佣兵／雇用、组队 | 原hireling／party面板、inventory原槽位／原字体；hireling_view、commerce_controller、party_view | 全职业／雇用列表与多人关系组合、完整按压及像素对照 |
| 传送点 | waygatebackground／expwaygatetabs／waygateicons；waypoint_view | 原版逐像素布局及全幕／不可达状态组合 |
| 消息日志／聊天 | boxpieces／textslid、strMsgLog／strClose、原字体；chat_view、app/input | 实体键盘人工操作、全部窗口尺寸、精确原版布局及中文输入未完成 |
| ESC／设置 | 原菜单标签与标记；exit_view、hud | 只接Automap Options；其余原设置页、完整原按压／导航仍缺实现 |

直接读取五个当前MPQ，确认minipanelbtn的真实帧身份、dialogbackground的210×158尺寸及inventory原表。金币文案／菜单参数另定点核对同快照D2Client RVA C31A8–C320D、C3212–C322F、C32CC–C32FC及C341B–C348F；保持原图与DLL只读。这些静态依据只裁定图集／原表及原版菜单参数，完整职业／窗口尺寸、多人／原服、音频、Linux及逐像素矩阵仍待核对。

原图布局核对本地OpenDiablo2 HUD／skill_select_panel／quest_log／escape_menu、Diablerie EnemyBar／PlayerController／MouseSelection及用户原版观察，原请求依据D2MOO PlrMsg。精确采样／Shift组合、全部面板鼠标服务、全部原像素／透明／染色与多人未认证；当前操作表见[README](../../../README.md)，有限原服证据见[联网记录](../../modules/NETWORK.md)。
