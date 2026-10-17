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
| sample／observe | 前者读取活动房间；后者也可读取未活动状态，不隐式激活 |
| state=true | 导出loaded、collisionActive、逻辑近邻、tileRandom及activeRandom；activeRandom为未消费玩法随机的活动房间初值 |
| ownedOnly=true | 保留全部近邻身份／顺序／随机和完整碰撞，仅本房间输出瓦片／单位／Pops明细，其他近邻标记details=false；全区采样逐房覆盖明细 |
| phase | 证据标签 |
| D2MAP_TRACE | 每步执行前刷新轨迹文件，便于定位原DLL中断 |

D2MAP_SIDE=server仅令LoadAct使用bClient=0和空客户端回调，后续仍调用AddRoomData；它不模拟真实D2Game创建、人口或延后释放，不能用作服务端生命周期验收。

D2X使用既有资源命令：

```powershell
& .\build\bin\d2x_assets.exe .\assets\mpq2 native-map <level> <seed> <difficulty> <原版导出.json>
& .\build\bin\d2x_assets.exe .\assets\mpq2 native-map <level> <seed> <difficulty> prepared
```

JSON模式只读取原版事件，独立生成地形；prepared走实际AreaGenerator准备路径，输出冻结碰撞及initialRoomRandom。complete使用另一条遍历顺序，不能替代prepared。

比较原点／尺寸、房间／事件／近邻有序列表、DT1路径及记录、单位／Pops、全部16位碰撞与两条房间流。列表长度单独核对，不排序。沿用[历史状态位掩码](../architecture/MULTIPLAYER.md#原版-dll-对照方法)，DT1路径统一大小写／分隔符；原导出缺省的outdoorFlags／auxiliary／themeMask补零，非零值保留比较。

## 真实EXE排查

1. 记录实际EXE／DLL摘要、挂载MPQ及优先级、角色／任务状态、人数、难度和本局seed。使用隔离角色副本；原版保存目录需单独确认。D2X没有产品--map-seed入口，跨局须先核对实际seed。
2. 先D2X Host／原版Join，再反向Host／Join。同局比较同一全局坐标的道路、桥、墙、门洞、楼梯和warp；从两侧走过狭口，并观察攻击／飞弹阻挡及服务器位置校正。
3. 选邻房A／B，分别观察A卸载而B保留、两房都卸载、交换返回次序及重复重入；另让一名玩家留在A作引用对照。D2X须看到loaded=true→false→true及重建后的creationOrder；07／08或离屏只表示客户端视野变化。原版服务端需可靠的只读观察确认实际释放。
4. 在合法任务进度下开门／开墓墙后重复回访；核对静态地形、动态物件、单位恢复及后续随机分别是否变化。优先覆盖野外接缝、迷宫共享墙、虫穴／古墓、第三幕丛林、第五幕大区域，再扩展种子和难度。

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

真实1.13c服务端的ChangeClientRoom、活动房间创建／删除、延后FreeRoom及单位压缩／恢复尚未完整取证。观察须区分服务端／客户端Act，地址按实际DLL校验；D2MOO 1.10f用于定位入口。

发现差异时保留第一个不同的区域／房间／全局格、事件／帧、DT1记录、静态与动态碰撞层及两条房间流。先核对资源、种子、阶段和引用状态，再决定是否修改生成、缓存或恢复机制。
