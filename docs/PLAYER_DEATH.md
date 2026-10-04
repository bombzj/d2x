# 玩家死亡与尸体

更新：2026-10-04。本批为源码实现；尚未构建、运行、打包或进行原档往返验收，当前发行包仍是此前版本。

## 当前行为

- 生命耗尽后结束玩家动作、清除死亡时应移除的效果，播放当前 MPQ 的裸身 `DT` 动画。动作帧与 Esc 可复活的时间读取 `AnimData.d2`，不再固定为每秒 20 帧。画面显示死亡／Esc 提示；怪物失去活目标后清除路线与攻击动作，原地待机。仍有可攻击的佣兵、其他活单位或诅咒目标时，AI 继续运行，不冻结整个世界。
- 单独建立玩家尸体；装备的原实例转入封闭容器，包括腰带、首饰和两套武器。鼠标上的物品放入尸体的储存位。背包、私人箱和方块物品保留；腰带首行四格保留，多余药水尝试放背包，再寻找合法地面位置。普通移动／消费命令不能访问尸体容器。
- Esc 等待死亡动画结束，在当前幕城镇满生命、魔法和耐力复活。最大值按失去装备后的属性重算。死亡区域保留怪物、掉落和尸体，不重新生成地图／人口。
- 宠物处理发生在玩家 `DT` 动画结束、进入 `DEAD` 状态时，沿 `PlrModes.sub_6FC81250`／`PLRMODE_StartXY_Dead` 调用 `PlayerPets.D2GAME_KillPlayerPets_6FC7CD10` 的阶段。当前 MPQ 的帧数／速率决定延迟，不写死一秒，也不等待 Esc。届时统一杀死仍存活的本人召唤物，结束其动作并播放已有原死亡动画；佣兵保留资料／装备和原 D2S 死亡位，回城后仍须走复活服务。召唤物死亡不产生经验、掉落或可利用尸体，死亡动画随时间结束。
- 原版遍历 `PetType.txt` 的全部宠物类型，而非只处理佣兵。当前按所属角色统一处理已注册召唤物，包括已有骷髅及九头海蛇；后续女武神、骷髅法师、石魔等接入同一生命周期即可，不增加技能名称判断。这不表示尚未实现的召唤技能已经完成。玩家死亡动画期间，宠物不会因主人生命归零或脱装导致技能加成下降而提前强制消失，仍可能正常战斗、受伤死亡或寿命到期；佣兵装备不放入玩家尸体。
- 点击自己的尸体提交回收命令；远处点击会寻路靠近。距离沿 `PlrMsg.sub_6FC828D0` 的原单位距离算法：超过 50 拒绝，距离不大于 8 可以回收。尸体不加入战斗目标或怪物尸体技能列表。
- 回收优先尝试原装备位及对应的另一戒指／手位，复用职业、属性、等级与双手组合限制，不替换当前穿戴的其他装备。获得属性装备后重试剩余装备；仍无法穿戴的物品尝试普通收集，复用自动腰带、书本和堆叠规则。放不下的剩余物品继续归尸体所有，只有全部取回后才删除尸体。
- 单局最多 16 个尸体；继续死亡时，装备和鼠标物品寻找合法地面位置，不覆盖旧尸体。没有合法落点或修订号耗尽导致脱装事务失败时，保留原物品并明确报错；不删除装备或制造替代位置。死亡金币找不到地面位置时，剩余部分保留钱包并提示。

## 普通单机死亡损失

依据 `PLAYER_ApplyDeathPenalty`：金币损失按随身与银行总数乘以 `min(等级,20)%`，单机免计前 `等级×500` 金币，实际罚金不超过钱包，银行不扣。扣罚后的随身金币使用当前 MPQ 金币定义／堆叠上限寻找地面落点。

经验罚率读取当前 MPQ `DifficultyLevels.DeathExpPenalty`；损失以当前等级区间为基数，最多扣到当前等级下界，不降级。本局首次点击自己的尸体返还记录损失的 75%，即使还有物品放不下也只返还一次。离开游戏后该经验返还资格消失。尚未实现 PvP、专家模式或 realm 游戏类型，不把对应规则混入普通单机流程。

## 原生 D2S

格式继续为 v96。角色物品段后的原 `JM` 尸体计数支持 0／1；有尸体时包含原 12 字节头及嵌套 `JM` 物品段。未知头字段保留，坐标写原字段，装备仍用原 body location，鼠标物品用尸体 backpack page。无私有尸体尾段或格式迁移。

普通单机沿 `PLRSAVE2_WriteCorpsesSection`：按创建顺序保存最早仍有物品的尸体，其他尸体不会合并进这一尸体。重进游戏在当前幕城镇出生点建立该尸体，重新分配运行 ID，经验返还清零。死亡时直接退出的角色按无装备后的最大资源复活；已活着退出的角色保留正常资源保存语义。既有无尸体的 D2S 正常读取，不根据旧角色生命值猜测或重新分配装备。

目前尸体容器表达 12 个装备位和一个死亡鼠标物品储存位；原档含其他尸体储存布局、多尸体段或既有不支持物品类型时明确拒绝，不丢弃无法解析的物品。装备、存档完整兼容边界仍见 [物品](ITEM_COMPLETION.md) 和 [保存](SAVES.md)。

## 入口与分工

| 入口 | 分工 |
| --- | --- |
| `gameplay/player/corpse.hpp` | 尸体身份、所属角色、区域／位置及本局返还经验；不拥有第二份物品 |
| `gameplay/items/corpse_inventory.cpp` | 脱装草稿事务与回收规则复用；普通 UI 不得访问封闭容器 |
| `gameplay/session/session_player_corpses.cpp` | 死亡事件结算、损失、复活、寻路／回收与尸体生命周期 |
| `content/character/death_data.*` | 当前 MPQ 死亡时序和三难度经验罚率适配 |
| `session_character_save.cpp` / `session_restore.cpp` | 选择保存尸体、校验、ID 重映射及城镇新局重建 |
| `persistence/d2s_codec.cpp` / `d2s_inventory.cpp` | 原尸体段和物品位置编码；不执行玩法 |
| `presentation/world/world_renderer.cpp` / `controller.cpp` | 只读尸体绘制／命中及命令提交 |

尸体图像优先读取原 `DD` COF/DCC；当前 MPQ 缺完整 `DD` 时保留原 `DT` 最后一帧，不绘制自造尸体。死亡提示复用现有 MPQ 字体与用户提供截图中的文字；原客户端死亡提示的具体贴图／字体布局、尸体专用提示音尚未核实，不宣称像素或音效等价。

运行规则指纹增加 `player-corpse-rules-v1`，保留 `quest-rules-v18-later-acts`；D2S 不保存指纹。此次没有编写测试程序，也没有继续构建、运行检查或打包。多次死亡、满背包部分回收、属性依赖装备与原生尸体存档往返待用户授权后冒烟。

## 依据

本地 D2MOO 固定版本与许可见 [资料来源](THIRD_PARTY.md)：`PLAYER/PlrModes.cpp::D2GAME_CORPSE_Handler_6FC7FBD0`／`sub_6FC80440`／`sub_6FC81250`／`PLRMODE_StartXY_Dead`，`PLAYER/PlayerPets.cpp::D2GAME_KillPlayerPets_6FC7CD10`／`sub_6FC7CDC0`／`sub_6FC7CF50`，`ITEMS/ItemMode.cpp::sub_6FC4AD80`／`sub_6FC45930`，`PLAYER/PlrMsg.cpp::sub_6FC828D0`／`D2GAME_PACKETCALLBACK_Rcv0x41_Resurrect_6FC87480`，`PLAYER/Player.cpp::PLAYER_ApplyDeathPenalty`，`PLAYER/PlrSave2.cpp::PLRSAVE2_WriteCorpsesSection`／`PLRSAVE2_ReadCorpses`，`D2Common/Units/Units.cpp::D2Common_10399`。`PlrModes.D2GAME_EVENTS_Callback_6FC81BD0` 将 `EVENTTYPE_ENDANIM` 分派到 `sub_6FC81250`；`PlayerPets` 遍历所有类型，非佣兵注销宠物记录并杀死单位，佣兵标记死亡并保留可复活记录。保留参考用途与 MIT 许可说明，不提交参考仓库或资源。

官方[角色与死亡规则](https://classic.battle.net/diablo2exp/basics/characters.shtml)、[技能常见问题](https://classic.battle.net/diablo2exp/faq/skills.shtml)和[经验规则](https://classic.battle.net/diablo2exp/basics/experience.shtml)交叉确认 Esc 回城、装备回收、多个尸体及同局经验返还；realm 的最高金币价值尸体选择与单机最早非空尸体不同。
