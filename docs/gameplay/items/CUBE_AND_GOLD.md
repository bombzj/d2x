# 堆叠、书、金币与赫拉迪克方块

本页维护堆叠、书页、钱包／箱内金币与方块操作。原物品尺寸、堆叠上限、初始页数、卷轴与书配对、方块格子及图形都在运行时从挂载 MPQ 读取；金币与储物箱的等级上限规则参照本地 `reference/d2moo` 的 `UNITS_GetStashGoldLimit`。

## 操作

- 同类普通箭矢、弩箭和其他原表可堆叠物品拖到一起会合并，目标装不下的数量留在来源。Ctrl 拖动明确交换，Ctrl+Shift 点击可拆分。不同掉落等级不妨碍同类型普通堆叠。
- 卷轴拖到 `books.txt` 指定的对应书上增加一页；满书拒绝，原卷轴留在原位。书的初始页数与容量读取 `misc.txt`。右键回城书消耗一页；右键鉴定卷轴或鉴定书后点击未鉴定物品。空书保留实例。
- 打开私人储物箱时点击右侧包裹金币栏可存入，点击左侧箱子上方第一条金额栏可取出；第二条显示箱子金币上限。平时点击包裹金币栏可选择丢弃数量。输入数字后按 Enter 或点 OK。库存金币受等级 ×10000 限制，储物箱上限按原引擎等级公式计算，地面单堆上限读取金币原表。
- `Ctrl+Alt+B` 在人物脚边掉落一件 MPQ 定义的赫拉迪克方块，已有方块时拒绝。正常拾取后右键背包中的方块打开原 `supertransmogrifier.dc6` 面板和 `inventory.txt` 的 3×4 格。背包与方块之间可拖动、交换、合并；Shift 点击可整件转移。方块不能放进自身；有物品时不能把方块丢在地上。关闭面板、换图、死亡不销毁内容，角色存档保存方块里的物品。
- 合成按钮提交 TransmuteCube：支持原 cubemain 绑定的赫拉迪克法杖、克林姆的意志，以及下述 8 条加孔和去镶嵌配方；资格、材料、数量、难度和空光标均由宿主复验，副本库存成功后一次提交。不匹配时保留材料；通用全部配方尚未实现。

## 合成范围与原表语义

“箱子合成”对应赫拉迪克方块；私人储物箱只负责存取，野外宝箱只负责交互／掉落。原版方块也用于普通物品制作；[暴雪方块说明](https://classic.battle.net/diablo2exp/items/cube.shtml)可交叉核对用途，实际配方、属性、限制仍以当前 MPQ 与对应原引擎分支为准，不从网页描述建立自定配方。

2026-10-04 当前挂载 `cubemain.txt` 有 146 条 enabled=1 原行，不是本项目已支持的配方数。源码实际执行范围如下：

| 配方类别／当前 MPQ 例子 | 本项目 |
| --- | --- |
| `msf + vip → hst`、`qf1 + qhr + qey + qbr → qf2` | 两项已绑定原代码／输入数量；`session_inventory.cpp::transmuteCube` 使用私有库存草稿消耗、创建并一次提交，要求背包持有方块、空 Cursor、未完成对应任务、材料难度合格、没有多余物品 |
| 同种同级宝石／骷髅升一级、符文升级（部分还需宝石） | 未实现；`Misc.BetterGem` 的祭坛升级入口不能代替方块配方 |
| 回复药剂、普通药剂转换、箭矢／弩箭互换及投掷武器制作 | 未实现 |
| 戒指／项链转换、魔法／稀有物品重掷、固定前后缀制作 | 仅接入三条加孔魔法武器重掷：标准宝石＋带孔武器、无瑕宝石／碎裂宝石＋魔法武器；原 lvl 优先，输出 30／30／25 级，重掷原词缀、销毁旧孔内物品；其余未实现 |
| Hitpower／Blood／Caster／Safety Crafted 配方，含 jewel 材料 | 未实现；没有 Crafted 实例品质或 D2S 对应分支。jewel 不是宝石的替代名称 |
| 基础装备打孔、稀有加孔 | 已接入普通且无孔的武器／身体护甲／头盔／盾牌四条原配方；从原 1–6 范围掷值，再按底材／等级／占格上限截断，不改成在实际上限内均匀抽样。三完美骷髅＋原暗金乔丹之石＋无孔稀有物品加 1 孔；特殊暗金按原行校验，不能用任意戒指替代 |
| 去除孔内物品 | 按原 `useitem,uns`：Hel＋回城卷轴＋带孔物品，销毁全部填充物及符文之语列表，保留孔、底材和原属性；无形物理标志不因删除符文之语列表而自动恢复 |
| 修理／补充数量、稀有／暗金底材升级 | 未实现；NPC 修理不能替代方块配方 |
| Cow Portal、Pandemonium Portal／Finale Portal、Token of Absolution | 原表存在，方块入口未实现；生成门户还需要独立任务／地点／模式规则，不能当普通物品创建 |

完整解释器仍需绑定以下字段，不应只按 description 文字匹配或只支持确切底材代码：

孔相关分支由 `content/items/socket_data.cpp` 绑定原 input／output／mod 字段、品质／sock／nos、材料类别／数量、特殊暗金原行、version／min diff、lvl／plvl／ilvl；玩法在私有草稿上匹配完整输入并一次提交。多余／错误材料或输出失败时保留原库存和随机状态。当前 8 条加孔与 1 条去镶嵌已有限实机验证，详见 [支持清单](SUPPORT.md)；没有覆盖当前 146 条 enabled 的完整配方表，也没有通用 op 或任意输出指令解释器。

| 字段／语法 | 必须保留的含义与当前限制 |
| --- | --- |
| enabled、ladder、min diff、version、class、op／param／value | 配方启用、难度、模式／版本／职业及条件操作。当前任务加载器只筛 enabled 并核验固定输入；并未完整解释这些字段。两项任务资格由现有宿主规则复验，不代表通用 op=28 已移植 |
| numinputs、input 1…7 | 有底材代码及 ItemTypes 类别，`qty=`、品质、孔／无形等限定；实际总材料数量和输入列数不同。七个 input 列不代表只允许七件材料 |
| output、output b／c | 有新物品及 `useitem`／`usetype`、品质／前后缀／升级／孔／数量等修饰，也有门户操作；多输出、原物品保留与材料销毁需要统一原子计划 |
| lvl、plvl、ilvl 与 b／c 对应列 | 输出等级可以依赖角色与输入等级，决定词缀和需求；不可固定为玩家等级 |
| mod 1…5 及 b／c 对应列 | chance／param／min／max、固定属性与随机词缀是不同来源，须保留原掷值并与保存消费者一致 |

本地依据：D2MOO `D2Common/DataTbls/HoradricCube.cpp` 的 InputParser／OutputParser 处理引号与逗号修饰，输入名称先查 ItemTypes 再查底材，亦可查暗金／套装身份；`D2Game/PLAYER/PlrTrade.cpp::PLRTRADE_CheckCubeInput`、`PLRTRADE_CreateCubeOutputs` 与末尾合成入口负责匹配、op 条件及输出。OpenDiablo2 的 `cubemain_record.go` 用于交叉核对字段，注释已承认其 class 语法是假设；Diablerie 的 `Player.Use` 方块分支为空，不能作为通用合成已实现的依据。参考边界见 [资料来源](../../resources/THIRD_PARTY.md)。

两项任务配方只按当前已核实的窄分支执行，不匹配时保留材料。资源表存在、按钮可点击和物品可存入方块都不能证明通用合成完成；本次实际合成及保存往返覆盖镶嵌相关分支，未重新验收两项任务配方。

## 代码边界

`content` 物品适配读取 `misc.txt`、`books.txt` 与 `inventory.txt`，`content/quest` 准备支持的任务配方；`gameplay/items/books.cpp` 处理原子装书、消耗页数；`gameplay/items/collection.cpp` 分别规划整件转移和地面拾取，共用堆叠资格；`gameplay/session/session_gold.cpp` 处理钱包／私人箱／地面金币和调试方块投放；`presentation/inventory/inventory_panel.cpp` 只生成拖放意图与格子预览；`presentation/inventory/cube_view.cpp` 只绘制 MPQ 原面板。命名管道有 `cube-drop`、`cube-open`、`book-load`、`identify-item` 和 `gold-transfer`，操作仍进入正式会话命令。

保存采用原 D2S v96；书页、金币、方块及内部物品按原字段保存，不恢复 UI 访问授权。共享储物箱和通用配方仍未实现；任务配方见 [第二幕](../quests/ACT2.md)、[第三幕](../quests/ACT3_5.md)，格式见 [存档](../../modules/SAVES.md)。既有堆叠／书页／金币有限检查不代替当前全部配方与键鼠验收。
