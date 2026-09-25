# NPC 商店、赌博与修理

当前源码 v89／规则 `v123-hireling-services`，未构建或运行验收。AGENTS.md 要求当前交接阶段不新增测试、运行检查或打包。此前 v87 右键购买即时入包、关闭商店不恢复菜单的运行记录仍只覆盖旧代码。

## 数据与服务

运行时读取原 MPQ 的 NPC、Weapons／Armor／Misc、ItemTypes、StorePage、Gamble、DifficultyLevels、词缀／品质、Properties／ItemStatCost 和 TBL。NPC.txt 是价格表，服务身份另按本地 D2MOO SUnitProxy／SUnitNpc 的引擎配置处理，不按显示名猜服务。

- 赌博：Gheed、Elzix、Alkor、Jamella、Nihlathak、Drehya。当前可到达世界只有第一幕。
- 修理：Charsi、Fara、Hratli、Halbu、Larzuk。菜单显示 Trade / Repair；其他商人显示 Trade。
- 现有任务服务仍按任务状态出现：凯恩鉴定、Akara 免费重置、Charsi 灌注、Warriv 东行。Kashya 在等级至少 8 或已领取血鸟奖励后提供 Hire；列表、O 面板及装备实现与边界见 [佣兵](HIRELINGS.md)。

## 库存与购买

`gameplay/npc/store.cpp` 根据每个 NPC 自己的 Min／Max、MagicMin／MagicMax／MagicLvl、PermStoreItem 和物品类型页签建立货架。普通和魔法数量独立生成；32 是生成失败次数上限，不是货品数量上限。普通难度各幕等级上限为 12／20／28／36／45，基础 ilvl 为角色等级加 5；ilvl 达到 25 后不生成普通随机货品。噩梦／地狱升级读取原物品的 ubercode／ultracode 和 NightmareUpgrade／HellUpgrade。

普通、优质和可计价的魔法物品上架；带未实现计价属性的候选暂缓，不将其退成白装。自动词缀、法杖技能、凹槽等生成分支仍缺，因此当前货架还不能称为原版完整复刻。离开野外返回城镇时按当前角色等级重建货架，清除其已售状态；多人占用、原 NPC 定时刷新与随机流等价未实现。新局也重建货架。

可修理投掷武器与常驻箭袋按容量出售。购买保留货架上的品质、词缀掷值和防御；成功后先扣钱包、再扣个人箱金币，再发布库存事件。余额、背包空间或会话校验失败不扣款、不消耗货品。右键直接购买，左键确认购买；绘制前的零时间步命令处理保留，世界暂停不会阻塞库存更新。

购买价格由 `content/item_pricing.cpp` 结合基础费用、防具防御、词缀／品质费用、附加统计值及 NPC SellMult 计算。第一幕已完成或待领奖的对应任务应用 NPC 原 quest multiplier；装备 reducedprices 对购买、修理和赌博应用同一折扣查询。UI 显示与实际扣款调用同一报价。原反编译费用函数本身标有舍入疑点，当前没有原版逐金额运行对照。

## 赌博

`content/gamble_stock.cpp` 读取 Gamble 候选池及 DifficultyLevels 品质／升级概率，生成 14 个报价；戒指和项链占前两个槽位。等级使用角色等级 -5 至 +4，范围限制 5–99。基础价格取原 gamble cost 或原引擎价格公式；不使用 MF 改变赌博品质。

打开 Gamble 时生成本次库存。成交前只显示原基础物品和费用，隐藏品质、属性及升级底材；成交后显示已鉴定实例。随机流仅在货架生成成功时提交；限量暗金在购买后登记。关闭交谈清空赌博库存，重新开启重新生成。暂用现有原交易面板，不宣称完整还原赌博专用客户端界面。

## 修理

在修理商的 Trade / Repair 窗口选择原修理按钮，再点击右侧装备或背包物品；悬停显示修理报价。会话、物品版本和余额都在玩法层验证。修复最大耐久并补满可修理投掷数量；无法破坏和自动补充属性遵循对应修理规则。充能技能费用、修理全部、无形物品等尚未完成；无法解释的属性拒绝报价。

## UI 与限制

商店在左、背包在右，使用原 buysell、buyselltabs、buysellbtn 等素材。分类来自 StorePage；前四个标签图是选中态、后四个是未选中态。商店与背包调用同一物品说明，底部 STASH 显示个人箱金币。没有修理服务时不绘制修理图标；关闭商店不再恢复 NPC 菜单。

出售、回购、修理全部及佣兵复活仍待实现；额外翻页、菜单背景和部分布局仍是项目适配。参考来源见 THIRD_PARTY.md，物品效果覆盖与缺口见 ITEM_COMPLETION.md。
