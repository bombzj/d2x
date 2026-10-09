# 地面物品、拾取与寿命

更新：2026-10-09。本页维护世界物品安装、落点、拾取与过期；生成概率归[LOOT](LOOT.md)，金币特例归[GOLD](GOLD.md)，身份／版本归[MODEL](MODEL.md)。

## 落地与拾取

items唯一持有地面库存与规则，准备全部根／子身份和合法落点，transactions同时复验角色与世界版本后提交。当前拾取验证同区、1.8格距离、原0x0801视线及空Cursor；任务携带与互斥资格不能绕过，见[QUEST_ITEMS](QUEST_ITEMS.md)。

规划复用Draft与transactions。普通地面拾取先装入背包已有书本或原AutoStack堆叠，再按已鉴定／需求／空手位尝试自动装备，最后入带／背包；Cursor拾取不合并、不自动装备。合并后余量无法安置时留地并更新数量／页数，整笔无变化则拒绝。争用复验原句柄；接近／背压等待绑定人物、区域代次和走跑意图。新拾取物品直接装备时发原9D Equip。

待拾取请求绑定本次落地generation、人物／区域代次和走跑意图；服务端自恢复及原堆叠余量可以更新物品revision，接近完成后取同一次落地的最新句柄。移除、拾走后再次丢下或位置变更使旧generation失效。GroundLifetime与过期时钟由items持有，不增加客户端协议，也不忽略所有权或区域复验。

本人尸体回收另有顺序与权限，不套上述合书／合堆，见[人物死亡](../characters/PLAYER_DEATH.md)。普通库存命令不能直接改地面或抢占其他人的容器。

## 恢复、过期与原包

地面寿命采用ITEMS_GetGroundRemovalTime：任务物品不消失；稀有／套装／暗金／Crafted或超过10000金币45000帧，可镶嵌填料30000帧，其他15000帧。可靠0A移除接受后清理世界物品和规则。

自恢复按GUID延续独立时钟，规则归[DURABILITY](DURABILITY.md)。普通箱桶／怪物新生掉落、手动丢弃及死亡落地发布GroundDropFact，编码原9C action=2；静态地面同步action=0不重播。原flippy／声音、mode=5不可拾取及反馈成功条件归[PRESENTATION](PRESENTATION.md)。

地面物品、落地代次和时钟不保存到人物D2S；拾入人物后按原字段保存。自恢复期间待拾取与普通过期已有有限证据，全部品质／金币阈值／区域卸载／多人争用与背压未认证，见[EVIDENCE](EVIDENCE.md)。
