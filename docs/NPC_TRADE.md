# NPC 购买实施顺序

数据始终在启动时从原 MPQ 读取。`content/vendor_*` 适配 `npc.txt` 与 `misc/weapons/armor.txt` 的商人列；`gameplay/npc` 生成并持有会话货架，处理购买；`presentation` 只显示报价并提交命令。货架不从 MPQ 抽取为独立文件。

1. 读取商人及物品的原表列，生成有身份、数量和价格的普通品质货架。常驻货物可重复购买；随机货物售出后消失。
2. 通过现有背包创建事务购买，校验正在交谈的商人、距离、余额、库存与空间；保存已售随机货物，提升存档版本和规则指纹。
3. NPC 初次互动先打开菜单，再进入 Talk 或 Trade；商店用 MPQ 原面板、分类标签、底部按钮和物品图标，左侧货架、右侧背包，选择货品后确认购买。named pipe 提供交谈、查询与购买命令。

上述三步已接入源码。原版界面结构依据[暴雪《Diablo II》手册](https://download.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)中的 NPC 交互和 Vendor Screen 说明，并与[英文原版商店截图](https://diablo2.judgehype.com/screenshots/lesbases/interface/NPC-menu.jpg)比较：货架格子位置匹配，顶部修正为四个原页签，底部改用 MPQ 的 `buysellbtn.dc6` 按钮图；面板和购买弹窗仍由 `buysell.dc6`、`buyselltabs.dc6`、`dialogbackground.dc6` 提供。Windows 构建和 Akara 商店画面截取通过，截图与参照链接保存在忽略的 `artifacts/ui-review-20260923/`；未执行购买交互验收。当前只出售原表中可生成、图形完整且基础价格可计算的普通装备和常驻物品。原商店等级公式、第一幕等级上限、普通物品数量与随机品质门槛来自本地 `reference/d2moo/source/D2Game/src/UNIT/SUnitNpc.cpp`；玩家购买使用 `npc.txt` 的 `sell mult`，防具基础价按防御值调整，来自 `reference/d2moo/source/D2Common/src/Items/Items.cpp`。随机种子由地图种子与 NPC 实体 ID 组成，货架在会话开始时生成，随机货品售出状态进入 v14 存档；常驻货品无限次购买。

NPC 菜单的矩形背景仍是现有 UI 绘制，原 MPQ 中尚未核实对应的专用背景；菜单层级与入口按手册组织。Magic 页签按原图显示但不响应，出售和修理按钮置灰；当前页数超过一页时保留额外翻页提示。原图没有这些分页文字，属于现有货架的操作适配。当前暂缓上架魔法、优质／劣质货品和魔法书：这些品类的完整属性计价或充能费用尚未接入，不把它们改作普通货品。出售、回购、赌博、修理、任务折扣、交易刷新以及完整人物等级进展也暂缓。当前角色等级为 1；后续实现升级时再核对货架刷新时机。
