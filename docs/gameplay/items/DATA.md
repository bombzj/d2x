# MPQ物品与掉落数据

当前资料片使用lod-named-txt-v1适配，运行时从挂载MPQ读取定义、原图与文字。当前资源摘要为659条物品、734条怪物、852条TreasureClassEx及160个自动类别；条数不表示每项效果可执行，也不用于硬编码游戏参数。

## 定义与消费者

| 原表 | 当前用途 |
| --- | --- |
| misc／weapons／armor、ItemTypes／BodyLocs／Inventory／Belts／Books | 身份、尺寸、装备／容器布局、堆叠与书本配对、原图 |
| Properties／ItemStatCost | 属性元数据、说明、位宽／参数／符号／保存及发送单位 |
| MagicPrefix／Suffix、UniqueItems／SetItems／Sets、Gems／Runes | 名称、属性、孔内和符文之语显示及独立编码 |
| CubeMain | 原配方导入／报告，联机合成由原服 |
| TreasureClassEx／ItemRatio、怪物难度掉落字段 | 独立资源报告中的候选／品质计算，不向联机生成掉落 |

原表Expansion分隔行不计TXT编号；前后缀原磁盘编号为一基，特殊品质身份为零基。未知属性／公式／位宽不能静默省略或迁移成其他参数。

## 当前边界与查询

网络物品位流不是D2S JM记录：无JM头／seed／Realm尾，位置和所有者按原包模式解释；原服回包形成物品副本。独立D2S工具支持范围见[存档](../../modules/SAVES.md)，联机提示消费者见[支持](SUPPORT.md)。

d2x_assets保留item、drops、treasure、quality、loot-plan等原表／报告入口，具体命令以工具usage为准。它们不替代原服自然掉落、配方资格、产物或金币结算。数据来源／许可见[资料来源](../../resources/THIRD_PARTY.md)，MPQ挂载与摘要见[资源](../../resources/MPQ.md)。
