# 自动地图

五幕地形批次修订（2026-10-02）：按D2MOO原LevelType名称表匹配Automap.LevelName，修正第五幕冰洞名称不一致；专用城镇图沿LvlPrest.AutoMap条件载入act2map(s)／act4map(s)／extnmap(s)，鲁高因按LutW／LutN变体选择原图组，不自画建筑轮廓。鲁高因每组5×4行优先拼接有点阵接缝统计支持，关卡中心锚点和精确缩放仍未认证。中立可交互NPC优先用MonStats2.automapCel，缺失时适配原蓝十字317，原资源帧不等于身份映射已核实。Release普通seed210鲁高因截图已显示建筑轮廓，实例退出0、stderr空，产物在artifacts/maps-priority-smoke-20261002/act2-automap.png。精确城镇对齐、NPC宽十字外观及人物标记列入地图暂缓难点，不宣称全面复刻完成。

原先的角落地图从碰撞格采样单个边缘像素，显示范围、线条与标记均不是原版资源。本轮改为在运行时从原 MPQ 读取 `LvlTypes.txt`、`Automap.txt`、`Objects.txt`，按区域类型和 DS1 格子的方向、style、sequence 选择原自动地图 cel，不再要求该格先匹配到 DT1 图形瓦片；大图和角落图分别绘制 `maximap.dc6` 与 `maximaps.dc6`。边缘可见性按解码后 cel 的实际纹理范围裁剪，不再使用固定的 32 像素容差。普通怪物不再作为红点暴露在地图上，原表具有 `AutoMap` cel 的物件才绘制标记。

`src/content/world/automap_data.*` 负责类型化原表规则；`src/presentation/world/automap_assets.cpp` 为每张地图准备图块且只上传实际使用的帧；`src/presentation/world/automap_view.cpp` 管理已探索房间与透明叠加绘制。当前大小图对 DC6 帧额外按宽高修正偏移并对齐整数像素，这不是已核实的原客户端基点规则；方向 3 墙角补方向 4，同格相同 cel 去重。探索复用 `RoomLayout`，包括固定场景的房间回退及共享边缘。沿无缝边界连接的已探索区域按 `worldX/worldY` 绘制，出城不再丢弃营地图块；洞口、楼梯和传送门不连接地图层。探索只在当前游戏视图中维持，读档重置，不改变存档格式。用户已指出当前绘制与原游戏有较大差别；读取原 DC6 不等于绘制复刻完成，基点、缩放、小图视口及人物标记仍待核对。本轮按用户授权改用常规alpha混合实现近似Fade，移除原加法混合及大小图固定alpha225／245，不宣称原客户端调色板混合等价。

2026-09-27 墙线出现过晚的修订：`revealAutomap()` 按观察者房间点亮当前及邻近房间，大小图共用探索状态；连续户外边界两侧的房间按世界偏移统一比较，不用当前 LevelId 截断墙线。`RoomLayout::nearRooms(RoomBounds)` 使用 D2MOO `DrlgDrlgRoom.cpp::sub_6FD77BB0` 的每轴边界间隔小于 6 地图格（30 子格）；共享墙格一并纳入。依据链进一步核对 `Clients.cpp::sub_6FC33020` 将客户端加入新邻房，`Level.cpp::LEVEL_AddClient` 随即发 `SCmd.cpp::D2GAME_PACKETS_SendPacket0x07_6FC3D120`；本地 OpenD2 `Shared/D2Packets.hpp` 将 0x07 标为 `D2SPACKET_MAPREVEAL`。按用户确认，本项以参考项目依据为验收标准，不要求与 1.13c 的精确揭示时机一致；上述邻房到地图揭示包的调用链满足本次依据要求。当前按现有 DRLG 房间显示整间邻房及其原物件标记，跨区范围以项目房间几何适配，不声称原客户端逐格等价。楼梯／传送门不连接地图层，不新增像素距离或自造墙线。本轮未构建、运行、测试或打包。

默认关闭地图，`Tab` 开关、`V` 交换小图左右、方向键平移已打开的地图、`Home` 居中、`F12` 开关名称；截图使用项目调试键 `Ctrl+F12`。无原版依据的 `Shift+Tab` 尺寸切换及 `automap-size` 调试入口已从源码删除，大小图通过原 Options 菜单选择。`V` 左右切换由官方 [Controls](https://classic.battle.net/diablo2exp/basics/controls.shtml) 的 Automap Options 段确认；`Home` 和 `F12` 分别见 OpenDiablo2 `key_map.go::ResetToDefault` 的 CenterAutomap／ToggleNamesOnAutomap。用户于2026-10-02确认方向键只平移绘制内容，相当于移动地图摄像头，不移动地图的屏幕显示区域；当前偏移只作用于内容坐标。Center When Cleared为Yes时按两下Tab关闭后重开居中，No时不居中。当前代码重开时按该设置清除或保留偏移，与实测一致。侧栏打开后限制在剩余场景范围，这是现有布局，尚未确认原版视口规则。Esc → Options → Automap Options已接原菜单图块，尺寸Full Screen/Mini Map共用原大小图。Show Names沿现有名称绘制；Show Party保存其他玩家队伍开关，当前单玩家世界没有队员实体，不把佣兵／召唤物当作其他玩家。设置写客户端client-settings.json，不写D2S。调试 `status.automap`包含fadeSupported=true、fadeApproximate=true、fade、centerWhenCleared、party与optionsPage。

Fade实测（用户，2026-10-02）：小图有No／Everything／Auto；No较亮，Everything使建筑等地图内容更透明，NPC和人物不淡化，目测约一半。Auto更接近Everything，移动不产生变化。大图额外有Center，只让屏幕中间区域更透明。此前Auto／On／Off命名有误，已更正。

按用户授权的近似实现：菜单使用当前MPQ的no／everything／auto／center原DC6，小图循环No→Everything→Auto，大图额外循环Center。No使用alpha255；Everything及Auto使用alpha128淡化地形和物件，NPC／人物及名称不淡化。Auto默认启用且暂与Everything相同，不随移动改变。Center固定为当前地图显示区域中央半宽、半高矩形，区内alpha128、区外255，跨边界图块按像素矩形分割绘制，不整块跳变，不编造渐变；方向键不移动淡化区域。Center切换至小图时转为Everything。配置新增字符串automapFade=no／everything／auto／center，缺省及非法值回退auto，小图读到center也转everything。强度、Center范围、模式循环顺序及切换回退是可用适配，不是原客户端精确认证，仍待用户画面对照。

联网核对（2026-10-02）：[D2MOO README](https://github.com/ThePhrozenKeep/D2MOO#why-are-some-dlls-missing-)明确说明逆向工作主要集中在D2Common／D2Game，并依赖原游戏程序和DLL；核对上游source目录及本地版本均没有D2Client模块。地图揭示／Automap表匹配不等于客户端Fade绘制已实现。D2Gfx的DrawMode.h提供TRANS25／TRANS50／TRANS75等混合类型，但未找到自动地图模式到这些类型的调用链，不能用枚举名称推定Everything的混合系数。[Phrozen Keep技术讨论](https://d2mods.info/forum/viewtopic.php?t=59846)提到原图标的点阵／缺像素样式与选项透明混合是两件事，未给出Auto／Center算法。[经典版Auto讨论](https://www.reddit.com/r/diablo2/comments/wixr98/d2_classic_what_does_automap_fade_auto_do/)搜索摘要中作者观察Auto与不淡化相近，与本次小图实测不同；正文请求被拦截，该摘要只作待核对线索，不作为规则。下一步原版实测应在同一场景、同一位置比较大小图各模式，并观察站立／移动、明暗场景、方向键平移及开关侧栏时Auto和Center是否变化；这些是区分假设的检查，不是已确认的触发条件。

2026-10-02菜单依据：当前MPQ的automapoptions、automapmode、automapfade、automapcenter、automapparty、automappartynames、full/mini及Fade原DC6；OpenDiablo2 escape_menu.go提供菜单层级、循环选值、Previous Menu和默认返回行、Esc关闭行为；game_event.go说明淡化不影响玩家/NPC标记、居中是重新打开时居中。其onUpdateValue只输出日志，Fade值仅Yes/No占位；本地map renderer没有可复用的Fade消费者，因此本次按用户实测及授权近似接入。Sound/Video/Configure Controls仍拒绝未实现页面，多人队伍位置绘制未实现。

验证范围：此前Release包普通Sorceress／seed210隔离实例验证Options→Automap Options入口、四项开关状态、菜单暂停及配置落盘；管道采用悬停加Enter，没有鼠标松键注入，真实点击未覆盖。该旧实例的Fade仍未实现，不能作为本轮绘制验证。实例退出0，无角色存档读写或新增测试程序，产物在artifacts/automap-options-smoke-20261002。本轮近似Fade、快捷键删除及兵营方向修复已通过Windows Release链接和静态诊断；不继续游戏冒烟，实际绘制／模式切换／配置重启读取及用户存档加载待验收。按本轮要求更新运行包并提交相关源码与文档，不包含MPQ、存档或产物。

NPC 与 Stash 名称受名称开关控制；NPC 通过 `MonStats.MonStatsEx` 查询 `MonStats2.automapCel`，不再用 MonStats 编号查询 Objects。当前 MPQ 城镇 NPC 没有非零automapCel，交互中立NPC现采用原317适配；Objects的Portal59／60 AutoMap仍为0。D2MOO `D2C_AutomapCells` 已确认红十字221／蓝十字317，但身份到帧号的完整客户端映射尚未取得。人物暂留旧自画十字，城镇NPC精确宽十字及动态传送门标记仍待对照；旧实现不是验收标准。

依据包括 D2MOO `LevelsTbls.cpp` 的 Automap 表匹配及 `D2Constants.h`，OpenDiablo2 的 `monster_stats2_record.go`、`game_event.go` 和 `key_map.go`。补充核对 D2BS `f4b99bbe8de6916384991dfdd198ecf234cef1c0` 的 `D2Helpers.cpp::ScreenToAutomap` 等轴坐标公式；未移植其代码。原引擎 cel 变体使用共享随机流，当前按瓦片坐标稳定选择；精确基点、叠加透明度与标记仍待原版对照。

2026-09-26：Windows 游戏构建及修改文件编译通过，已启动原 MPQ 暂停实例并截图，确认营地步行进入血腥荒地、地图平移不移动角色。跨区域层诊断最后修改只做编译；洞穴隔离、返回营地及最终独立打包尚未验收。按用户要求停止后续核对并提交源码，不宣称完整原版一致或最终验收完成。
