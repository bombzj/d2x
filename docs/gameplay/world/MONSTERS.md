# 怪物联机表现

所连接的服务端负责出生、AI、攻击／技能、命中、复活、召唤、经验／掉落和任务。当前客户端只消费真实单位与原动作／技能／状态，旧按58项本地AI和内部存档版本排列的完成记录已退场。

## 第一幕原身份与表现家族

当前MPQ第一幕mon／nmon／umon池有57个身份；字段／资源逐类核对不等于全部随机出生或战斗分支认证。

| 家族／身份 | 当前消费 |
| --- | --- |
| fallen1–4、zombie1–3、skeleton1–3、fetish1 | 原行走、攻击、受伤、死亡；复活等原活模式确认，沉沦魔／僵尸有有限单人击杀及反击证据 |
| fallenshaman1–4／Bishibosh | 原MonSeq复活／ShamanFire动作及释放；不本地选择或恢复尸体 |
| foulcrow1–2、crownest1–2 | 原飞行／Nest序列；仅显示原服新增单位 |
| brute、goatman、corruptrogue、cr_lancer、hellbovine | 真实组件、普通近战与原服生命／动作，不运行家族AI |
| quillrat1–4 | MissA2原尖刺、A2释放及已核实额外射线表现参数 |
| cr_archer1–4、sk_archer1–3 | 原MissA1箭与释放帧，客户端不判命中 |
| bighead、骷髅法师／Boneash | 原火／闪电／毒弹及爆炸图，状态与伤害等原服 |
| wraith1–2、arach1 | 原移动／普通动作、SpiderLay状态及既有叠层 |
| vampire5、Countess | 原火球／ClientSend火墙与陨石程序，完整随机起帧／散布／落轨未认证 |
| Blood Raven、Smith、Griswold及固定金怪 | 原真实身份、序列、原服新增召唤和动作；0xAC固定hcIdx名称／三难度Utrans |
| Andariel | 原喷毒序列／释放及原弹体；专用死亡演出未完整接入 |

自研服务端对应普通57身份／19类AI的当前执行、MPQ覆盖与未实现边界集中见[怪物模块](../../modules/MONSTERS.md#自研服务端第一幕普通怪物)。本表描述客户端原包消费，不能据此把精英／首领或全部行为分支视为已完成。

## 原图、声音与尸体

MonStats→MonStats2组件／尺寸／Light、Skills／Missiles／MonSeq、AnimData、原COF／DCC及变换来自当前MPQ；ActorAnimation统一资源与动作，SoundCatalog／SceneAudio唯一解释并消费MonSounds。

0x0C先剥离暗金标志再读原生命刻度；原动作12／13映射SKILL1，真正序列消费技能通知。原freeze可停止表现路径／动作；hide、udead与corpseSel分别控制尸体显示／选择。弹体出生与ClientSend按原程序决定，动作派生与原0x73同步去重。

0x69动作9尸体当前位置及死亡停止行走修正已构建入包，具体死亡边界未完整运行，见[联网源码修正](../../modules/NETWORK.md#本批源码修正)。原资源不足时明确不可用；唯一敌对外观替身保留真实身份，中立单位不可替换。

完整随机精英名称／染色、状态透明／速度、空间音频、特殊死亡和全部客户端程序仍有限。模块文件归属见[怪物模块](../../modules/MONSTERS.md)，有限原服观察见[联网记录](../../modules/NETWORK.md#既有有限证据)；本地D2MOO MonsterMsg／MonsterMode／SkillMonst／SCmd提供原语义，不能替代完整D2Client证据。
