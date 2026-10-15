# 词缀、属性层与特殊品质记录

更新：2026-10-12。本页负责Properties／ItemStatCost、词缀／StaffMods／AutoMagic、暗金／套装实际记录及属性消费者边界。品质选择归[GENERATION](GENERATION.md)，属性贡献归[EQUIPMENT](EQUIPMENT.md)，镶嵌层归[SOCKETS](SOCKETS.md)。

## 当前准备与保存

2026-10-12核对启用来源的Properties执行函数，补齐奥玛斯长袍`skill-rand`的随机技能编号／固定等级，以及地狱火炬`randclassskill`的随机职业／Properties.val等级。随机编号在生成时冻结，保存为原stat107／83层，不在装备或重入时重掷；未知函数明确报缺，避免静默丢属性。时间函数18目前没有启用来源引用，未自造世界时钟。表级扫描及运行边界见[EVIDENCE](EVIDENCE.md#客户端与服务端物品核对)。

MagicPrefix／Suffix、AutoMagic、稀有名、StaffMods、UniqueItems／SetItems／Sets及属性层已有内容准备。保留实际掷值、特殊记录身份与条件套装随机值，激活不重掷。基础最大耐久与属性修饰分开，读写／提示／维修共用实际最大耐久，避免保存后重复加成。当前MPQ dur%的百分比最大耐久投影由公共item_properties及D2S额外属性校验支持Properties函数13，依据本地D2MOO ItemMods::ITEMMODS_PropertyFunc13；客户端读取已保存的原网络属性，不能在解码时再加一次。

原表编号与AutoMagic一基索引唯一维护在[DATA](DATA.md)，报价与需求使用同一真实身份，不能按三张词缀的合并编号解释。原位宽、参数、符号与单位见DATA及[存档](../../modules/SAVES.md)。

## 属性程序与消费者

生成期rollItemProperties按Properties操作顺序冻结值，区分独立掷值与第一操作返回值复用，保留set覆盖；函数17的空参数回退、函数19随机当前充能与固定最大充能、函数12／36随机编号均在生成时确定。稀有属性求值与掷值同按前后缀槽交错执行；StaffMods覆盖已有同技能层。套装per-item附加属性按add func选择主列表或条件列表，并保留独立随机和覆盖。operationRolls是临时生成数据，freezeCubeItem后清除，不写D2S。依据D2MOO D2COMMON_11292_ItemAssignProperty及PropertyFunc01–24／36；当前MPQ导出的res-all／all-stats、元素三元组、charged及强制无形记录作静态交叉核对，未运行认证。

content/items/item_properties、equipment_data与gameplay/items/equipment_stats等准备属性；人物总值、战斗、技能、价格、恢复和显示分别消费。同一属性能够解码、生成或显示，不等于全部来源下的服务端效果已执行。未鉴定隐藏信息不泄漏到客户端；未知规则、公式或位宽不能通过省略字段“支持”。

后续按Properties函数／ItemStatCost ID登记：原表参数、品质／来源、冻结值、消费者、Clt表现、服务端执行、保存与证据。普通数值、时间／级别属性、套装条件、技能来源、战斗特殊属性须分别列状态；公共程序共享，不按每个物品再复制一份公式。

## 缺口与依据

2026-10-15源码对齐资料片稀有词缀数量表`{3,4,4,5,5,5,6,6}`（珠宝仍为3–4）、某侧候选耗尽后继续另一侧、全部词缀选择后按前缀0／后缀0至槽2赋属性。新词缀抽签使用总权重加一、末候选回退；仅保存前511个候选但累计全部权重。偏好词缀通过同一等级／类型／职业／品质／组过滤，不先随机抽签再覆盖；魔法品质保留负参数抑制与无前缀补偿后缀行为。AutoMagic加载不要求spawnable，稀有／Crafted／Tempered仍检查rare和已有词缀组。StaffMods六次失败保留最后技能编号，重复属性层覆盖等级，不再拒绝整批掉落。

本地1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`，ImageBase `0x6fc20000`）只读反汇编确认：RVA `0x152FB`引用RVA `0xFD058`的上述八项稀有表；`0x1541D`不计失败选择次数；`0x15450`／`0x15473`按槽先前缀后后缀赋属性；新词缀入口`0x144D0`在`0x14801`增加权重上界，`0x14853`采用最后访问候选，`0x14769`限制保存候选数为511。其余本轮规则依据本地D2MOO ItemsMagic／Items／ItemMode，未逐项二进制认证。当前源码已纳入Release构建；显式魔法词缀、Properties目标预览、强制无形、符文之语和保存重入获有限观察，范围见[运行包记录](../../development/BUILD_AND_RUN.md#当前物品显示与生成收尾)，未新增测试程序；稀有概率及全部属性程序仍未逐项运行认证。

完整Properties程序／组合、特殊显示函数与全部战斗消费者仍未逐项验收；玩家吸取、压碎／撕裂已接[武器命中效果](../combat/WEAPON_EFFECTS.md)，专用触发限制见所属技能专题。先参考master，再查当前MPQ、D2MOO ItemMods／Items及原D2Common；对应属性证据链接[参考设计](../../architecture/REFERENCE_DESIGN.md)。已有词缀与保存代表证据见[EVIDENCE](EVIDENCE.md)。
