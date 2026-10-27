# 经典HUD与游戏界面

当前产品只保留SceneView／SceneController／ViewState一套界面，RemoteUiClients提供原服只读值并提交语义命令。世界、自动地图、原图缓存、动画与声音也使用公共入口；没有Local适配或联机专用HUD。

## 原资源与布局

800ctrlpnl7.dc6提供800像素底栏；原球体／耐力／经验、技能槽、腰带、人物／技能树、背包／箱子／方块、NPC／商店、任务／佣兵、传送点和ESC面板读取当前MPQ原图／字体／TBL。侧栏裁剪、相机和命中共用worldViewport，数据缺失不自绘替代资源。

生命／法力球悬停数值使用当前MPQ panelhealth／panelmana格式与font16原字形，保留原灰白像素，不乘parchment黄色，不缩到12；字距按原TBL字宽。字形保持原像素大小（hudTextScale=1），不再乘W/800的hudScale；球体布局、命中及文字锚点仍随HUD适配，外层视口统一缩放最终画布。原800坐标下生命文字中心x=65、法力中心x=宽度−80，基线y=高度−95；DC6底部原点转换到字形上沿，不以球体矩形中心或固定20像素偏移定位。原1.13c D2Client RVA 0x276B5／0x27767读取两条TBL，0x276EE–0x27703／0x277AC–0x277C2确认颜色0与锚点；font16资源依据本地OpenDiablo2 globeWidget及当前MPQ。聊天font8正文／font16输入和日志标题采用同一文字尺度，换行、光标及行距同步；原边框与面板布局保留。原服数值与未知值投影沿用，不修改资源值。完整窗口尺寸及球体悬停仍缺原版对照。

底栏只加载原联机minipanel，八个按钮绘制／命中统一；P／Party Screen打开原图组队名册，Invite／Cancel／Accept／Leave共用原服0x5E请求，结果读取名册回包。同队行显示绿色名字、职业、等级、队伍状态和原区域名，按原38像素行距分组／分隔；左上角队友用MPQ七职业头像、原TRANS50混色及0x7F生命条。入口及限制见[联网模块](../../modules/NETWORK.md#组队邀请客户端)。当前MPQ的minipanelbtn帧对依次是人物、背包、技能、组队、自动地图、消息日志、任务、菜单；hudMiniActions统一真实帧与语义动作，提示读取minipanel系列TBL并显示当前实际绑定。消息日志的物理M、滚轮、PageUp／PageDown、Home／End已接入FrameInput；底栏按钮也走同一ChatView入口并报告缺资源原因。普通ASCII聊天用Enter输入／发送，顶部最多15条、每条独立10秒、新消息在下，随左侧面板边界右移；原版资源依据及限制见联网模块。面板存在不代表对应服务完成；佣兵、赌博及任务特殊服务保持明确限制。

## 聊天与消息日志

聊天界面依据与源码：用户提供原版底部输入、左上回显及M消息日志三张截图，历史最新消息在上、真实角色名深金色／普通文字白色、标题Message Log、底部Close、右侧滚动条。OpenDiablo2 `d2game/d2player/key_map.go`核对Enter／M，`d2common/d2resource/resource_paths.go`及`d2core/d2gui/box.go`／`layout_scrollbar.go`核对boxpieces／textslid原帧用途。当前MPQ：boxpieces为22帧14×15，textslid为17帧12×13；TBL的strMsgLog／strClose存在；对照截图字形和尺寸选font8消息、font16输入／标题，字宽沿原TBL。`presentation/hud/chat_view.*`独立维护只读消息、编辑草稿、光标及滚动；边框步距从原帧尺寸读取，位置按截图800像素基准适配当前HUD尺度。Sky原PL2十三种字体索引变换按需上传，白色保留原索引；messageColor控制正文，姓名色独立于等级字段，支持原FF c0–c9颜色串（本地d2bs Print用例补证）。标题／Close动态读当前MPQ。

Enter打开底部输入，再次Enter提交普通广播；空白Enter关闭，Esc丢弃草稿。Backspace／Delete、左右／Home／End编辑光标，255字节限制，非ASCII输入明确提示；提交拒绝保留草稿。M／底栏Message Log打开历史，M／Esc／Close关闭；滚轮、上下、PageUp／PageDown、Home／End及原箭头／滚动条拖拽翻阅，新消息到达时保留正在查看的旧行锚点。输入／日志占用整个开启、编辑和关闭帧，隔离走跑、自动地图、背包／技能、药剂／换装及鼠标玩法输入；持有按钮需重新释放，服务器／网络不暂停。关闭与新局不重放消息或玩法意图，换区保留本局历史。

当前显示限language=0的普通type1／私聊type2和6／server type3／noname type4及ASCII正文，其余真实原包保留但计chatUi.unavailableMessages，不猜表情／线索格式。用户补充原版实测：每条显示10秒、新消息在下、最多15条，左侧角色等面板打开时右移。顶部源码取最新15条可显示消息，按收到顺序向下排列，以原消息receivedMilliseconds的单调时钟独立过期；新消息、打开／关闭日志及面板不重置寿命。绘制使用SceneView::worldViewport的左边界和可用宽度，重新换行并裁剪在世界区域内；过期只隐藏顶部消息，不清除本局最多256条历史，M日志继续沿截图最新在上。font8正文／font16输入及标题保留原像素大小，字宽、换行、光标和行距同步。中文原编码／输入法、频道、头顶气泡绘制及全部窗口／字体精确一致性仍暂缓。

聊天发送、原包字段、队列与服务器公告依据见[多人只读副本](../../modules/NETWORK.md#多人只读副本)；本节只维护界面，不生成本地成功消息或授予玩法权限。

## 装备耐久提示

右上角小人使用当前MPQ `data/global/ui/panel/duroicon.dc6`，33帧分为灰／黄／红三组，每组11个部位；保持原48×64透明画布和DC6底部原点，逐层用当前幕PL2 TRANS50混色，不重绘小人或用RGBA染色代替原帧。头／胸甲／腰带／鞋／手套及当前武器组左右手按原顺序叠加；空防具槽仍有灰色轮廓，空手省略，盾牌与箭袋外形由当前MPQ ItemTypes继承／Quiver字段选择。

RemoteUiClients从已解码且revision一致的原物品副本投影整数 `当前耐久×100／实际最大耐久`，最大值沿现有原属性／孔内修饰计算，显示百分比封顶100。只纳入当前原表有耐久、非nodurability、非stackable、无不可摧毁属性且最大／当前值与孔内属性完整的物品；未知值不作零耐久。至少一个活动装备的整数百分比≤20才显示小人，正常部位灰色，1–20黄色，0红色；这是整数截断结果，不把红色条件简化为真实耐久必须为0。备用武器不触发本人当前提示，背包／仓库／佣兵装备不参与。

原锚点为画面宽度−60、基线110；打开自动地图基线改为140，自动地图打开且有真实名册玩家对本人的0x8C关系flag8时改为183。图形与距离按现有classicPanelScale适配，打开背包／商店／依附背包的加工面板时隐藏。维修、卸装、武器组切换与新局随库存副本刷新，不新增可写耐久状态、权威请求或存档字段。

依据当前MPQ原图及本机同快照1.13c：D2Client RVA `0x27E06–0x27E1A`加载DuroIcon，`0x28680`筛选／分色，`0x28420`部位／坐标／mode1绘制，`0xC3A69–0xC3A81`隐藏分支，`0x9DB90`核对他人对本人的敌意；D2Common ordinal10865／10048／11116分别提供耐久资格、stackable、实际最大耐久。参考项目的durwarning仅为附加字段线索，不用它推断20%阈值。当前源码已接；完整装备类型、缩放／面板／多人敌意组合及原版逐像素效果仍待核对。

## 手势与输入

首次世界按下确定移动／交互／拾取／左或右技能；单位技能按GUID锁定，按住不换到另一个目标。空地行走保持世界目标，拖动才更新，到达后可续走；松键／失焦／目标失效／UI接管结束未发意图。0x12仅原地狱火停止语义，已发普通动作可由原服完成。

UI消费的按下归UI直到释放，关闭面板或改变视口不会把同次按住转成世界动作。普通面板热区与世界分流，面板、ESC及失焦不暂停原服／网络；显式测试pause另见[调试入口](../../development/DEBUG_PIPE.md)。

物理键在`app/input.cpp`按活动ControlBindings换算为FrameInput语义位。默认I／B背包、A／C人物、T技能树、S右键技能菜单、Q任务、O佣兵、P组队、M日志、Enter聊天、1–4腰带、波浪号展开腰带、F1–F8技能槽1–8、R走跑、Tab自动地图、V换侧、F9淡化、F10居中、F11队友、F12地图名字、PrintScreen截图、H帮助、Alt地面物品、Shift原地攻击、W换装、Z头像、Space清屏、N清除顶部消息。16个技能槽均可配置物理键，9–16默认None；临时跑步默认None。默认表依据本机原版default.key，不保留旧的Ctrl+F1／Ctrl+F12及Home地图绑定。HUD按钮提示、技能菜单快捷键文字和帮助页读取活动绑定。物理Shift／Ctrl继续用于背包和文本编辑，可配置的原地攻击／临时跑步另有语义字段；原Ctrl调试组合及Ctrl+Z附加面板仍独立，配置不支持组合键。GetCharPressed、聊天内Enter发送、方向键／Home／End／Delete编辑与日志导航不随玩法键位改变。调试`ui-input`仍是语义注入，不按用户物理配置解释。

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

悬停菜单后按已配置的技能槽键绑定，单按选择不立即施法；客户端投影已接全部16槽，原0x51无即时ACK，0x7B重入恢复。菜单的选择资格与底栏施放资格分开，城镇禁施不把菜单所有主动技能染红；完整充能／物品来源和精确禁用调色板仍未认证。

## 传送点与菜单

原800borderframe、waygatebackground／icons、expwaygatetabs及Levels.Waypoint提供边框、九槽、标题／页签／原颜色。只有原0x63确认菜单及历史解锁位可旅行；请求未确认、关闭／迟到回复分别处理，不自行解锁。启动期间点击／ON动画的原版依据与时序缺口见[物件](../world/OBJECTS.md#自研传送点)。

背包、人物、技能树、任务、佣兵、组队、消息日志、聊天、走跑、武器组及技能／腰带快捷键先退出传送点交互，再处理同次输入；普通字符输入也退出，Tab自动地图保留原版例外。列表外的左右键按下先关闭列表，再交给对应HUD／场景动作，列表内页签／目的地操作仍由列表消费；ESC只关闭列表。等待0x63期间的新键盘动作或点击也取消原请求，迟到菜单沿既有关闭路径处理。退出发送原0x49（源GUID、目的地0），清除客户端交互并刷新本帧上下文，关闭本身不增加移动节流时间，避免同次移动／交互因旧上下文或额外等待被拒绝。

退出依据为本机同快照1.13c D2Client：RVA `AA380–AA3D0`构造9字节关闭包并防重复，`AAD50–AAEB8`处理字符输入并跳过Tab，`AA4E0–AA60E`核对列表外鼠标按下的关闭分支；服务端参数与取消语义核对D2MOO PlrMsg::Rcv0x49_TakeOrCloseWp。当前仅静态核对，完整原版实机输入及多人时序仍待验证。

## ESC 菜单

ESC先关闭面板／对白，无面板时打开游戏菜单。`hud/escape_menu`加载所选语言原资源、缺图回退英文，并维护绘制／命中几何；SceneController只消费选择、切换页面和提交已支持动作。主菜单为Options／Save and Exit Game／Return to Game；Options的Sound Options、Video Options、Automap Options和Configure Controls均可进入，保留Previous Menu／Cancel／Default／Accept。无后端的3D Sound／EAX／3D Bias及其余画面设置使用MPQ Sky PL2第5号灰色表并禁用。Configure Controls已接双键编辑、草稿默认／取消／应用及客户端共享保存。

菜单标签通过`localUiArtPath`优先读取所选语言的`data/local/ui/<language>/*.dc6`，缺图才回退英文；使用原分片、底部原点和原像素尺寸，不随hudScale拉伸，也不由TTF字体重绘。按键页的功能文字和字体另按下述英文TBL／Latin资源读取。3DSound原图实际文案为3D SOUND；大字Previous与设置页SPrevious使用各自原资源。普通菜单按`(H−80)/2−行数×50/2`定位，标签基线偏移39；设置页包含标题，行距45、基线偏移34，左右列锚点为W/2±230。五芒星原8帧、相反方向、50ms帧间隔，锚点为W/2±249，保留原帧偏移及底部原点。扩展窗口沿原居中公式延伸，不缩放菜单字形。

按键页直接读取英文TBL和Latin FontInGameChat的DC6／TBL，原表62行含56功能及6个分隔空行，每屏15行、行距18；保留620×369原边框、三列、原textslid滚动条、底部三个按钮。点击功能／主键列编辑主键，点击副键列编辑副键；键盘上下逐项选择并滚动入视野，首尾停止，左右在两个槽间选择、不循环；Enter进入捕获，鼠标悬停不改变键盘选中行。滚轮、PageUp／PageDown、Home／End及拖动滚动条浏览。捕获时Esc只取消捕获，重复绑定清除全表原槽为None，不设Delete／Backspace清空捷径。Default只重置草稿；Cancel及非捕获时Esc丢弃，Accept应用并保存；捕获优先消费按键，失焦取消捕获。具体格式与失败处理见[客户端偏好](../../modules/SAVES.md#客户端偏好凭据与探索)。

字体颜色全部来自Sky PL2原索引变换，不猜RGB：按钮未悬停4（金），Cancel悬停1（红）、Default 3（蓝）、Accept 2（绿）；选中键位3，未选中已绑定4，空槽5（灰），双槽空1（红），普通功能标签0（白）。取证为同份1.13c D2Client按钮表RVA F8B98（三条26字节记录、色号偏移6）、绘制12CE0–12D26、绑定绘制127D0–12A58及槽存在判定398C0；用户原版实测确认按钮颜色、选中蓝色／捕获蓝色闪烁、Esc取消捕获、冲突清除及不循环键盘导航。当前捕获按时间闪烁，原版按帧计数，精确节奏未认证。上一／下一技能及8条角色语音功能无已核实消费者，保留原行并灰显，不接受编辑；普通键盘、中键／侧键及滚轮已接编码，但全部设备与原版完整允许键集合仍待实测。

声音／画面页保留原标签、档位、滑条和骷髅滑块；无后端控件按原菜单记录初值灰显，不能把该初值当成后端已生效。缺原图报错，不生成替代图形。按键编辑仍缺专项实机回归，不能据代码接入或数值场景入局认定真实画面、实体键盘及重启保存已验证。

Sound、Music与NPC Speech接入真实音频和客户端偏好。两条滑块均为0–100、每档5的21位置，原265像素行程；左右键每次一档、端点钳制，鼠标须从原滑条内按下，随后可拖出并钳制；Enter不改变滑块。Sound是总音量，影响已播放及后续效果音、循环飞弹、语音和音乐；Music仅额外衰减Sounds的Music Vol通道。按原整数顺序计算音量，移除旧固定0.45增益。NPC Speech按Audio Only／Text Only／Audio + Text循环，左右键反向／正向；前者隐藏字幕、语音结束沿既有对白确认路径结束，后两者分别禁止语音／同时显示播放。取消、切换对白和退局释放语音流。默认Sound=100、Music=50、NPC Speech=Audio + Text；启动先应用偏好再播放。

`audio/scene_streams`按Levels.SoundEnv→SoundEnviron.Song读取环境音乐，按Sounds原WAV、Loop、Volume、Music Vol与25Hz Fade字段播放；原75帧最短切歌间隔、125帧更新下一Block恢复点。NPC语音优先用实际TBL stringId→a1–5npc原映射→Sounds.FileName，普通闲谈使用已解析的原文映射；歧义不借用其他NPC声音。WAV解码器与其借用的MPQ字节同寿命，不落地解压文件。当前`assets/mpq2`缺d2music／d2xmusic／d2speech／d2xtalk，缺成员仅记录一次诊断，保持无声，不生成替代音频；使用包含这些同版本原包的`--mpq`目录后才能听到相应资源。环境音乐之外的任务插曲、完整原版混音／字幕速度及流边界时序仍未认证。

声音静态依据为同份1.13c D2Client的64400–64470（21档百分比）、6504A–65128（滑条命中／拖动）、64250–642D5（语音／字幕标志）、5D24C／5D26A／5D45D（Music、Master、Sounds.Volume整数联动）、456A0–45962（环境音乐及Block恢复）；D2Sound（MD5 `b07372c8e740cf04f45b8e2e63a01346`）9B30–9C26确认默认值及合法范围，9A40／9AA0／9AD0为三项保存入口。d2x保存到client-settings.json，不写原版注册表或角色D2S。完整音频操作与原版实机对照尚未执行。

Video Options的Lighting Quality沿原图位置显示On／Off并默认On；鼠标、Enter和左右键均切换，Off为世界全亮，切回On恢复当前光照。该开关按用户要求替代原版High／Medium／Low；具体绘制与保存边界见[场景照明](../world/LIGHTING.md)。其余未支持画面选项继续置灰。

自动地图五项沿用当前已支持状态与偏好保存；大小切换、Fade四档与小图跳过Center保持原规则，支持鼠标／Enter及左右方向键。普通菜单上下导航环绕并跳过禁用行；设置页ESC关闭菜单，按键页ESC返回Options。Save and Exit提交原保存退局，确认后返服务器选角，超时保存未知；不在UI写D2S；单机／联机的暂停由app/frontend既有宿主及表现策略负责。

布局与内容静态依据为本机1.13c D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）：菜单表RVA E2AC0／E3AC8／E5570／EA550／EEFE0，绘制65230–65574、命中64080、左右切换64D20／64E00及导航64EE0／64F50；按键表F8928（62条10字节记录）、布局11410、绘制12570–12DB8及按钮表F8B98。D2Win的字体名表RVA1C588第13项确认FontInGameChat。原DLL／资源仅只读，导出物不提交。当前仅静态核对，尚未进行原版实机交互或逐像素画面对照；全部菜单状态与扩展窗口仍需实际确认。

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
| ESC／设置 | 按所选语言的菜单原图、缺图回退英文／Latin FontInGameChat／PL2；escape_menu、hud、controller | 自动地图、声音、Lighting及双键编辑已接；3D音效／其余画面／技能循环和角色语音键灰显；实机按压、导航、重启保存与逐像素对照未执行 |

直接读取五个当前MPQ，确认minipanelbtn的真实帧身份、dialogbackground的210×158尺寸及inventory原表。金币文案／菜单参数另定点核对同快照D2Client RVA C31A8–C320D、C3212–C322F、C32CC–C32FC及C341B–C348F；保持原图与DLL只读。这些静态依据只裁定图集／原表及原版菜单参数，完整职业／窗口尺寸、多人／原服、音频、Linux及逐像素矩阵仍待核对。

原图布局核对本地OpenDiablo2 HUD／skill_select_panel／quest_log／escape_menu、Diablerie EnemyBar／PlayerController／MouseSelection及用户原版观察，原请求依据D2MOO PlrMsg。精确采样／Shift组合、全部面板鼠标服务、全部原像素／透明／染色与多人未认证；当前操作表见[README](../../../README.md)，有限原服证据见[联网记录](../../modules/NETWORK.md)。

## 客户端普通施法与移动输入

原1.13c D2Client（SHA-256 `dd8bc6025de921216a97c17f97cd1a50fbb85926e838ec60e13451448836d906`，ImageBase 0x6FAB0000）的地图点击入口RVA `0x1BF20`，左键按下`0x1BDE0`与按住`0x1BCC0`先调用输入资格`0xA54A0`。该函数对玩家当前模式CAST（10）返回不允许，攻击／格挡／投掷等也有各自模式分支；SEQUENCE另走`0xA4C50`，不能一律当普通施法。后续`0x29F30`才检查已用技能的`Skills.interrupt`位。普通SC动作的左键限制不能只看interrupt：当前原表Fire Bolt与Ice Bolt的interrupt均为1，仍先受CAST模式门槛约束。此处是固定原DLL静态调用链，不能把允许受击打断解释成允许鼠标移动提前结束动作。

`src/presentation/remote/remote_scene.cpp::localCastBlocksMovement`对MPQ `anim=SC`且已开始、尚未结束的本人施法使用完整动画时钟，不以释放帧作为结束。`remote_scene_draw.cpp`据此提供移动输入资格，`presentation/controller.cpp`只在仍按住左键时保留待发送移动；提前松开则撤销这次手势。`remote_scene_effects.cpp`和`remote_scene_movement.cpp`还对其他入口已提交的移动请求延后中断／预测，保留施法原点及尚未到释放帧的Clt飞弹。动作正常完成当帧的移动走正常结束清理，不取消该帧稍后推进时刚到释放时点的飞弹。移动请求不是服务端已接受的模式切换；客户端副本、权威施放和扣费不由这个显示门槛改写。

真实受击／击退／死亡及换区仍沿原有动作中断清理；已发射的飞弹继续拥有自己的寿命，取消待释放飞弹只作用于尚未出生的显示请求。此修正共用于原版与PD2普通SC施法，喷流和各专用序列保留原执行路径，不据此宣称其他模式、全部FCR／变形、延迟和多人交错已逐帧对齐。
