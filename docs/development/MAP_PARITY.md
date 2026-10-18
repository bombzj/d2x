# 地图对照与房间恢复排查

核对结果见[五幕地图](../gameplay/world/MAPS.md#对照与验证边界)，历史样本见[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)，EXE身份见[构建记录](BUILD_AND_RUN.md#运行包记录)。

## 已恢复的DLL导出与独立重放

旧会话记录已找回，旧原始报告及EXE未找到。本轮重建既有reference/d2mapapi_mod和d2x_assets，新导出、重放结果、源码摘要及构建日志位于`artifacts/map-parity-20261017-01`。

原版导出器使用Win32：

```powershell
cmake -S reference/d2mapapi_mod -B <新证据目录>/oracle-build32 -G "Visual Studio 17 2022" -A Win32 -DUSE_STATIC_CRT=ON
cmake --build <新证据目录>/oracle-build32 --config Release --target d2mapapi_piped
```

产物在bin/Release，以原版完整安装目录为唯一路径参数，每组新建进程。stdout先返回4字节有符号初始化状态；stdin请求为小端uint32的seed／difficulty／level，stdout返回4字节JSON长度及对应字节。

默认使用历史遍历顺序。D2MAP_REPLAY环境变量可指定事件JSON，顶层含id／seed／difficulty及native.events；每事件含op／level／x／y，坐标为tile房间锚点。

| 事件／选项 | 含义 |
| --- | --- |
| reveal／hide | 调用原AddRoomData／RemoveRoomData |
| stream | 原DLL以房间中心调用StreamRoomAtCoords；D2X宿主会话初始化同一目的房间，无引用变更 |
| enter／change／leave | enter先初始化再ChangeClientRoom；change只切换已活动目的房间；leave将该client上一房间改为空。client为0–7，默认0 |
| retire | 原DLL调用1.13c ordinal10150／RVA3CA20的RemoveRoomFromAct；D2X释放同一房间。用于已记录的实际释放或显式DRLG控制，不代表模拟了D2Game单位压缩 |
| tick／frame | tick显式调用DRLG_Update；frame为D2X宿主会话推进GAME_Update卸载检查。frames最多250；原导出器不模拟完整游戏帧 |
| sample／observe | 前者读取活动房间；后者也可读取未活动状态，不隐式激活 |
| state=true | 导出loaded、collisionActive、逻辑近邻、tileRandom及activeRandom；activeRandom为未消费玩法随机的活动房间初值 |
| ownedOnly=true | 保留全部近邻身份／顺序／随机和完整碰撞，仅本房间输出瓦片／单位／Pops明细，其他近邻标记details=false；全区采样逐房覆盖明细 |
| phase | 证据标签 |
| D2MAP_TRACE | 每步执行前刷新轨迹文件，便于定位原DLL中断 |

D2MAP_SIDE=server令LoadAct使用bClient=0和空客户端回调。reveal／hide仍走视野入口；其他事件也只重放明确的DRLG操作，不模拟D2Game人口。真实1.13c D2Game不导入10715，实机记录的Update均来自客户端；服务端轨迹不得凭7 tick分支自行补入预取。当前导出器补丁／二进制及运行证据位于`artifacts/map-room-runtime-20261017-01`，旧批次EXE另行保留。

D2X使用既有资源命令：

```powershell
& .\build\bin\d2x_assets.exe .\assets\mpq2 native-map <level> <seed> <difficulty> <原版导出.json>
& .\build\bin\d2x_assets.exe .\assets\mpq2 native-map <level> <seed> <difficulty> prepared
```

JSON默认沿用客户端事件重放；native.mode="server"选择与权威world相同的纯值房间会话和服务端引用状态机，接受上表事件。区域输入按需合并，不重新初始化已有房间。输出creations记录每次实际创建后的瓦片随机／活动初值，retirements记录实际释放。prepared现在只验证资源准备，地形全为0xffff、materializedRooms=0；不再输出冻结初值。complete仍是离线地图投影，不能认证宿主实际时序。

宿主模式比较尺寸、房间／事件／近邻有序列表、DT1路径及记录、全部16位碰撞与两条房间流；完整单位／Pops明细仍由既有客户端模式导出，不能据宿主地形对照宣称单位恢复通过。列表长度单独核对，不排序。沿用[历史状态位掩码](../architecture/MULTIPLAYER.md#原版-dll-对照方法)，DT1路径统一大小写／分隔符；原导出缺省的outdoorFlags／auxiliary／themeMask补零，非零值保留比较。

## 真实EXE排查

1. 记录实际EXE／DLL摘要、挂载MPQ及优先级、角色／任务状态、人数、难度和本局seed。使用隔离角色副本；原版保存目录需单独确认。D2X没有产品--map-seed入口，跨局须先核对实际seed。
2. 先D2X Host／原版Join，再反向Host／Join。同局比较同一全局坐标的道路、桥、墙、门洞、楼梯和warp；从两侧走过狭口，并观察攻击／飞弹阻挡及服务器位置校正。
3. 选邻房A／B，分别观察A卸载而B保留、两房都卸载、交换返回次序及重复重入；另让一名玩家留在A作引用对照。D2X须看到loaded=true→false→true及重建后的creationOrder；07／08或离屏只表示客户端视野变化。原版服务端需可靠的只读观察确认实际释放。
4. 在合法任务进度下开门／开墓墙后重复回访；核对静态地形、动态物件、单位恢复及后续随机分别是否变化。优先覆盖野外接缝、迷宫共享墙、虫穴／古墓、第三幕丛林、第五幕大区域，再扩展种子和难度。

已执行的最小原版实例为`Game.exe -w -ns -seed 1`，隔离角色经原UI进入4区并返回。该参数仅用于原版诊断，D2X无对应产品入口。以已校验的1.13c地址记录LoadAct、视野引用、StreamRoomAtCoords、ChangeClientRoom、DRLG_Update、InitRoom及目标房间GetTileCache／共享砖查找；同时按Act+0x50区分服务端0／客户端1，Act+0x0c核对实际seed。释放须同时看到Room2+0x30的Room1为空及+0x28的HAS_ROOM（0x100000）清除，回访再记录InitRoom，不能以08或离屏代替。

采样同时保留原始16位碰撞和静态投影。静态投影取实际原生DT1记录的25格碰撞及影响碰撞的tile flags 0x02／0x40／0x80，按活动near顺序合并；不以D2X生成值充当原版预期。随后分别重放实际07／08序列、补入实际Update序列、执行prepared，用第一个不同的共享砖命中／选砖参数／随机定位原因。实机结果、动态占位区别及证据路径统一见[实机首差](../gameplay/world/MAPS.md#实机首差与合成调用边界)。

从仓库根启动D2X Host，先替换占位路径并建立新证据目录：

```powershell
& .\build\bin\d2x.exe --mpq .\assets\mpq2 --host-saves '<隔离角色目录>' --host-lan 127.0.0.1 --debug-pipe map-host --game-packet-log '<新证据目录>\client-packets.jsonl' --host-packet-log '<新证据目录>\host-packets.jsonl'
```

通过UI选角建局，原版TCP/IP Join同一地址。跨机使用实际网卡地址。原版直连端口为4000，已有占用时另安排环境。反向连接用--lan <原版HostIPv4>替换--host-lan，另取管道名和日志；此时D2X无权威server-snapshot。

## 诊断入口与限制

以下变量先从本局记录取得：target／sample为全局subtile坐标；两个since首次为0，之后取上次序号；capturePath为新截图路径。

```powershell
.\scripts\Send-D2XCommand.ps1 -PipeName map-host -Command server-status
.\scripts\Send-D2XCommand.ps1 -PipeName map-host -Command server-snapshot -Arguments @{limit=256;x=$targetX;y=$targetY}
.\scripts\Send-D2XCommand.ps1 -PipeName map-host -Command online-world -Arguments @{fromX=$sampleX;fromY=$sampleY;x=$targetX;y=$targetY;radius=24}
.\scripts\Send-D2XCommand.ps1 -PipeName map-host -Command server-events -Arguments @{since=$factSince;commandSince=$commandSince;limit=256}
.\scripts\Send-D2XCommand.ps1 -PipeName map-host -Command screenshot -Arguments @{path=$capturePath}
```

| 入口 | 观察范围／限制 |
| --- | --- |
| server-snapshot | generationRandom.rooms含loaded、revision、creationOrder及low／high；碰撞窗口固定以权威人物为中心49×49，x／y只指定寻路目标 |
| online-world | 可选采样中心，但必须已物化；navigation.mask为运行movementMask，须与对象／动态占位分开，不能当原始16位静态碰撞。collisionVerified／layoutMatched只校验本地锚点及重建成功 |
| server-events | 事实环1024、命令环512、单次最多256；gap或截断时补采 |
| 原协议日志 | 按connection／gameGeneration及sequence对齐；LOADACT、07／08没有完整服务端碰撞、房间流或卸载状态 |

## 待核对项

真实1.13c单机已取得服务端初始化／ChangeClientRoom、两端房间实际释放／重建和出生随机证据，宿主已改为实际逐房间物化；包内双D2X客户端已有限核对不同路线及留守／释放。原版混合客户端、更多种子／交错、完整单位压缩／恢复及整级资源回收仍须单独核对。本专题既有地址已按实际DLL校验；后续规则与调用链优先读D2MOO，发现版本差异后再定点核对。

发现差异时保留第一个不同的区域／房间／全局格、事件／帧、DT1记录、静态与动态碰撞层及两条房间流。先核对资源、种子、阶段和引用状态，再决定是否修改生成、缓存或恢复机制。
