# 死灵法师技能

PD2专项内容：[职业技能](../../pd2/CLASS_SKILLS.md)。本页保留原版／公共规则及必要的版本边界。

更新：2026-11-09。自研服务端已接入66–95共30项技能（27项主动、3项被动）。本页维护当前实现、原版依据与缺口；规则来自当前MPQ，master的纯求值与D2MOO作为执行线索，旧单机会话执行器没有迁回客户端。

骷髅支配、石魔支配及召唤抵抗的提示与实际召唤共用`evaluateNecromancerPassive`，法师元素长度、降低抵抗递减、范围与AI资格复用[公共函数](COMMON.md#公共函数抽取)。参数在content从当前MPQ准备，客户端被动提示不再逐次查表。完整技能组合仍待原版对齐，存档校验以[SAVES](../../modules/SAVES.md)为准。

## 30项职责核对

骨甲的StateFact与其他防御状态共用`nativeEffectStats`，从实际安装列表读取吸收池及modifierlist_skill／modifierlist_level；不再另造只有两个吸收字段的包。D2MOO SkillSor::SKILLS_SrvDo018_DefensiveBuff明确写入来源元数据，原版2级骨甲有限对照为bonearmor／bonearmormax各23040、来源技能68／等级2；护盾受击后的余量仍以唯一效果列表为准。

按技能 ID 记录本地 d2decomped 对照、原版 1.13c 静态入口和当前 MPQ 的执行依据。对照结论限于表列路径。

| 技能 ID／名称 | 原版对照与当前执行 |
| --- | --- |
| 66 Amplify Damage | d2decomped 的物抗修正与原 SrvDo30 `505D0` 和 `damageresist=-par5` 一致，免疫对象只取五分之一。原 CltDo30 `C8B40` 创建 `cursecenter`，按 `max(2,AuraRangeCalc-3)` 投放表列图形，客户端已改用原中心程序。 |
| 67 Teeth | d2decomped 与原 SrvDo8 `4D420`／CltDo17 `15DA0` 都使用整数横向扇形，当前 Calc3 为空时所有牙走中段。服务端直接取 `min(ln12,24)` 并从整数施法格朝扇形目标发射，伤害和命中延迟仍取各自表。 |
| 68 Bone Armor | d2decomped 的 8.8 物理吸收与原 SrvDo18 `428F0`／Event22 `4DFE0` 对应，原表两槽安装相同最大值与当前值。客户端 setfunc6 `73230` 读取池量三等分切换骨片，现有执行已保留耗尽撤出和两端表现分工。 |
| 69 Skeleton Mastery | d2decomped 在召唤时读取支配，原 D2Game `4F9B6` 也从召唤技能的 PassiveCalc 槽对出生单位写值。当前骷髅、法师和重生按各自表引用取主人有效等级，分别使用四个 Param，提示按同一参数求值。 |
| 70 Raise Skeleton | d2decomped 的 Do31 尸体召唤对应原 SrvSt15 `4EF20`／SrvDo31 `518B0`。当前先删尸体，再用冻结坐标尝试 S1 出生、PetMax 和盾组件，CltDo31 `C88A0` 在尸体处呈现。跨职业来源的原圣骑士自伤分支尚未接入。 |
| 71 Dim Vision | d2decomped 的普通怪物 Special10 与原 SrvDo30 的 dimvision 特例一致，按 AiCurseDiv 缩短，AI 在近战、20%近步和待机之间选择。原 CltDo30 不读取 CltMissileC，已撤掉额外施法者飞弹，落点走诅咒中心。 |
| 72 Weaken | d2decomped 使用诅咒属性降低物理攻击，原 Do30 `50661` 读技能范围、持续期与连续 AuraStat 槽。当前 MPQ 是 `damagepercent=-33`，与怪物随机诅咒的独立数值分开。客户端按共用诅咒中心生成表列 `curseweaken`。 |
| 73 Poison Dagger | 原 SrvSt16 `4F7D0` 先建带毒近战记录，SrvDo32 `4F920` 再处理目标耐久，符合 d2decomped 的阶段划分。当前匕首资格、原表毒率／毒长、物品毒与 `AttackNoMana` 均走共用武器路径。 |
| 74 Corpse Explosion | 原 SrvDo55 `4F4D0` 先隐藏尸体，再按基础 HP、尸体种子和等级比例准备一份伤害。d2decomped 的半物理／半火同源，D2X 保留物理截短与元素向上取整的两个半径。客户端与尼拉塞克的技能351共用 CltSt21 `C8330`／CltDo32 `C8CE0`，起手保留尸体坐标及类型，按 MPQ 尺寸和 `A2380` 的方向与散布生成碎块，再生成爆炸及等级2红光。目标音效在释放时定位到尸体，目标已移除则挂到爆炸弹体。客户端随机流保持连续，原始种子的逐次一致性尚未运行核对。 |
| 75 Clay Golem | d2decomped 的主人贡献和减速对应原 SrvDo56 `51750`、Event27 `4EF70`。原表读取 `item_slow=dm34`、支配命中和血魔基础等级协同；当前 S1 出生、共用石魔名册、受近战时减速及数值上限沿各自原入口。 |
| 76 Iron Maiden | d2decomped 的物理近战反伤对应 Event4 `4F090`。当前按诅咒列表来源算 Calc1，玩家及雇佣兵按目标类型选 Calc2／3，使用原 HitClass。原回调的邪恶鲜血石魔反伤后追加治疗仍未接入。 |
| 77 Terror | 原 Do30 用 `4DF80` 专判状态 56，难度缩短持续期，目标切换 Special11。d2decomped 的逃离 30 子格、不能逃离时备选动作和现有 AI 入口一致。客户端表列 `curseterror` 经诅咒中心生成。 |
| 78 Bone Wall | d2decomped 的根段与两条控制飞弹对应原 SrvDo60 `50FE0`，`51211` 先求 Calc2 再除 2，大于 1 才发两侧。当前表 `par34` 取 Param3，Do13 仅跨格尝试生长，成功才减段数；S1 和寿命按各段出生读取原表。 |
| 79 Golem Mastery | d2decomped 的出生时支配取值与原 `4FA18`–`4FA34` 的召唤槽写入一致。当前四种石魔分别引用 `ln12`、`dm34`、`ln56`，客户端支配提示沿同一参数；召唤后不会轮询重写已装载贡献。 |
| 80 Raise Skeletal Mage | 原 Do31 `518B0` 与 d2decomped 同为先删尸体、再出生；现有独立名册和 `SumSkill` 公式给法师安装 338 等级。原 SrvDo149 `B4B10`／CltDo95 `588B0` 用四种元素索引选择飞弹，D2X 权威按本体等级求 raw 伤害，客户端按可见组件选择。 |
| 81 Confuse | d2decomped 的邪恶怪物筛选和目标改写对应原 SrvDo61 `51A90`。持续期按 AiCurseDiv，原状态写入后转中立并登记目标节点，保留原家族 AI。客户端使用共用 CltDo30；完整目标列表与全局调度边界见下文。 |
| 82 Life Tap | d2decomped 的受击者回调与原 Event5 `4E760` 的近战／飞弹两槽一致。D2X 从命中记录的 8.8 物理伤害取 Calc1 治疗实际攻击者，保留原 `steallife` 图层；补上原最大生命必须为正的检查。 |
| 83 Poison Explosion | 原 SrvDo63 `50190` 先置 CorpseNoSelect 再生成一组毒云，d2decomped 的定点发射及现有保留尸体的服务器路径一致。原 CltDo33 `C8710` 先建尸体图，再建等级 2 的绿光，尸体图创建成功才发毒云；客户端已按该顺序执行。 |
| 84 Bone Spear | 原通用飞弹构造 `A28F0` 从施法者整数格向整数目标发射；d2decomped 的直线穿透与 MPQ 的 CollideKill／NextHit 均为空一致。已移除服务端多余起点偏移，客户端仍走表列 CltDo18 骨矛拖尾。 |
| 85 Blood Golem | d2decomped 归在 NecroPet 通用 AI，专用分摊以原 Do56 和 Event23 `4E5F0`／Event26 `4E500` 为准。当前表对邪恶目标按 Drain 和物伤求治疗，先给主人 Param3 份额，再回填石魔与主人；Param5=0。非玩家主人和无主人回调仍待补齐。 |
| 86 Attract | d2decomped 的单目标入口对应原 SrvDo59 `51C60`。当前先核验邪恶目标并转中立，摘旧主从，再登记附近怪物的目标与 AI 重置，最后按原抗性及列表资格安装状态；其他诅咒不能覆盖。客户端沿共用范围图。 |
| 87 Decrepify | d2decomped 的共用诅咒处理对应原 Do30 `505D0`。当前四槽各取 Param5=-50，分别修改移动、物伤、物抗和攻速，速度由原 stat 标志刷新；免疫物抗分支和怪物随机诅咒表独立。客户端读取 `cursedecrepify`。 |
| 88 Bone Prison | d2decomped 的十二段与原 SrvDo62 `51E70` 的两个整数偏移表相符。现有 NU 出生模式、首个成功段链接、UMod15 及 Calc1 生命协同分别沿原规则。客户端显示实际出生单位，技能无单独 CltDo。 |
| 89 Summon Resist | d2decomped 识别召唤抵抗被动，原 D2Common 被动槽写入与当前 `passive_summon_resist=dm12` 一致。出生回调独立安装抗性，火／电／冰分别跳过已有吸收的元素，毒抗照常安装；客户端提示同取 dm12。 |
| 90 Iron Golem | d2decomped 识别材料召唤及反伤属性。原 SrvSt20 `4EDD0` 与 Do57 `51540` 复验地面、金属、鉴定和归属，成功出生后才装备材料，再写基础与被动。当前材料先投递、PetMax=1，荆棘和 Fade 取表；完整材料回调及保存边界见下文。 |
| 91 Lower Resist | d2decomped 的共用诅咒路径与原 Do30、SkillNec 的单位基础抗性检查对应。当前 `-dm56` 只作用于火、电、冰、毒，每一项独立判断免疫并按原五分之一折算。客户端图形和四项抗性提示各走原表。 |
| 92 Poison Nova | d2decomped 的普通环与原 SrvDo22 `43AA0`／CltDo25 `73CB0` 一致，当前是完整 64 向，独立于怪物的 16 向 PrimePoisonNova。原表碰撞即终止且 NextDelay=4，毒率和毒长分开；人物面板以 DescDam5 显示总毒伤。 |
| 93 Bone Spirit | d2decomped 的单位追踪与坐标搜索对应原 SrvDo10 `4D270`／CltDo18 `15C40`。坐标构造的 0x400 只改 CurrentFrame，保留原表 TotalFrames=128 供周期使用；两端修正这一帧值分离，服务端也按整数格瞄准。 |
| 94 Fire Golem | d2decomped 的 Holy Fire、火吸收和死亡爆炸分别对应原 Do56、当前召唤槽及 UMod31 `265E0`。已保留 Holy Fire 上限 30、抗性与吸收分槽，以及四帧后 100 物理＋100 火焰、半径 6 的原伤害回调；独立死亡图形时序仍待核对。 |
| 95 Revive | d2decomped 的等级限制、支配贡献及 Calc2 到期对应原 SrvSt21 `4ED60`／SrvDo58 `51340`。D2X 保留实际 GUID 和家族，按当前尸体等级重投生命，再限制到主人等级；原空位失败仍进入属性分支。CltDo31 在原尸体坐标呈现，完整家族调度仍待运行对照。 |

## 状态、归属与保存

召唤物攻击朝向与其他非玩家单位共用客户端路径：0x6B／0x6C动作包的方向字节在原`MonsterMsg::sub_6FC65C70`普通攻击分支中保持0，不是有效固定朝向；仅DEATH／DEAD取`PATH_GetDirection`，GH同位置字节为生命／触发标志。客户端按该语义保留可用朝向，普通攻击面向原包指定的目标，并使用目标已有的连续显示位置，不能让占位0覆盖它。此修正共用于原版与PD2；两种配置的实机朝向回归仍待核对。

客户端仅0x4C／0x4D切换技能动作；0x99／0x9A保留当前模式、目标、朝向与动作时间，通知仍进入效果事件队列，不打断本人施法预测；无客户端表现的表行不请求动画。

依据为D2MOO `SkillItem::SKILLITEM_CastSkillOnTarget/OnPosition`→`SUnitMsg::sub_6FCC60D0/sub_6FCC6150`的0x99／0x9A触发通知；固定1.13c D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）包表RVA DDE60中，两者直接进入AF0F0／AF090→C9030，临时设置效果目标并在返回前恢复；0x4C／0x4D则由AF1D0／AF160进入动作分派2A750。此修正定位于错误动作覆盖；PD2专项说明见[职业技能：骷髅通知观察边界](../../pd2/CLASS_SKILLS.md#职业技能骷髅通知观察边界)。

actions持有动作与待释放队列，skills解释并推进技能程序，companions拥有伙伴／上限／寿命，monsters拥有实体／AI准备值／生命，effects拥有诅咒与接触反应，transactions提交人物／库存／地面材料，combat持有伤害和来源复验。资源不足、目标消失、尸体已消费、非金属／未鉴定材料、缺原资源均明确拒绝，不自造参数或召唤替身。

骷髅／法师／石魔及重生单位已有路线时，`companions/necroStep`不重选目标或重启追击／跟随，归位和固有光环仍先处理。重生单位的主人跟随使用现有`nativePetMoving`运行标记，路线执行期间不再同时调用原怪物家族AI覆盖它；家族自身的移动仍由`ai::System`推进，跟随结束或归位后交回家族决策。依据为本地D2MOO Fn067_NecroPet、PetMove及MonsterMode的WL／RN结束回调；此处只修正路线继续与两个AI入口的交接，完整Fn067目标／命令和全重生家族仍待对齐，不改变D2S。

骷髅／法师／重生在skills/casting起手、发布动作之前通过monsters.consumableCorpse复验，释放时再次检查：除现有死亡完成、区域、不可选／已搜索及奖励保护外，要求MPQ准备的MonStats原速度非零。重生另检查revive、AI切换、首领／精英和uninterruptable状态，普通随从不因Minion身份被拒绝。尸爆／毒爆使用普通usableCorpse入口，不附加速度条件；铁魔起手要求明确选中已鉴定金属材料。依据D2COMMON_11017_CheckUnitIfConsumeable、D2COMMON_11021及SkillNec的SrvSt15／17／20／21和释放调用。服务端尚未完整映射原DEATH／DEAD模式、附着单位及全部Udead状态掩码，完整尸体资格仍待对齐。

客户端RemoteCombat按当前MPQ的TargetCorpse／SelectProc选择尸体：1／5普通尸体，2要求原速度非零，3附加AI／精英与SandLeaper基类限制，4／6要求soft。尸体必须已进入DEAD（12），读取corpseSel与States的不可选／隐藏／udead条件，不使用活体敌对阵营／killable作为尸体资格。PD2专项说明见[职业技能：召唤尸体选择](../../pd2/CLASS_SKILLS.md#职业技能召唤尸体选择)。直接点中的合格尸体保留为目标；点击空地时，TargetCorpse且SearchEnemyNear启用的技能先按落点搜索附近尸体，找到后把坐标请求转为真实单位请求；只有找不到目标时才在本地预测动作前拒绝。

原版附近搜索有两层距离：UNITFINDS先收集距落点平方距离≤81的候选，随后以UNITS_GetDistanceToCoordinates的`max(abs(dx),abs(dy))+min(abs(dx),abs(dy))/2`整数距离取严格小于10的最近者。它不要求鼠标命中尸体图像，也不以角色坐标为搜索圆心。房间范围从角色当前房间开始，搜索触及边界时遍历原activeNear；同距离不替换先遇到的候选。RemoteTown保留观察到的单位入房次序（头插，房内移动不重排），并按原活动邻房列表提供候选，不用GUID大小选择最近距离的平局。副本更新之间未观察到的跨房往返仍无法重建原客户端房间链时序。

证据为固定1.13c D2Client RVA51C9C–51CC2读取SearchEnemyNear并调用51650，5170E–517D0准备TargetCorpse查询与半径9，517EB–51846以初值10严格比较最近距离；D2Common ordinal10591／10052／10080分别对应RVA12030／12720／7F5E0，默认候选测试位于124E0；D2MOO的SkillsTbls、UNITFINDS_TestUnit、UNITFINDS_FindAllMatchingUnitsInNeighboredRooms及UNITROOM_AddUnitToRoomEx提供字段／遍历依据。PD2专项说明见[职业技能：尸体搜索距离差异](../../pd2/CLASS_SKILLS.md#职业技能尸体搜索距离差异)。

尸体鼠标命中共用[世界选取几何](../world/OBJECTS.md#世界悬停与点击选取)，读取当前DT／DD的COF外层范围／Selectable及原组件帧外扩；最小尺寸保留原整数截断。悬停检查左右两个已选技能，实际施法仍使用当前手。名称使用原Font16红色字形，在可见世界视口顶部基线32居中，追加当前语言TBL的corpse后缀（非英文加括号），不显示生命条或词缀。依据固定1.13c D2Client.dll（MD5 `f5860c629d309b8fc1f96174babcf633`）SelectProc表RVADE838、A6070／A6250、A6730／A6C90及C0850–C0B66；请求距离保留PlrMsg::sub_6FC83C60的每轴50 subtile边界，不把鼠标像素范围当成游戏作用半径。PD2专项说明见[职业技能：尸体选择观察边界](../../pd2/CLASS_SKILLS.md#职业技能尸体选择观察边界)。

公共curseStateDuration现供死灵、亚马逊状态和祭坛使用：先算nativePercentage(期限,抗性,100)，再从原期限中减去；仅当前States.curse启用抗性拒绝，负抗性不钳零，零期限不强行变成一帧。怪物抗性从当前原始属性来源查询，缺原始来源的旧生产端保留显式适配。AI难度除法仍在原调用端先执行。依据D2MOO Skills::sub_6FD10EC0，完整原事件排序仍未闭合。

AI诅咒源码开始区分两类移除：微暗灵视／恐惧／嘲讽的RestoreAi先准备普通AI恢复，再走状态／被动回调；迷乱／吸引的ClearTargets准备敌对阵营恢复、清状态及目标撤出，不刷新被动，也不代替独立AIRESET。迷乱施加后写原始alignment=1，怪物关系查询读取当前原始属性；中立单位按原AI入口排除。复生单位恢复敌对的拒绝条件保留Hydra例外。控制、命中延迟及吸引的附近目标／计时器随同一单位候选复验和发布，不再在人物提交后额外修改控制。AIRESET按抗性修正前长度登记，保留重复注册及−1连续事件，仅清MonsterData特殊目标参数，不清微暗灵视／恐惧／嘲讽的AI函数。阵营／控制变化推进归属修订；诊断公开原始阵营、控制、吸引目标及独立重置事件，均不写D2S。依据SkillNec::sub_6FD0B250／sub_6FD0C2B0、MonsterMode::AiFunction11、MonsterAI::sub_6FC61F00及SUnit::sub_6FCBDD30。属性候选现进一步在每次撤出观察中消费独立回调：先传播撤出的属性，再在RestoreAi／ClearTargets边界准备控制／阵营变化，随后处理状态清除及被动刷新。MonsterResourceTrace读取修改前的回调基线，嵌套来源重入沿同一候选读取；无基线的既有回调历史明确拒绝，避免把旧回调当新事件重放。普通AI诅咒在列表投递后、后续AuraStat写入前切换控制，迷乱／吸引仍在属性写完后执行专用目标程序；普通数值诅咒不再额外统一停步。普通群体诅咒又接入目标中心的原单位查询：从选中单位当前坐标／施法路径点查当前及相邻房间，随后按目标房间的相邻列表和房间单位链顺序交错准备玩家／怪物；单目标回调保留直接选中单位入口，不冒充范围查询。单位事实按实际接收区域发布，移除原先仅遍历施法区域、按EntityId先怪物后玩家的近似。

吸引现独立由nativeAttract准备：先按原前置条件复验选中敌对怪物、写中立阵营，再准备附近敌对怪物的目标参数／独立AIRESET，最后以无主属性的原CurseLevel列表尝试安装吸引。抗性／列表资格拒绝仍保留前置中立变化和附近目标候选；容量／输出拒绝则不发布半笔候选。选中怪物不被额外切换为一套Attract AI函数，后续重施会读取真实中立阵营。依据SkillNec::SrvDo059、Skills::sub_6FD10360及D2Common::DUNGEON_GetRoomAtPosition。吸引又准备运行时OwnerGUIDEx摘除：首领清其随从关系，普通随从摘除自身；真实spawn身份保留。迷乱／吸引登记group 9节点，以实例递增序号保留头插顺序，移除回调摘除节点，AIRESET仍独立清选择器；准备的单位提交顺序不改为ID顺序。AI函数切换另推进参数修订，普通族参数／命令重置，迷乱查询按单位种子投掷临时EVIL／GOOD阵营后恢复真实阵营。微暗灵视采用近战／20%近步／10帧中立分支，恐惧使用原整数30格逃离及当前MPQ运行模式／速度参数，嘲讽重置参数并采用原20格候选查询。当前group 0–7玩家／宠物链、AI备用函数及全局事件调度仍属于通用AI／事件整合阶段，完整家族、路径和事件组合仍待对齐。

诅咒按原CurseLevel覆盖，同等级延长保留来源及分配阶段首项，随后仍按槽写后续非零属性；首项为零时不安装，原连续槽在缺字段处停止。吸引不被其他诅咒覆盖。AI诅咒按原难度AiCurseDiv缩短，排除不能切换AI的对象、首领／精英和附身者；负抗性对原基础免疫通道单独除五。降低抵抗现从公共原公式环境读取AuraStatCalc；固定原1.13c D2Common RVA4DC30／ordinal10115确认先截断等级比值，已消除与D2MOO 1.10f转录的分组差异。骨甲原属性查询从唯一护盾池采样当前余量；护盾写入／耗尽撤出开始使用逐边界资源观察，重新施放使用防御状态组预清理及非零Aura／Passive写入。怪物Weaken与Cursed接相同首项投递／后续槽写入、诅咒抗性及原始来源，被诅咒精英保留1–40半径约束。完整原版事件时序仍待对齐。

宠物使用原7A名册、怪物／状态／弹体投影；客户端不预消费尸体、不判伤害、不改召唤存档。铁魔材料保留在原v96的kf段，存活退出后新局重建，主人／铁魔死亡、取消或被另一种石魔替换时清除。其他召唤、墙段、毒云、诅咒、护盾、AI与重试队列不保存；保存边界见[SAVES](../../modules/SAVES.md)。

完整原服、全等级／装备组合、全怪物重生、多人争用、背压、Linux及逐像素／逐帧表现仍待对齐。

## 依据与入口

NativeSummonStats从主人公式准备五项Passive基础加值、六项Aura非零写入、Calc1生命百分比、PetMax和SumSkill槽；prepareSummonStatSources与亚马逊召唤共用基础、普通／L列MonLvl、初始／继承技能及被动缓存的安装机制。骷髅支配生命先作为raw maxhp基础加值，再执行百分比增长；item_normaldamage、命中／防御及damagepercent保留原基础／列表位置，不把最终标量重建成第二份基础来源。原初始技能槽与难度技能加成保留，SumSkill仅在公式正值时覆盖；法师公式零值不抹掉原初始飞弹技能；完整来源组合仍须运行验收。

四种石魔现也开始从主人原Calc／Passive／Aura／SumSkill进入相同来源入口：黏土的复合生命百分比、命中／速度／防御／增伤及减速，鲜血的生命和基础加值，铁魔的反伤／fade，火魔的火抗／吸收／火伤与Holy Fire赋值，不再只依赖手写参数曲线安装标量。当前支持的MonStats仍为noRatio且MinHP=MaxHP的既有记录，raw生命及上限回调共用公共算子；原随机流、全部过程参数和更新时点未认证。

当前源码将普通骷髅／法师／石魔分开使用游戏、单位和施法者随机流：实际单位种子从游戏流分配，单位流先准备区域组件池／组件，再保留当前等生命范围的单选投掷；法师元素与三级以上骷髅的盾牌机会使用施法者流，费用提交时复验并发布。普通召唤、骨墙根和骨牢复用怪物出生搜索的原位置−1／半径4回退、房间边界与辅助碰撞；骨牢沿十二偏移逐段搜索，后续搜索采样本批已有段的原生碰撞占位，多房间随机贷款同笔复验。属性刷新复用现有Actor的已掷MonProp列表，不重新创建出生候选或消费组件／生命随机。

骷髅外观共用原十一行组件覆盖表，盾牌、右手和法师S4／S5分开覆盖。原枚举STATE_ITEMFULLSET在当前MPQ实际名为skel_mastery（97），不是fullsetgeneric；出生从施法者实际效果或仍启用的骷髅支配被动列表读取modifierlist_level，缺列表为1，最高10，负值明确拒绝。此处不以穿齐套装推算外观等级。异步召唤准备携带唯一票据、实际施法等级及有效／硬技能快照，重备同种子不能接收旧返回。零售Fn067由nativeNecroStep执行；法师出生参数为1／0／−666，实际AI函数切换推进修订并清参数；普通召唤与法师分别沿原选敌、近战／施法／绕行及PetMove分支，不再共用最近敌人／直接距离追赶。

零售PetMax由attributes/skill_refresh的同步回调与effects::stagePetLimit维护：Skills::sub_6FD108D0按PetType编译反链（最多15项）读取各技能的原最高实例，优先无归属实例，再比较充能等级；PlayerPets::sub_6FC7DF40按实际技能登记链逐项执行增长前缀，没有实例才回落basemax。单技能／非本职技能写回及充能移除、学习与原状态被动刷新分别沿各自原入口，不给每次普通+allskills被动刷新增加全表更新。PlayerResourceTrace把中间采样的上限回调顺序带入人物／库存事务；先发生的超额移除不因后续上限增大回滚。退役按petRegistrationOrder选最早节点，包括死但仍登记的节点，保留原死亡回调；SINGLE与佣兵例外沿原函数。出生替换也按同一名册预留新节点数量，不依赖尸体EntityId先后；后续最后充能移除回调包含该笔新宠，死亡后不重新安装出生／物品光环。

Fn067依据AiThink::AITHINK_Fn067_NecroPet／sub_6FCE3740、AiUtil::sub_6FCF2110／2CC0／14D0及AiTactics。近战分支保留50／28主人距离、24／6本体选敌与36主人后备，法师保留15／20、80%技能及后续75%绕行选择；沿原bCombat、单位随机、PETIGNORE后备过滤及三射线资格。PetMove复用原逻辑区／路径／探针，探针class及碰撞从当前MPQ的necroskeleton准备；Fn067的7／8间距按真实登记宠物数。待受理施法保存原决定，UseSkill失败NU10而普通A1模式失败不补时钟。失明／恐惧／嘲讽仍进入共享特殊AI，迷乱和吸引留在原选择器路径。零售火魔的继承光环从出生时指定右手及准备周期，不在Fn067轮询补放。

零售SrvDo031按D2MOO `SkillNec::SKILLS_SrvDo031_RaiseSkeleton_Mage`，先记录实际尸体坐标并删除Actor、房间链／碰撞、效果及时钟，再尝试出生。`CorpseSummonExecution`由本次技能释放或装备触发持有，内容等待／输出背压及内部重备使用同一已删除前缀和冻结坐标；不因目标已撤出取消回调，不恢复失败召唤的尸体，也不再次删除、扣费或提交单位。当前费用与身份仍由实际出生／人物事务阶段提交；失败空位保留原房间搜索的随机消费。PD2保持其原有尸体消费路径，本段不外推核心规则；源码已通过Windows Release构建，未运行验收。

钢铁石魔按SkillNec::SrvDo057先继承材料，再SetSummonBaseStats／SetSummonPassiveStats／SetSummonResistance。材料从native_sources保留原始物品／孔内／参数层贡献，以真实材料ID进入候选；materialBeforeStats使其在等级与MonLvl加值、召唤Passive／Aura及生命增长前投递，装备授予技能同时准备。已有类型化武器／过程参数仍保留；材料的物品光环与逐写充能回调已接共享来源候选；完整物品事件、技能实例归属、装备有效性与存档投影仍需继续核对。PD2既有刷新将准备结果的材料来源转交候选；零售不再轮询刷新出生贡献。完整材料／周期／保存组合保留运行验收。

召唤抵抗单独投递来源，火／电／冰各自检查当前吸收百分比，正值时跳过该元素，毒抗始终写入。新增来源、基础AddStat和每项写入通过MonsterResourceTrace刷新快照及生命上限回调。零售普通召唤的主人贡献在出生时安装，已有单位不因主人等级／装备／学习变化重新准备出生属性。PD2原有属性刷新仍绑定实际ID、保留当前模式来源；其等价边界归PD2专题。

复生现在按当前尸体原属性等级重新读取独立MonsterLifeTable：hosting从当前MPQ准备MonStats三难度MinHP／MaxHP、noRatio及MonLvl普通／L-HP列，gameplay复用DATATBLS_ApplyRatio整数算子并按原上界钳制等级。prepareReviveStatSources保留原单位ID、属性来源与基础技能，使用本单位随机流重新投掷生命并左移8位，先Set MAXHP再Set HP；等级高于主人时，按raw生命执行原百分比计算，最低1 raw，再Set MAXHP／HP／LEVEL。随后与普通召唤共用Passive基础Add、独立Aura写入、Calc1生命增长和继承技能；不调用普通召唤的MonLvl护甲／命中加值或召唤抵抗。主人等级／技能变化不重写既有复生生命；PetMax走下述同步名册回调。Calc2正值才附加原UMod21与MONUMOD；不可打断时再等3帧，正常到期从自身名册节点移除并走死亡回调，已提交死亡的背压继续点保留。

固定原1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`）SrvDo表RVA1074A8第58项为RVA51340；RVA513F2–5143B确认游戏类型布尔选择、当前等级及HP表调用，5143F–514C4确认本单位投掷、shift8、MAXHP→HP、raw等级比例、最低1及LEVEL写入顺序，514DC调用公共召唤贡献。零售Do58已按D2MOO同名入口补原落点搜索：找到空位才切中立、清死亡模式、恢复目标标志、摘除旧主从、清PeriodicSkills／右技能并迁移同一GUID；空位失败仍继续原属性／归属登记分支。新主人同时写Ex关系与主牵引，首次AITHINK为+15；保留家族AI，主牵引按AICORE_MinionLeash执行≤1逃离19／>20以path7和40搜索界跟随，失去主牵引不抹掉Ex主人。未新增Revive自行远距传送。

骨墙／骨牢开始使用同一原始召唤属性入口：当前MPQ的bonewall为noRatio，三难度生命范围相等，保留MonStats等级7／40／73及原防御、抗性、DamageRegen和raw初始生命。主人Calc1及Passive／Aura／SumSkill从公共公式环境采样，原SrvDo060／062只调用公共召唤贡献，因此setBaseStats关闭额外的主人等级／MonLvl护甲／命中安装；不再先用整点生命百分比生成另一份基础。骨墙Calc2控制每侧段数。独立BoneGrowth计时器已撤出：根段和两条真实控制飞弹一起预备／发布，Do13按前次PATH跨格尝试原位置出生、成功才减TargetY；后续墙段重新采样当前玩家贡献，从各自出生帧安装MonStats.Skill1所指Param2寿命。出生模式读取Skills.summode，当前骨墙为S1、骨牢为NU；monsters::System::prepareNecro（controlled.cpp）同时写入Actor.riseMode及aiPose，出生计时期间不沿用通用S2默认值；原sub_6FC40280(...,9)选择目标链节点，不是S2模式。AIGENERAL所有者／子列表与实际玩家归属分开，骨牢链接首段／其余段；UMod15死亡候选按原清除子所有者／子列表、清除实际怪物主人AI所有者及死亡模式的顺序处理。DD保留真实Actor，未对死亡根GUID作提前撤销。非玩家公共召唤生产、完整出生随机顺序、尸体／房间退休与全局调度仍未闭合。

骨甲客户端由States.setfunc=6驱动，使用当前状态列表的raw bonearmor／bonearmormax，以严格小于max/3及2*(max/3)判断一至三片骨骼。数量增加时清旧层，安装castoverlay及overlay3／4隐形延迟；浮现结束后才生成overlay1／2前后循环层，数量降低则从最新生成的循环层逐对撤出，保留其余相位和仍待执行的延迟／浮现。状态撤出、完整状态重分配与单位重新分配清理表现运行态；不向权威属性或D2S写入原客户端scratch shortparam1。入口为remote_scene_effects.cpp::boneArmorVisual和remote_scene_draw.cpp，原版与PD2共用当前MPQ的骨甲状态定义。多次属性变化被客户端更新合并时，尚未逐条重放中间吸收量回调。

依据为固定原1.13c D2Client.dll（SHA-256 `dd8bc6025de921216a97c17f97cd1a50fbb85926e838ec60e13451448836d906`）setfunc表RVA E2678槽6→73230、清层72A50／727F0及Overlay构造6B300、更新6C130。构造器将AnimRate左移4，更新以25Hz推进8位定点帧；SceneAssets::ensureOverlay及world_renderer据此转换，不把AnimRate直接当fps。真实DCC决定长度；原MPQ的BoneShieldEmerge为12帧、Front／Back各24帧、null为2帧，当前表AnimRate分别为16／16／16／4／2，两种延迟为8／16个游戏帧。原更新在替换节点后重扫列表，新建浮现／循环层在同一帧继续推进，前后遮挡仍读PreDraw；未进行逐像素实机对照。

骨墙外观仍使用原BW的TR／S1组件及当前MonStats2方向数。固定上述D2Client的RVA 4CEC0中，骨墙分支4CF2A从本机玩家表现种子按原6AC690C5乘加进位算法取低六位，初始化墙段的64向PATH朝向；当前原表对应四种资源方向。remote_scene_movement.cpp在每次真实墙段分配时执行一次同类初始化，避免所有静止墙段落在默认朝向。表现随机流独立于服务端战斗及PD2 SHRD路径流，新局清理；网络未提供原客户端完整种子与其他表现消费者的消耗历史，当前以本机玩家GUID初始化，不能保证与原客户端同一次建墙逐段选择相同方向。骨墙出生／死亡的完整逐帧位置与图层仍待实机对照。

骨墙／骨牢各成功单位分别从游戏流取单位种子，先组件再HP，不再把已分配单位种子二次派生；首个AI事件按公共召唤25帧准备。骨墙根与控制飞弹串接同一游戏流候选，飞弹发布后不再被根段的较早终值覆盖；普通召唤的游戏流也随人物事务复验并发布，不在事务之后无条件回写。后续生长按BoneWallMaker的0xD标志只尝试原位置、不扩大搜索，失败仍提交搜索实际消耗的房间随机；当前MPQ的bonewall TotalPieces=2，无区域组件池。这些是源码修正，完整初始AI／模式回调、出生与飞弹全局交错及失败分配副作用仍未认证。

法师飞弹规则携带当前MPQ的MissileLevelDamage曲线及其实际技能身份。spawnMonster在发射候选中读取本单位当前基础／有效技能等级，从原Missiles曲线以整数计算元素范围、MaxDamage上限和元素长度；宿主不再将float伤害／秒数乘回原值作为权威输入。该路径修正初始技能／难度加成与后续等级变化的采样，完整SrvDo149、Missile Do／Hit／Dmg、随机流、组件选择及同帧流程仍待后续程序／调度阶段闭合。

MonStats数值准备必须保留原表合法空白：bonewall第344行三个难度的ToBlock为空，necroskeleton第363行三个难度的Level为空；当前MPQ对应MonStats.bin记录中的值均为0。D2MOO MonsterTbls分别绑定BYTE偏移163–165及WORD偏移170／172／174，当前BIN记录大小424字节。bone_data和necromancer_data对此采用BlankIsZero，仍拒绝缺列和非法数字，不修改MPQ。

当前MPQ的Skills／Missiles／MonStats／MonStats2／MonLvl／PetType／States／ItemStatCost／DifficultyLevels和AnimData负责规则与原图；本地D2MOO SkillNec、AiUtil／AiThink、PlayerPets、SUnitDmg及MissMode提供执行证据。Fire Golem死亡的固定100物理＋100火焰、六subtile范围及四帧延迟，另由固定1.13c D2Game.dll UMod31回调RVA265E0／21A90确认；同一伤害结构通过真实monstercorpseexplode飞弹结算，来源不再受主人同区／存活限制。来源职责见[资料来源](../../resources/THIRD_PARTY.md)，通用规范见[COMMON](COMMON.md)。

本节新增召唤失败顺序、Fn067、名册回调及Revive初始化／到期源码已通过Windows Release构建，未运行验收；待验收场景集中维护在[原版缺口](../../development/ORIGINAL_GAPS.md#待验收死灵召唤)。
