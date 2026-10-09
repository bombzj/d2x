# MPQ物品与掉落数据

更新：2026-10-09。本页负责当前MPQ物品表与编号／单位契约。运行时从挂载MPQ读取定义、原图与文字，不把数值、概率或材料清单硬编码；实际全量条目以当前挂载资源为准，不用旧资源统计认证执行。分类与规则入口见[目录](README.md)。

| 原表 | 当前消费者 |
| --- | --- |
| Misc／Weapons／Armor、ItemTypes／BodyLocs／Inventory／Belts／Books | 身份、尺寸、装备／容器布局、堆叠、书本、需求及原图 |
| Properties／ItemStatCost | 属性解析、真实掷值、说明、位宽／参数／符号及保存／发送单位 |
| MagicPrefix／Suffix／AutoMagic、UniqueItems／SetItems／Sets | 品质与词缀生成、报价、需求、装备属性及套装条件 |
| Gems／Runes | 镶嵌、顺序、符文之语、显示和原属性列表 |
| CubeMain | hosting准备原表配方／输出；crafting与transactions复验、消费及安置 |
| TreasureClassEx／ItemRatio、怪物难度掉落字段 | hosting准备自然掉落／NoDrop／品质／MF，loot协调、items安装世界物品 |
| Gamble／DifficultyLevels／NPC原商店列 | 个人赌博货架、普通货架与价格准备 |

Expansion分隔行不计TXT编号；前后缀／自动词缀磁盘索引一基，特殊品质身份零基。网络和磁盘AutoMagic均是本表索引，不是三张词缀合并编号。未知属性／公式／位宽明确拒绝，不省略或迁成其他参数。

原包／磁盘负载不能互换，网络位流见[PRESENTATION](PRESENTATION.md)，原D2S支持范围见[存档](../../modules/SAVES.md)。执行／来源按[物品目录](README.md)分别登记；属性编号／位宽可解析不证明消费者完成。资源工具的item／drops／treasure／quality／loot-plan入口只读报告，不替代运行资格、原包或保存验证。来源与许可见[资料来源](../../resources/THIRD_PARTY.md)。
