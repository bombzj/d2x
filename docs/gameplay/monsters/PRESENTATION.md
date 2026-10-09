# 怪物原协议与客户端表现

更新：2026-10-09。客户端专题负责原包、动作、原图、声音与视觉限制；权威程序及覆盖范围见[怪物目录](README.md)。

所连接的服务端负责出生、AI、攻击／技能、命中、复活、召唤、经验／掉落和任务。当前客户端只消费真实单位与原动作／技能／状态。

## 已接表现程序与边界

| 表现族 | 当前消费／限制 |
| --- | --- |
| 普通动作／组件／生命 | 原行走、攻击、受伤、死亡与真实组件；不运行家族AI，GH／BL由命中事实驱动，快照不重复启动 |
| 序列／复活／出生 | 原MonSeq复活、ShamanFire、Nest／飞行及首领技能释放；只显示服务端实际新增或恢复的单位，不本地选尸体／补出生 |
| 武器／怪物弹体 | MissA1／A2箭、针刺及额外SEIS射线、元素弹与爆炸图；ClientSend与原Clt程序分别处理，客户端不判命中 |
| 地面／状态程序 | SpiderLay及已有叠层、火球／火墙／陨石原程序；完整随机起帧／散布／落轨未认证，显示程序存在不表示AI实际会使用该槽 |
| 首领专用动作 | 血乌、安达利尔等真实序列／喷毒与技能；专用死亡演出未完整接入，执行范围见[BOSSES](BOSSES.md) |
| 精英回调 | 原AC身份与电／冰强化回调；随机精英染色／完整名称及全部词缀演出尚未认证 |

实际类型与家族不在此重复登记，见[第一幕](ACT1.md)与[五幕目录](README.md)。本表仅说明客户端程序，有限冒烟只覆盖列出路径，不能据此认证全部怪物、随机战斗分支或原服行为。

## 原包与公开信息

hosting沿SCmd／MonsterMsg编码原AC阶级、superunique hcIdx、词缀／nameSeed、67／69／6C／6D和4C／4D技能；AC包含五位rank旗标。A1=10、A2=16、S1=13、S2=15、GH=6、BL=18按wire动作表解释，不直接套内部模式号。人物MonProp击退用原0F/action20（KB），不能当隐藏Kick。

人物命中／生命修正为PlrMsg的0D/action19及整数生命0..100比例；怪物0C为旗标19、整数生命0..128比例且大于1时减一及原命中类型。两类不能互换，0D／19也不是GH动作。原编码依据见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)，协议目录见[服务端协议](../../modules/SERVER_PROTOCOL.md)。

晚入视野恢复当前模式、生命、阶级与已公开状态，不重播历史攻击／弹体；换区或移除清对应显示缓存。原服／自研共用消费者，不为自研宿主多发私有或强制本人消息。

## 原图、声音与尸体

MonStats→MonStats2组件／尺寸／Light、Skills／Missiles／MonSeq、AnimData、原COF／DCC及变换来自当前MPQ；ActorAnimation统一资源与动作，SoundCatalog／SceneAudio唯一解释并消费MonSounds。

0x0C与69 GH先剥离电强化触发位再读生命刻度；原动作12／13映射SKILL1，真正序列消费技能通知。原freeze可停止表现路径／动作；hide、udead与corpseSel分别控制尸体显示／选择。弹体出生与ClientSend按原程序决定，动作派生与原0x73同步去重。电强化由GH帧2／公开触发位发八条充能路径，冰强化由死亡帧4发64方向新星；RemoteMonsterEffects只持显示时钟，两端充能方向共用monsterLightningRays。

0x69动作9按尸体当前位置校正，GH动作6的方向槽按生命字节解释并清除旧路径；0C／69生命高位是电强化公开发射资格，不能当阶级。server/effects持MONUMOD延时／十帧冷却，RemoteMonsterEffects只持显示时钟。当前194／195速度／寿命无等级增量，不猜隐藏等级；不同MPQ启用增量时明确不可用，ClientSend为空不强发73。

原函数依据见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)，有限运行见[第一幕证据](ACT1.md#有限运行证据)。原资源不足时明确不可用；已授权敌对替身保留真实身份，中立单位不可替换；实际宿主适用条件见[COMMON](COMMON.md)。

完整随机精英名称／染色、状态透明／速度、空间音频、特殊死亡和全部客户端程序仍有限。模块文件归属见[怪物模块](../../modules/MONSTERS.md)，第一幕有限原服样本见[第一幕证据](ACT1.md#有限运行证据)，其他有限原服观察见[联网记录](../../modules/NETWORK.md#既有有限证据)；本地D2MOO MonsterMsg／MonsterMode／SkillMonst／SCmd提供原语义，不能替代完整D2Client证据。
