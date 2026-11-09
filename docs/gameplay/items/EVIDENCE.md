# 物品规则依据与原版协议参考

本页维护物品规则的原函数索引和注明 Host／Join 方向的原版协议观察。D2X 行为由[物品专题](README.md)维护，D2S 格式及样本限制见[存档](../../modules/SAVES.md)。

## 客户端与服务端物品核对

规则来源为当前`assets/mpq2`、本地固定D2MOO及注明版本的原DLL核对，固定版本见[资料来源](../../resources/THIRD_PARTY.md)。

| 链路 | 核对对象 | 原版依据与范围 |
| --- | --- | --- |
| 原库存请求／客户端 | 2A Cursor／地面直接入本人方块，63空Cursor背包入带，37最后成功购入身份鉴定；D2X Shift入带、列预览和原请求编码同步 | PlrMsg、ItemMode、D2Inventory、SUnitNpc；自动入带优先底格同类药水，明确拖放仍保留指定格 |
| 附近地面拾取 | 0x16 的动作准入与库存事务 | `PlrMsg::D2GAME_PACKET_Handler_6FC89320` → `PlrModes::D2GAME_PLRMODES_First_6FC7F340(NU, 0)` → `Rcv0x16` → `sub_6FC828D0` → `ItemMode::PickupItem／PickupItemEx`。D2X 门禁见[地面拾取](GROUND.md#落地与拾取) |
| 实际属性／保存 | 奥玛斯长袍随机技能、火炬随机职业的原stat107／83层；冻结编号和等级，未知函数不静默丢属性 | ItemMods PropertyFunc12／36与MPQ Properties.val；函数18未有启用来源，未实现自造时间规则 |
| 掉落 | NoDrop总人数包含已入场死亡玩家，同区域存活同队成员完整贡献；佣兵实际击杀叠加主人及佣兵MF／GF | Items_DropItemByMonster；TC、首杀、暗金一次限制及失败品质耐久沿现有路径，不替换概率 |
| 活体低血量 | 0C 可将存活怪物的生命比例报告为零 | `MonsterMode` 先将生命取整。活体与尸体按原动作模式区分，见[怪物选取](../monsters/PRESENTATION.md#活体攻击目标与悬停) |
| 金币 | 同区域同队自然金堆整除分配／拾取者余数，原存活人数除数及死亡队员分支、有符号余数，所有钱包及地面同笔提交；玩家手动／死亡丢金不分配 | ItemMode／Party及当前1.13c反汇编，已纠正D2MOO队友容量指针，见[GOLD](GOLD.md)；极限分支静态核对，未逐项录包认证 |
| 方块 | 输入匹配、`useitem`／`usetype` 及 `rep,qty` | CubeParser／PlrTrade；`useitem` 保留品质与等级，`usetype` 重生基底。额外堆数和书本边界见[方块](CUBE.md) |
| 镶嵌／符文之语 | 原0x28实际子项、宿主类型、品质、顺序、孔数、冻结属性及D2S保留沿既有路径 | MPQ有78条complete，其中23条server；Realm Ladder过滤证据仍不足，见[SOCKETS](SOCKETS.md) |
| 商店与鉴定 | 货架、本人来源、鉴定对象与库存输出 | SUnitNpc／ItemMode；交易流程及待核实报价见[城镇经济](ECONOMY.md)与[鉴定](IDENTIFICATION.md) |

## 原版协议观察与缺口

### 职业装备与原版拒绝对照

原版 1.13c Host／D2X Join 的隔离女巫样本中，入场以 9C/action18 指派 Cursor 上的爪 `ktr`。一次放置与装备操作超时，记录缺 C2S 原字节及逐次请求参数，原因未定。原 2C 为 NPC 事件 0x12。后续同类条件的成功操作见下文。

本地D2MOO PlrMsg::Rcv0x18及ItemMode::sub_6FC45E60用于解释资格分支：背包原InvPage为0，S2C存储page为InvPage+1；装备交换的身体／职业资格可在无物品回包的分支直接返回。存档Cursor加载另核对PlrSave::sub_6FC895E0及sub_6FC4B9D0，不能仅凭入场手持指派断言原版指针恢复失效。当前存档格式和旧档保持不变，证据不足部分保留为开放项。

原版Host的9D/action8卸装回包在mode4时仍保留来源body／x：已观察body4／x4、body8／x8及body3／x3；19取背包／箱子回包保留来源page1／5。D2MOO ItemMode::sub_6FC45B30及Items::ITEMS_SerializeItemToBitstream确认移入Cursor时保留原body／坐标／InvPage用于移除原位置。D2X现从InventoryFact.before提供这些仅用于原包的来源字段，并让装备x对应原body；不修改D2S格式。自有客户端以mode4判定Cursor，不要求body／page为零。

原版客户端1.13c Join D2X Host的兼容对照中，用户确认背包拿放及dgr卸装／重装有正常鼠标反馈；原客户端直接禁止女巫装备异职业ktr，没有发送1A／1D。该本地拒绝是原版客户端行为，D2X预览／提交补齐同一职业限制。这一方向可参考原客户端资格与反馈，但宿主回包来自D2X，不能作为原版服务端字段的依据；箱子／方块／腰带及保存重入未覆盖。

D2X Join原版1.13c Host（ShotDruid）的后续对照中，隔离女巫入场时ktr在mode4；18放背包(8,0)、19拿回Cursor，再18放回，随后1C卸下dgr、18放背包(7,1)、19拿起及1A/body4重装均收到原Host确认。原Host卸装回包保留body4／x4，背包拿取保留来源page1。按物品类型归一GUID后，七条C2S的包号／坐标／page／body与反向原客户端操作一致；这只支持所列请求的编码对照，不涵盖全部容器或失败分支。

同一原Host连接以69正常退局，收到B3／B0／05／06返回选角；同进程及新D2X进程再次Join后，原服副本恢复ktr为背包(8,0)、dgr为右手body4，Cursor为空。这是D2X客户端与原版宿主的混合协议观察，不能扩展为原版客户端画面、全部D2S布局或全部存档组合的参考结论。

通用卷轴／书本及弹药／药瓶规则见[GENERAL](../skills/GENERAL.md)，原服开门消息参考见[COMMON](../skills/COMMON.md#原服施法参考)。原服交易／金币、药剂和商店的协议观察归[NETWORK](../../modules/NETWORK.md#既有有限证据)。

概率、经济、配方、特殊存档身份及多人争用的覆盖范围由[各物品专题](README.md)维护。管理生成只用于准备观察条件。
