# 堆叠、书、金币与赫拉迪克方块

本轮顺序：先核对普通堆叠，再接书／卷轴，之后接金币存取，最后接方块及其界面。原物品尺寸、堆叠上限、初始页数、卷轴与书配对、方块格子及图形都在运行时从挂载 MPQ 读取；金币与储物箱的等级上限规则参照本地 `reference/d2moo` 的 `UNITS_GetStashGoldLimit`。

## 操作

- 同类普通箭矢、弩箭和其他原表可堆叠物品拖到一起会合并，目标装不下的数量留在来源。Ctrl 拖动明确交换，Ctrl+Shift 点击可拆分。不同掉落等级不妨碍同类型普通堆叠。
- 卷轴拖到 `books.txt` 指定的对应书上增加一页；满书拒绝，原卷轴留在原位。书的初始页数与容量读取 `misc.txt`。右键回城书消耗一页；右键鉴定卷轴或鉴定书后点击未鉴定物品。空书保留实例。
- 打开私人储物箱时点击包裹金币栏可存入，点击箱内金币栏可取出；平时点击包裹金币栏可选择丢弃数量。输入数字后按 Enter 或点 OK。库存金币受等级 ×10000 限制，储物箱上限按原引擎等级公式计算，地面单堆上限读取金币原表。
- `Ctrl+Alt+B` 在人物脚边掉落一件 MPQ 定义的赫拉迪克方块，已有方块时拒绝。正常拾取后右键背包中的方块打开原 `supertransmogrifier.dc6` 面板和 `inventory.txt` 的 3×4 格。背包与方块之间可拖动、交换、合并；Shift 点击可整件转移。方块不能放进自身；有物品时不能把方块丢在地上。关闭面板、换图、死亡不销毁内容，角色存档保存方块里的物品。
- 合成按钮保留原面板外观，点击只提示当前尚未实现合成，不消耗材料。

## 代码边界

`content/classic_data.cpp` 适配 `misc.txt`、`books.txt` 与 `inventory.txt`；`gameplay/items/books.cpp` 处理原子装书、消耗页数；`gameplay/items/collection.cpp` 继续复用通用整件转移和堆叠；`gameplay/session/session_gold.cpp` 处理钱包／私人箱／地面金币和调试方块投放；`presentation/inventory_panel.cpp` 只生成拖放意图与格子预览；`presentation/cube_view.cpp` 只绘制 MPQ 原面板。命名管道有 `cube-drop`、`cube-open`、`book-load`、`identify-item` 和 `gold-transfer`，操作仍进入正式会话命令。

角色存档格式为 v82、规则指纹为 `d2x-session-rules-v116-books-gold-cube`；旧格式不自动迁移。原版 .d2s 导入、方块合成配方与共享储物箱仍未实现。Windows Release 构建通过，完整六 MPQ 运行目录 `dist/d2x-runtime-20260924-v82-r116-items-cube/` 短帧启动退出码为 0；管道检查卷轴装书、箭袋合并、金币存取与丢弃、方块携带／存放和存读档。原面板截图留在忽略的 `artifacts/reference-ui/`。没有新增测试脚本、测试用例或专用测试程序。
