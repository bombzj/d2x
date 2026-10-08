# MPQ物品与掉落数据

运行时从挂载MPQ读取定义、原图与文字，不把物品数值、概率或材料清单硬编码。当前资源摘要为659条物品、734条怪物、852条TreasureClassEx及160个自动类别；条数不表示每项效果已执行。

| 原表 | 当前消费者 |
| --- | --- |
| Misc／Weapons／Armor、ItemTypes／BodyLocs／Inventory／Belts／Books | 身份、尺寸、装备／容器布局、堆叠、书本、需求及原图 |
| Properties／ItemStatCost | 属性解析、真实掷值、说明、位宽／参数／符号及保存／发送单位 |
| MagicPrefix／Suffix／AutoMagic、UniqueItems／SetItems／Sets | 品质与词缀生成、报价、需求、装备属性及套装条件 |
| Gems／Runes | 镶嵌、顺序、符文之语、显示和原属性列表 |
| CubeMain | hosting准备普通方块配方；内核原子消费与安置 |
| TreasureClassEx／ItemRatio、怪物难度掉落字段 | hosting准备自然掉落／NoDrop／品质／MF，loot领域安装 |
| Gamble／DifficultyLevels／NPC原商店列 | 个人赌博货架、普通货架与价格准备 |

Expansion分隔行不计TXT编号；前后缀／自动词缀磁盘索引一基，特殊品质身份零基。网络和磁盘AutoMagic均是本表索引，不是三张词缀合并编号。未知属性／公式／位宽明确拒绝，不省略或迁成其他参数。

网络物品位流无JM头／seed／Realm尾；所有权来自原包。D2S支持范围见[存档](../../modules/SAVES.md)，执行与来源边界见[库存](../../modules/INVENTORY.md)，显示见[支持](SUPPORT.md)。资源工具的item／drops／treasure／quality／loot-plan入口只读报告，不替代运行资格、原包或保存验证。来源与许可见[资料来源](../../resources/THIRD_PARTY.md)。
