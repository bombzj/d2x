# 资料片佣兵

佣兵资格、费用、归属、同行、战斗与保存由所连接的服务端决定。第一幕罗格源码已接卡夏名单／雇佣替换、复活、头盔／护甲／弓装备、腰带／光标喂药和经验成长，并复用血乌奖励的真实伙伴运行态。客户端沿原包显示，不恢复旧本地执行器。当前验证与包状态见[基线](../../../BASELINE.md)，有限冒烟及阻塞见下方；入口存在不等于全量原版认证。技能分类见[同行者能力](../skills/COMPANIONS.md)。

## 任务奖励与运行态

[第一幕任务](../quests/ACT1.md)负责一次性奖励资格；hosting从当前Hireling表准备原Kashya奖励，已有佣兵（包括死亡记录）不被替换。异步准备复验人物身份、佣兵sourceRow与等级，缺原身份、动画或技能参数明确暂缓。

[hireling_content.cpp](../../../src/hosting/hireling_content.cpp)目前只准备第一幕罗格，读取Hireling、MonStats／MonStats2、Skills、Missiles与AnimData等原数据，使用原成长、技能权重及Fn061同行／目标选择规则。[companions/hireling.cpp](../../../src/server/systems/companions/hireling.cpp)管理规则准备与归属、出生、同行／远距归位、射击及Inner Sight；伤害、弹体和死亡复用monsters／effects与事务。伙伴为真实owned monster，不使用敌对替身，不由客户端补造单位或技能。

第三幕基德宾的铁狼与第五幕囚犯救援的野蛮人奖励已由hosting/quest_content准备原Hireling身份并保存，保留已有佣兵；具体资格及证据见[第三至第五幕](../quests/ACT3_5.md)。这两类的完整活动实体／AI尚未迁入，不能把身份领奖／保存当作同行战斗支持。

个人佣兵身份与死亡记录通过PlayerStore导出，原0x7A等消息投影归属。罗格奖励／同行／新进程恢复证据见[第一幕任务](../quests/ACT1.md#有限运行证据)，铁狼身份奖励／恢复与保留已有佣兵证据见ACT3–5，不据此认证全部AI分支或完整佣兵系统。

## 第一幕服务

| 入口 | 当前行为 |
| --- | --- |
| 卡夏名单 | 原38/action3请求，4F清空及4E姓名TBL索引／seed；双方共用resolveHirelingOffer纯求值，数据来自当前MPQ。宿主候选绑定玩家、NPC、交谈revision、等级和种子；准备重试不重新掷值 |
| 雇佣／替换 | 原36请求姓名索引；至少8级或已取得血乌奖励，验证当前候选，先钱包后仓库扣款。按旧实现／SUnitNpc替换原佣兵并移除旧佣兵装备，人物和库存同笔事务；不会把旧装备交给新佣兵 |
| 复活 | 原62，卡夏有效交谈且已有死亡罗格；费用min(50000,15*level*level/2)，恢复装备后的最大生命；保留seed／经验／装备，移除旧死亡实体再出生。未准备规则不扣款 |
| 装备 | 原61在空光标时取下指定槽，有光标时由真实物品类型选择槽；罗格头盔／护甲／弓、已鉴定非损坏非任务物品。排除被替换装备后复验需求，inventory事务成功才移动，换装不补满生命 |
| 药水 | 原26/useOnMerc=1腰带喂药；背包先取光标再原61。治疗采用非玩家倍率及固定点合并恢复，回复按最大生命比例；解毒／解冻及有时限药水状态独立于玩家。成功消耗后安装效果，死亡清理，临时恢复不存盘 |
| 成长 | 沿怪物按佣兵等级准备的经验与planHirelingExperience：每次最多下一等级差的1/64，非本人击杀86/256，佣兵不得超过主人等级。奖励记录冻结计划并去重；升级重备同类型Hireling等级段，不重造装备 |
| 原副本 | 81本人身份，9B死亡报价，9E/9F/A0绝对属性及A1/A2增量；面板只读原属性，不从主人属性推算。7A归属独立于视野；9D完整装备只发主人，其他人仅外观 |

旧master的gameplay/npc/hireling_services与client/local_hireling_view提供迁入起点；当前实现位置为companions/hireling、hireling_equipment、monsters/controlled及RemoteUiClients。旧会话状态和本地服务调用未移入客户端。普通列表与完整特殊装备效果不是同一完成度；当前名单按玩家交谈准备，未复刻原多人共用NPC候选耗尽／刷新竞争。

## 主人死亡时序

D2MOO PlrModes::PLRMODE_StartID_Death开始人物DT，PLRMODE_StartXY_Dead才调用PlayerPets::D2GAME_KillPlayerPets。当前death以MPQ角色死亡帧换算的ready时间触发companions::ownerDied，不再由伙伴另起计时器；佣兵提交死亡记录并进入自身死亡动画，召唤物进入死亡并按自身动画移除。人物回城复活和死亡保存都等待这一事务完成，背压可延后、不可提前。自身受伤死亡或寿命到期不受主人时序保护。

佣兵身份、等级、经验、死亡位与装备仍写原D2S v96；规则指纹更新为`d2x-character-admission-v28/native-wire113c/d2s96/act1-hireling`，无私有段、无旧档静默迁移。当前生命、恢复队列、装备缓存、AI和名单不写盘。

## 当前包有限冒烟

2026-10-09至10-10使用基线所列Windows Debug客户端包（EXE `9A806F06906F31B28C5C4C1531CD24C6C9DF9A0292596295CC45C13D3773AEB4`），未重新构建／打包或修改玩法代码。仅使用已有调试管道、原协议操作和UI输入；新女巫存档独立保存在忽略目录`artifacts/hireling-smoke-20261009`，其中保留JSON、截图、存活／死亡D2S及三个进程的日志。管理经验、金币、物品、换区、伤害与击杀仅准备条件，不认证正常通关。未新增测试脚本、用例或程序。

| 路径 | 实际观察 |
| --- | --- |
| 名单／雇佣／替换 | 10级主人经卡夏Hire菜单取得名单，雇佣4级Abhaya（merc04）后生成owned roguehire；随后替换为1级Amplisa（merc02），旧活动实体移除、金币扣除150、新身份与归属正确。未验证带装备替换，因为换装入口被阻塞 |
| 面板／同行 | O键显示原佣兵面板；管理旅行至鲜血荒地后同一佣兵GUID同行，返回营地仍保持身份。远距离走跑／拥挤分支未专项认证 |
| 腰带喂药 | 原useOnMerc请求消费hp1，受伤Abhaya生命约29.2→54，Amplisa约20→40；主人资源未由这次喂药恢复。未认证全部药水／状态合并与时限 |
| 主人死亡联动 | 暂停权威后致死主人，开始tick14776与推进1帧时佣兵仍为54生命；再推进100帧后佣兵死亡记录为0。认证延迟发生，不宣称逐帧边界精确比对 |
| 复活 | 卡夏报价120；零金币点击不复活、不改变死亡身份，授予10000金币后扣至9880、同名4级佣兵以54／54重生。死亡档新进程恢复后1级报价7，复活保留种子及215经验 |
| 经验／保存恢复 | 管理击杀真实zombie1后佣兵经验210→215，写入原D2S字段。存活档新进程恢复为同名同级满生命；死亡档的flags=65536，恢复后无活动佣兵并提供复活。seed=3912530501、type=1、name偏移1与经验在恢复／复活后保留。未认证自然击杀奖励、升级、等级段切换与装备保存 |

本包仍有以下阻塞，不能把佣兵功能整体标为通过：

- 换装／光标喂药：Cursor上的cap点击佣兵头槽、Cursor上的hp1点击头像，都被`RemoteInventory::submit`的`action >= TradeOpen`检查当作NPC服务拒绝，截图显示“NPC service requires an active server conversation and a compatible cursor”；物品未消费／未装备。`HirelingEquipment`枚举排在交易动作之后，该范围判断先于专用分支。背包直接拖向头槽另被`RemoteUiClients`只允许Cursor／佣兵装备来源的预检拒绝。护甲、弓、卸装、装备贡献及带装备复活／保存因此未认证。
- 自然射击：4级罗格对zombie1及1级罗格对近处zombie1均出现重复攻击事实（RogueMissile336，4级另有Fire Arrow7），未取得命中／击杀证据。第二个样本将目标准备至1生命后推进250帧，目标仍为1；不能用攻击动画或管理击杀代替射击伤害认证，原因尚未定位。
- 下一等级经验：4级面板显示0。当前投影跨同class的所有Hireling定义比较阈值，无法一致时保留默认0；应视为显示缺口，不是下一等级无需经验。

末次宿主failures=0、ignoredPackets=0，mapErrors／effectLimitations为空，三个进程stderr均为空；这些诊断不抵消上述功能阻塞。本轮仅自研Single Player、普通难度、第一幕代表路径；未运行原服、多人、其他幕或Linux，不扩展既有血乌奖励证据。

## 原数据

当前MPQ的Hireling、MonStats／MonStats2、Skills、Missiles、MonSeq、States和AnimData提供真实身份、难度分段、成长、技能权重、原图及动画。content/npc/hireling_data负责表适配；第一幕罗格、第二幕沙漠守卫、第三幕铁狼和第五幕野蛮人按原表区分，第四幕没有独立佣兵类型。读取所有类型的表不等于已经实现所有类型的运行态。

原D2S保留类型、名字偏移、种子、经验、死亡位和装备编码；没有当前生命字段，不补写私有字段。入局重新准备运行生命与规则，临时AI、位置、时钟不写盘。保存边界见[存档](../../modules/SAVES.md)。

## 核对入口与缺口

本地D2MOO：AiThink::Fn061_Hireable、MonsterAI::MONSTERAI_UpdateMercStatsAndSkills、SUnitNpc雇佣／复活／治疗、PlayerPets同行与死亡，以及PlrMsg原0x61装备处理。参考用于核对共享规则、服务端执行和原字段，不能恢复客户端执行器；许可见[资料来源](../../resources/THIRD_PARTY.md)。

未完成／未认证：其余幕佣兵执行、完整装备触发／吸血／压碎／撕裂等战斗规则、所有IAS／技能加成组合、多人共享候选竞争、原服与全部难度／装备逐项对照。A0在旧D2MOO经验增量分支中的疑点不据重建代码猜测修改，客户端按现有绝对值解释；本宿主只发送绝对A0。当前仅声明以上第一幕路径，不宣称完整佣兵兼容。
