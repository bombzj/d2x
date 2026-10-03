# 会话与权威宿主基线

更新：2026-10-03。记录当前源码事实；后续阶段见 [技术改造方案](../TECHNICAL_REFACTOR_PLAN.md)。

## 当前边界（P0）

- [`GameSession`](../../src/gameplay/session/session.hpp) 是不透明、不可复制的兼容外观，唯一拥有 `unique_ptr<GameSessionImpl>`；构造、析构和查询转发在 [`session_facade.cpp`](../../src/gameplay/session/session_facade.cpp)。公开成员不再内联读取私有世界／内容状态。
- [`GameSessionImpl`](../../src/gameplay/session/session_impl.hpp) 拥有原会话成员：ID／随机流、内容目录、区域／地图缓存、模拟、库存、掉落及临时交互请求。只由会话／NPC 实现和外观转发包含，不作为 UI 入口。
- `session.cpp`、各 `session_*.cpp` 及 NPC 会话成员实现迁到 `GameSessionImpl`，函数逻辑、成员顺序、构造初始化、固定步与随机消费顺序保持。模拟／库存原有朋友访问暂时由该私有实现承担。
- [`session_views.hpp`](../../src/gameplay/session/session_views.hpp) 保存旧接口使用的祭坛、NPC 对话、佣兵属性和传送门值类型；传送门仍可通过 `GameSession::PortalView` 使用。它不是新的网络协议。
- `d2x_session` 增加外观转发源文件，库依赖方向不变。消费者需要完整内容、区域、库存或角色保存值时，在实现中显式包含对应头，不再依赖会话传递包含。

## 状态与生命周期

`app → GameSession → GameSessionImpl → Simulation / InventoryService / 区域`。外观与私有实现同时存在，不能复制或替换私有实现；已注入的回调仍指向稳定的实现对象。区域与模拟的借用关系、销毁顺序、存档恢复和死亡结算继续沿用现有实现。

公开 `state()`、`content()`、`inventory()`、区域查询和旧命令仍保留，返回同一权威对象的只读引用。这是迁移中的兼容边界；P1 已开始通过本地适配投影移动／受控人物显示，具体范围见 [客户端基线](CLIENT.md)，不能据此宣称多人或独立服务端已经实现；已实现技能的通用执行边界另见 [技能基线](SKILL_RUNTIME.md)。

P0 降低私有布局与函数声明变化向消费者传播；共享命令／事件／值类型及完整内容查询仍有编译依赖。未测量增量编译耗时，不报告性能提升百分比。

## 技能来源服务切片（P5 S2）

`GameSessionImpl` 拥有 `UnitSkillSources`，在 [`session_skill_sources.cpp`](../../src/gameplay/session/session_skill_sources.cpp) 配置本地玩家提供函数与模拟器求值回调。玩家起手及 `resolveUnitSkill_` 的后续／反应求值按单位 ID 走同一服务；提供函数每次读取当前记录及原加成，不缓存起手数值或长期借用人物状态。服务公共头不包含会话、模拟器、人物或怪物。

现有玩家与会话同寿命，恢复替换记录仍使用本局 ID；独立来源移除必须先注销绑定。怪物特殊飞弹保留显式输入；火专精查询改为显式单位 ID，未知单位拒绝。会话准备当前玩家资格／来源后调用独立技能运行器；动作／资源能力由模拟器适配短期借用原状态，技能不读完整会话。装备充能及多玩家所有权仍未实现；此前技能迁移通过 Windows Release 与代表性冒烟。本批光环／诅咒资格和 ColdEffect 桥改用内部 `RuntimeCombatUnit`，具体记录不进入技能端口；本批已通过 Windows Release 与简单冒烟，准确边界见 [通用技能基线](SKILL_RUNTIME.md)和[单位基线](UNITS.md)。

## 保存与验证

原 D2S v96 编码及保存语义不变；没有增加私有字段、网络序列化或迁移旧档。原 MPQ、用户存档和旧产物保留。

P0 已完成 Windows Release 构建，游戏与资源工具均链接成功。修复声明整理、内部 include、朋友访问及消费者任务阶段／内容类型的显式包含；未改变玩法逻辑。

既有游戏入口冒烟：普通 Sorceress／seed210 临时角色在营地从 `(153.5,68.5)` 移动到 `(155.5,68.5)`，库存查询 7 件、物件查询 42 项、技能查询成功；旅行到区域40后保存／恢复成功，角色仍一级、40生命／35法力、7件物品。独立进程从临时原 D2S 加载区域40、运行两帧并截图，退出0、stderr空，截图已查看。两实例均已退出，未读写用户角色档；产物在忽略的 `artifacts/refactor-p0-20261003/`，构建日志为 `artifacts/refactor-p0-build.log`。

未新增测试脚本、用例或专用程序。上述范围不认证全部战斗、NPC服务、装备事务或多人；其余 P1 投影及领域拆分尚待逐项实施。
