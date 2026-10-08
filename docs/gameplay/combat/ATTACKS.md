# 攻击与弹体表现

普通攻击和技能由RemoteCombat提交原请求，结果由连接的权威服务端执行；原服为D2GS，自研为server领域。客户端显示原动作／释放帧、弹体和状态，不扣数量／资源，不判命中或结算伤害。旧本地攻击／飞弹执行器已删除。

## 共用表现输入

Skills、Missiles、AnimData、COF／DCC、ItemTypes和当前装备外观提供动作、速度、图形、层序及武器弹体映射。玩家／怪物原动作和技能通知进入RemoteScene，再由公共ActorAnimation、SceneAssets和弹体绘制消费；本人原服省略通知时以已发送请求衔接显示，强制同步去重。

手部组件核对D2MOO INVENTORY_GetCompositItem：1hs／1ht／ht1按手位选择，其余保持原component。当前MPQ短弓sbw为LH，女巫SOA1BOW的COF包含LH；不能把主手图强塞RH。方向与偏移使用原DCC／DC6和已有等角映射。

原0x73没有弹体GUID；首路径目标不是出生点。ClientSend决定原通知资格；普通箭／投掷及部分地面程序由原Clt动作创建，0x73同步按原程序与来源去重，不能用这个标志禁止已核实的本地创建。共享Grid、地形／对象阻弹和单位相交只决定画面接触，不能当作原服命中证明。

## 当前覆盖与限制

部分25Hz直线／加速飞行、充能弹、整数扇形、闪电／骨矛拖尾及冰封球已接。当前野外物件批补CltDo25的64方向新星（包含Trap Nova／Nova／Frost Nova）、CltDo51喷火雕像及ClientSend毒云CltDo4烟团；当前MPQ决定图形与参数，原1.13c静态入口核对布局／节拍，范围见[物件](../world/OBJECTS.md#野外箱子破坏物与陷阱)。程序编号、数据消费与限制集中见[联网表现](../../modules/NETWORK.md#联机游玩表现与输入)。普通弓弩／Throw请求不等于全部武器效果验收。

女巫批已补Inferno、连锁、暴风雪及地面程序；亚马逊批补原CltDo18–22的导引／充能／炮轰／Fend／闪电攻击、MissileCltDo3／4／7的尾迹／毒烟／引导，以及CltHit12／14／25的牺牲地面火／冻结碎片／闪电之怒分裂。公共sequence、weaponVolley、环形／圆盘／burst与GUID继任保留Clt／Srv参数差异；本人IAS及Pierce只用已知装备／属性。原依据与逐项边界见[亚马逊](../skills/AMAZON.md)。本批已构建打包，列出的路径有有限自研冒烟和原服回归；范围见基线。

通用Attack／弓弩、Throw／Left Hand Throw、Left Hand Swing及六种药瓶已迁入统一武器管线；投掷药瓶由原CltDo2创建，地面寿命计算两端共用。逐项规则与未运行边界见[通用十项](../skills/GENERAL.md)。

骨魂、药瓶抛物线高度和其余客户端程序仍有缺口；RandStart指南存在冲突，暂缓。全效果密度／随机分布、空间音频、精确定点路径及像素一致性没有完整认证，不画自造替代效果。

原规则入口：本地D2MOO PlrMsg／PlrModes、Missiles／MissMode、PathMisc、D2Skills；Diablerie Missile／MissileFunctions只交叉核对显示结构。当前MPQ始终决定原图与参数，参考服务端函数不代表完整D2Client视觉实现。
