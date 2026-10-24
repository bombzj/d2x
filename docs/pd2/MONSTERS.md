# PD2怪物与伙伴差异

本页维护非玩家实体的身份、属性及生命周期差异。版本见[版本与证据](BUILDS.md)；召唤／攻击程序的算法归[技能](SKILLS.md)，区域来源归[地图](MAPS.md)，装备属性归[物品](ITEMS.md)。定义准备和AI执行分别归[怪物模块](../modules/MONSTERS.md)。

## MonStats与预设身份

已确认：目标`monstats.bin`有1283条、每条424字节，名称链接采用编译记录顺序；TXT的hcIdx不是该表的编译字段。row1198的BIN首WORD ID为1198，同名fallen1Nakrul的TXT hcIdx为1199；末row1282的BIN ID为1282，TXT hcIdx为1284。

预设名称链接及PD2客户端的classId／descfunc23文案按编译记录身份匹配，并保留同名首次解析；SuperUniques的hcIdx校验单独保留。不能把所有身份都改成行号，也不能把hcIdx缺号当成原图缺失。其他怪物消费者仍须逐项核对其字段所用身份。

| 消费者 | 当前入口／边界 |
| --- | --- |
| 预设名称与单位类型 | `src/world/preset_identity.cpp`；与公共怪物目录的hcIdx来源分别核对 |
| 物品交互／怪物属性文案 | `src/client/remote_inventory.cpp`、`src/content/items/item_descriptions.cpp`；原版保持既有身份匹配，详见[物品表现](../gameplay/items/PRESENTATION.md) |
| 怪物目录、类型与专用来源 | `src/content/monsters/{monster_catalog,monster_type_data}.cpp`；存在hcIdx消费者，不视为全部编译身份已统一 |
| Desecrate池 | `src/content/pd2/corpse_data.cpp`当前按名称取记录后读取hcIdx；扩展身份存在缺号时须继续核对，不能由预设修复推断本路径也已对齐 |

## 编译字段宽度与MonProp

MonStats2.MeleeRng按原BYTE编译。当前spiritofbarbs的TXT值999对应BIN值231，不能作为大于一字节的近战范围。消费者为`src/content/monsters/monster_catalog.cpp`，规则准备保留原宽度；255另走原武器类别后备分支。

当前fenris的TXT虽然列出inc_splash_radius，配套`monprop.bin`三个难度的该属性ID均为FFFFFFFF，原生跳过该槽。宿主也跳过，不能仅因属性名称存在而补成478。MonProp及召唤定义的具体边界见[召唤与诅咒](SKILLS.md#desecrate召唤和诅咒的数据边界)。

骷髅MonProp溅射使用真实单位接触事件；触发通知保留正在进行的攻击动作，不能重新播放施法／攻击造成短暂消失。算法、手位和伤害来源归[Do163](SKILLS.md#do163实际手位分配伤害与独立防御)，通用原包表现归[怪物表现](../gameplay/monsters/PRESENTATION.md)。

## 召唤实体与伙伴

当前死灵内容准备为`src/content/pd2/summon_data.cpp`，德鲁伊／刺客继续使用各自领域准备；PetType使用原生idx，不能跨Expansion分隔行直接取TXT物理行号或Id。骷髅、法师、弓手、石魔的MonStats、MonProp、隐藏攻击及数量／生命公式使用当前表。

弓手复用受控远程角色的实现载体，但保留自身原生class、组件、飞弹、技能及属性，不把实际身份替换为法师。Desecrate死态实体无经验／掉落、部分出生失败保留已创建尸体及到期取消选择的规则归[技能负责条目](SKILLS.md#desecrate召唤和诅咒的数据边界)，不与自然人口生成混为一条路径。

佣兵服务、装备槽／资格及战斗程序是不同能力。CN七槽原请求及当前资格见[佣兵](../gameplay/characters/HIRELINGS.md)，服务回调见[城镇](TOWN.md)；它们不证明自有宿主具备全部PD2佣兵战斗差异。

## 新增怪物、首领与掉落

PD2自有宿主使用当前MonStats／Skills等定义并复用公共人口、AI、战斗及掉落系统。`src/content/pd2/town_data.cpp`调用公共掉落准备；没有独立的“全部PD2新增怪物已实现”保证。地图新增怪物及首领的出生、特殊AI、免疫／伤害、死亡程序和专属掉落需逐条定位。

Open Wounds对扩展首领935／936的镜像写入尚未接入；该编号限定当前版本，规则归[物品](ITEMS.md#open-wounds与crushing-blow)。Do163对攻击者1112的特殊防御修正限定该接触程序，不能推广成所有怪物攻击的统一减免。

## 待核实与未完成

新增身份的全部名称／class链接、各难度AI与隐藏技能、地图／首领程序、佣兵战斗、Iron Golem材料／恢复、Revive完整动作、伙伴跨区和复杂装备组合仍未全部恢复或认证。数据行、资源原图、复用原AI和已接专用程序分别描述；中立单位不可用敌对替身填补未知行为。
