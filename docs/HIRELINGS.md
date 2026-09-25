# 资料片佣兵

当前源码：存档 v91，规则 `v126-npc-interact`。佣兵功能于 v89 接入；上一版 v91／规则 v125 已构建、打包并做营地基础 UI 冒烟；本轮尚未构建；佣兵界面与行为未单独验收；以下说明源码已接入的行为，不代表完整佣兵系统验收。

## 原始数据与依据

- 运行时读取 MPQ `Hireling.txt` 的 `Version=100` 行。卖家、难度、佣兵身份、姓名范围、等级、费用、生命、力量、敏捷、伤害、命中、防御、抗性与经验曲线均来自原表。姓名读 TBL，列表的 Fire Arrow／Cold Arrow 描述读 `HireDesc.txt`。
- `Inventory.txt` 的 Hireling 行提供四个装备框坐标；O 面板读取 `npcinv.dc6`，空槽读取 `inv_helm_glove.dc6`、`inv_armor.dc6` 和 `inv_weapons.dc6`；列表滚动条读取 `scrollbar.dc6`。外框与现有背包、任务面板共用。
- 本地 D2MOO 固定快照 `5596f5c`：`SUnitNpc.cpp::D2GAME_NPC_FirstFn_6FCC67D0`、`D2GAME_NPC_BuildHirelingList_6FCC6FF0`、`D2GAME_NPC_AssignMercenary_6FCCB520` 提供候选、雇佣条件、扣金与任务奖励依据；`D2Common/Monsters/Monsters.cpp::MONSTERS_HirelingInit`、`MONSTERAI_UpdateMercStatsAndSkills` 提供初始化和升级数值；`SUnitDmg.cpp` 提供佣兵经验份额及升级上限。O 键另核对 OpenDiablo2 `key_map.go`。
- 列表和属性窗口对照用户提供的两张原版截图。列表尺寸、字距及滚动交互为本项目适配，未宣称 D2Client 逐像素／逐帧等价；参考仓库和抽取素材不提交。

## 已接入

- 当前世界为第一幕，Kashya 在角色达到 8 级或已领取 Blood Raven 奖励后显示 Hire。候选从原姓名范围选择十个不重复名字，按难度及角色等级派生列表数值；同一次游戏内关闭列表不会重新抽取。
- 选择候选走正式会话命令，先扣钱包、不足再扣私人箱金币。更换佣兵按参考规则移除原佣兵及其装备。Kashya 免费任务奖励复用同一候选与分配逻辑；已有佣兵不替换。
- O 打开左侧属性面板；I 可打开右侧背包。支持头盔、衣甲及原 WType 允许的弓，双手弓在另一手框作镜像显示。拖放装备／卸装共用物品事务、版本、需求和容量校验；物品从佣兵拖回背包或丢到地面也走正式命令。
- 属性面板与攻击共用派生结果，包含原基础成长、装备属性、物理伤害、命中、防御和四抗；佣兵不会获得玩家背包护符的效果。普通射箭可使用装备元素伤害、致命攻击、压碎和撕裂。
- 佣兵生命恢复、击杀经验、升级和原分段成长行已接入。单次经验上限为当前等级区间的 1/64，主人击杀使用该奖励的 86/256，达到主人等级后停止增长。
- 身份、等级、经验、生命和独立装备容器进入角色存档；路线、攻击计时和 Hire 候选不跨局保存。v88 及更早格式拒绝载入，不静默迁移。

## 命名管道

快速得到一名当前难度的罗格佣兵：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command grant-hireling
```

`grant_hireling` 是同一命令的别名。无需任务、金币或站到 NPC 身旁；按当前角色等级及 MPQ 数据生成。默认打开 O 面板，`-Arguments @{open=$false}` 只领取。重复执行保留已有佣兵及装备，返回 `created=false`。角色死亡时拒绝。授予走游戏命令并立即处理，不推进世界时间；不会写入 Blood Raven 任务完成状态。

辅助入口见 [命名管道](DEBUG_PIPE.md)：`hireling` 查询、`hireling-panel` 开关面板、`hireling-equip` 装卸。返回的属性与 O 面板共用数据，响应内有装备实例 ID 和原部位代码。

## 未完成

- Fire Arrow／Cold Arrow 目前是原类型和列表描述，技能选择、技能等级、施法及 Inner Sight 尚未接入；战斗仍是已有的普通远程箭和跟随。
- 敌人选择佣兵为目标、佣兵受击／死亡动作、复活收费服务、喝药和完整防御效果尚未接入。当前不应据面板抗性判断其承伤流程已完成。
- 佣兵吸血、装备 MF／金币加成叠加给掉落、速度对应原动作帧、触发技能、光环以及完整物品效果尚未完成。元素和部分命中特效已接，不等于全部属性有效。
- 其他幕世界与雇佣流程尚未实现；只读取其资料片定义，不开放无法使用的入口。该实现没有经典版模式或资源回退。

## 代码入口

- `content/hireling_data.*`：MPQ 类型化定义、原装备框和候选／等级派生。
- `gameplay/npc/hireling_services.cpp`：雇佣、装备、经验和调试授予；`hireling.cpp`：任务奖励与跟随／普通远程攻击。
- `presentation/hireling_view.cpp`、`hireling_panel.hpp`：两个窗口；输入由现有 controller 提交命令。
- `app/debug_hireling.cpp`：JSON 管道适配；玩法模块不依赖 Win32。
