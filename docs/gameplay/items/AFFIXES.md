# 词缀、属性层与特殊品质记录

更新：2026-10-09。本页负责Properties／ItemStatCost、词缀／StaffMods／AutoMagic、暗金／套装实际记录及属性消费者边界。品质选择归[GENERATION](GENERATION.md)，属性贡献归[EQUIPMENT](EQUIPMENT.md)，镶嵌层归[SOCKETS](SOCKETS.md)。

## 当前准备与保存

MagicPrefix／Suffix、AutoMagic、稀有名、StaffMods、UniqueItems／SetItems／Sets及属性层已有内容准备。保留实际掷值、特殊记录身份与条件套装随机值，激活不重掷。基础最大耐久与属性修饰分开，读写／提示／维修共用实际最大耐久，避免保存后重复加成。当前MPQ dur%的百分比最大耐久投影由公共item_properties及D2S额外属性校验支持Properties函数13，依据本地D2MOO ItemMods::ITEMMODS_PropertyFunc13；客户端读取已保存的原网络属性，不能在解码时再加一次。

原表编号与AutoMagic一基索引唯一维护在[DATA](DATA.md)，报价与需求使用同一真实身份，不能按三张词缀的合并编号解释。原位宽、参数、符号与单位见DATA及[存档](../../modules/SAVES.md)。

## 属性程序与消费者

content/items/item_properties、equipment_data与gameplay/items/equipment_stats等准备属性；人物总值、战斗、技能、价格、恢复和显示分别消费。同一属性能够解码、生成或显示，不等于全部来源下的服务端效果已执行。未鉴定隐藏信息不泄漏到客户端；未知规则、公式或位宽不能通过省略字段“支持”。

后续按Properties函数／ItemStatCost ID登记：原表参数、品质／来源、冻结值、消费者、Clt表现、服务端执行、保存与证据。普通数值、时间／级别属性、套装条件、技能来源、战斗特殊属性须分别列状态；公共程序共享，不按每个物品再复制一份公式。

## 缺口与依据

完整Properties程序／组合、特殊显示函数与全部战斗消费者仍未逐项验收，未完成的吸取、压碎／撕裂、专用触发等见战斗／技能专题。先参考master，再查当前MPQ、D2MOO ItemMods／Items及原D2Common；对应属性证据链接[参考设计](../../architecture/REFERENCE_DESIGN.md)。已有词缀与保存代表证据见[EVIDENCE](EVIDENCE.md)。
