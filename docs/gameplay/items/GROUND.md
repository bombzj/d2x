# 地面物品、拾取与寿命

更新：2026-10-20。本页维护世界物品安装、落点、拾取与过期；生成概率归[LOOT](LOOT.md)，金币特例归[GOLD](GOLD.md)，身份／版本归[MODEL](MODEL.md)。

## 落地与拾取

items唯一持有地面库存与规则，准备全部根／子身份和合法落点，transactions同时复验角色与世界版本后提交。当前拾取验证同区、1.8格距离、原0x0801视线及空Cursor；任务携带与互斥资格不能绕过，见[QUEST_ITEMS](QUEST_ITEMS.md)。

玩家手动丢物／丢金币、腰带溢出及死亡落地通过items::System::placement复用AreaStore::nativeFreeSpace：以人物原整数坐标`(X+2,Y+3)`为中心；该点不在原近邻活动房间时回退人物坐标。空旷且人物不移动时，第一件占中心，随后八件依次占相对中心的`(-1,0)`、`(1,0)`、`(0,-1)`、`(0,1)`、`(-1,-1)`、`(1,-1)`、`(-1,1)`、`(1,1)`，之后继续外环。每环按曼哈顿距离优先、同距保留原扫描首项，搜索半径小于50；碰撞使用size=1／`COLLIDE_MASK_SPAWN=0x3E01`及当前MPQ的`expfield.d2`／fieldMask=`0x0801`，保留原版搜索耗尽时回退起始点的分支。物品占位取事务草稿，单位／宠物／门户取实时公共空间投影，落点保留实际房间所属区域。普通新生掉落的prepare路径仍有独立落点实现，不据此认定所有来源已对齐。

依据为本地D2MOO `ItemMode::D2GAME_GetFreeSpaceEx_6FC4BF00`、`Items::sub_6FC4FCA0`／`ITEMS_DropGoldPile`及`D2Collision::COLLISION_GetFreeCoordinatesImpl`。D2MOO的sub_6FC4FCA0将横坐标转录为`Y+2`；固定原1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`）的手动丢金RVA`0x38037`调用`0x11CA0`，`0x11D38–0x11D4E`确认分别读取X／Y并加2／3，通用寻位`0xE935–0xE943`亦一致。按该原版分支使用`X+2`；当前仅完成静态核对，连续丢弃的实际画面及障碍／跨区组合尚未运行验证。

规划复用Draft与transactions。普通地面拾取先装入背包已有书本或原AutoStack堆叠，再按已鉴定／需求／空手位尝试自动装备，最后入带／背包；Cursor拾取不合并、不自动装备。合并后余量无法安置时留地并更新数量／页数，整笔无变化则拒绝。争用复验原句柄；接近／背压等待绑定人物、区域代次和走跑意图。新拾取物品直接装备时发原9D Equip。

待拾取请求绑定本次落地generation、人物／区域代次和走跑意图；服务端自恢复及原堆叠余量可以更新物品revision，接近完成后取同一次落地的最新句柄。移除、拾走后再次丢下或位置变更使旧generation失效。GroundLifetime与过期时钟由items持有，不增加客户端协议，也不忽略所有权或区域复验。

本人尸体回收另有顺序与权限，不套上述合书／合堆，见[人物死亡](../characters/PLAYER_DEATH.md)。普通库存命令不能直接改地面或抢占其他人的容器。

## 恢复、过期与原包

地面寿命采用ITEMS_GetGroundRemovalTime：任务物品不消失；稀有／套装／暗金／Crafted或超过10000金币45000帧，可镶嵌填料30000帧，其他15000帧。可靠0A移除接受后清理世界物品和规则。

自恢复按GUID延续独立时钟，规则归[DURABILITY](DURABILITY.md)。普通箱桶／怪物新生掉落、手动丢弃及死亡落地发布GroundDropFact，编码原9C action=2；静态地面同步action=0不重播。原flippy／声音、mode=5不可拾取及反馈成功条件归[PRESENTATION](PRESENTATION.md)。

地面物品、落地代次和时钟不保存到人物D2S；拾入人物后按原字段保存。全部品质／金币阈值／区域卸载／多人争用与背压仍待原版核对，自恢复中的revision变化不得使同次落地的待拾取请求失效。
