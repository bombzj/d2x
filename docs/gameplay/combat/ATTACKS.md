# 攻击与弹体表现

普通攻击和技能由RemoteCombat提交原请求，结果由D2GS执行。客户端显示原动作／释放帧、弹体和状态，不扣数量／资源，不判命中或结算伤害。旧本地攻击／飞弹执行器已删除。

## 共用表现输入

Skills、Missiles、AnimData、COF／DCC、ItemTypes和当前装备外观提供动作、速度、图形、层序及武器弹体映射。玩家／怪物原动作和技能通知进入RemoteScene，再由公共ActorAnimation、SceneAssets和弹体绘制消费；本人原服省略通知时以已发送请求衔接显示，强制同步去重。

手部组件核对D2MOO INVENTORY_GetCompositItem：1hs／1ht／ht1按手位选择，其余保持原component。当前MPQ短弓sbw为LH，女巫SOA1BOW的COF包含LH；不能把主手图强塞RH。方向与偏移使用原DCC／DC6和已有等角映射。

原0x73没有弹体GUID；首路径目标不是出生点。ClientSend只从该通知创建显示实例，原技能不重复补造。共享Grid、地形／对象阻弹和单位相交只决定画面接触，不能当作原服命中证明。

## 当前覆盖与限制

部分25Hz直线／加速飞行、充能弹、整数扇形、闪电／骨矛拖尾及冰封球已接。当前野外物件批补CltDo25的64方向新星（包含Trap Nova／Nova／Frost Nova）、CltDo51喷火雕像及ClientSend毒云CltDo4烟团；当前MPQ决定图形与参数，原1.13c静态入口核对布局／节拍，范围见[物件](../world/OBJECTS.md#野外箱子破坏物与陷阱)。程序编号、数据消费与限制集中见[联网表现](../../modules/NETWORK.md#联机游玩表现与输入)。普通弓弩／Throw请求不等于全部武器效果验收。

引导箭／骨魂追踪、连锁跳转、炮轰、持续射流、药瓶抛物线／毒烟节拍及其他客户端程序尚未完整接入；RandStart指南存在冲突，暂缓。爆炸密度、碎冰随机分布、空间音频和精确定点路径没有足够原客户端证据，不画自造替代效果。

原规则入口：本地D2MOO PlrMsg／PlrModes、Missiles／MissMode、PathMisc、D2Skills；Diablerie Missile／MissileFunctions只交叉核对显示结构。当前MPQ始终决定原图与参数，参考服务端函数不代表完整D2Client视觉实现。
