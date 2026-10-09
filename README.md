# D2X — Diablo II: Lord of Destruction C++

基于1.13c资料片原MPQ的C++20客户端和自研权威服务端。Single Player、TCP/IP局域网与Battle.net共用客户端和原MCP／D2GS协议；另提供连接PvPGN D2CS／D2DBS的独立控制台游戏服务端，复用同一玩法内核、地图和原包处理。

当前Windows客户端与独立服务端均已有Debug构建及独立发行包。已有有限佣兵及本机PvPGN独立服务端冒烟，代表证据不认证全部新增功能；包身份与准确范围见[当前基线](BASELINE.md)。

[文档目录](docs/README.md) · [当前基线](BASELINE.md) · [代码架构](docs/architecture/OVERVIEW.md) · [协作约定](AGENTS.md)

## 当前范围

已有局前流程、五幕原地图生成、行走／旅行、库存／成长／商店、五幕个人任务主流程，以及女巫、亚马逊、第一幕怪物和罗格佣兵的已声明实现。完整七职业、其他幕怪物、组队、交易权威、PvP与Ladder仍有缺口；逐项限制见[基线](BASELINE.md)及[文档目录](docs/README.md)，不以界面或消息入口存在宣称全部可玩。

## 两种发行包

| 内容 | 客户端 | 独立服务端 |
| --- | --- | --- |
| CMake可执行目标 | `d2x` | `d2x_pvpgn` |
| 程序 | `d2x.exe` | `d2x_server.exe` |
| 发行目录 | `dist/current` | `dist/server` |
| 打包脚本 | [package.ps1](scripts/package.ps1) | [package-server.ps1](scripts/package-server.ps1) |
| 用途 | 图形客户端，含Single Player／LAN嵌入宿主 | 无图形控制台D2GS，连接既有PvPGN后端 |

CMake目标`d2x_server`是权威内核库，不是独立EXE目标。两份打包脚本只复制已构建产物，不隐式编译或切换Debug／Release，不互相覆盖，不复制MPQ或角色数据库。

## 构建与运行

需要当前支持的完整原MPQ，本机位于assets/mpq2；也可--mpq指定或放在EXE同目录。原版认证文件只读，配置见[开发指南](docs/development/BUILD_AND_RUN.md)及[模板](docs/development/online.example.json)。

```powershell
# 在仓库根显式构建，再运行；Play.cmd不会隐式构建。
.\scripts\build.ps1 -Configuration Release
.\Play.cmd
```

主菜单Single Player进入同一原图角色列表，服务端管理本地D2S。Battle.net登录／注册，选择服务器角色并建房／加入。先通过UI记忆登录后，`Play.cmd -OnlinePlay <角色名>`可正常认证、选角并一次创建随机普通房间；指定房间用-OnlineCharacter配-OnlineCreateGame或-OnlineJoinGame。失败或人工操作停止自动步骤，不绕过认证／取票或反复建房。

[客户端打包脚本](scripts/package.ps1)更新`dist/current`。已有包可在仓库根用 `dist/current/Play.cmd -Mpq assets/mpq2` 启动；偏好和截图归包目录，Single Player角色由包目录saves保存，原服角色由原服保存。Linux尚未实际编译／运行；构建与打包方法见[开发指南](docs/development/BUILD_AND_RUN.md)。

## 独立服务端

服务端用于接替原D2GS游戏进程，**仍需已有bnetd、D2CS和D2DBS**，不替代账号／Realm／数据库服务。先备份数据库并使用开发环境；原D2GS须先释放游戏端口4000。LAN Host／Join源码默认游戏端口也为4000，同机并行运行需显式改端口。

在仓库根操作已打包的服务端：

```powershell
.\dist\server\scripts\Start-D2XServer.ps1
.\dist\server\scripts\Stop-D2XServer.ps1
```

包内`server.json`配置MPQ、D2CS／D2DBS地址、恢复目录和停止文件，路径相对配置文件解析。启停脚本只管理新服务端，不修改原PvPGN脚本或停止原服进程；停止时等待存档确认，不强杀。

仅接受当前支持的资料片非Ladder／非Hardcore角色。后端接入已有本机双客户端／双房／保存／启停有限冒烟，不支持自动重连、签名认证或Warden，不能视为完整原D2GS或生产部署替代。配置前提、保存故障恢复与限制统一见[PvPGN服务端部署](docs/development/PVPGN_SERVER.md)。

## 操作

| 输入 | 当前用途 |
| --- | --- |
| 左键空地／方向键 | 移动；连续边界步行跨区，洞口／楼梯点击交互 |
| 左键敌人／物品／NPC | 左技能、靠近拾取或交谈；按住单位锁定目标 |
| 右键单位／空地 | 右技能，按住续发；单位目标锁定，空地跟随朝向 |
| 点击技能槽；悬停菜单＋F1–F8 | 展开／选择；绑定快捷键，单按快捷键切换而不立即施法 |
| Shift＋左键 | 原地使用左技能 |
| I、A／C、S／T、Q、O | 包裹、人物、技能树、任务、佣兵面板；未接服务仍不可用 |
| 1–4、B、W／I–II标签 | 腰带饮药、展开腰带、原请求切换武器组 |
| R／底栏走跑、按住Ctrl | 走跑切换、临时跑步 |
| Alt、Tab、V、Home／方向键、F12 | 物品名、地图、角落地图左右、居中／平移、地图名称 |
| ESC | 关闭面板／对白或开菜单；死亡时请求原服回城，Save and Exit请求原服保存退局 |
| Ctrl＋F1、Ctrl＋F3、Ctrl＋F12 | 帮助、碰撞网格、截图 |

操作可用不代表全部场景验收。显式调试pause/resume仅冻结客户端表现，原服仍会移动、伤害或死亡；现有命令与异步含义见[调试管道](docs/development/DEBUG_PIPE.md)。

源码GPL-3.0，暴雪素材权利独立；固定参考与第三方许可见[资料来源](docs/resources/THIRD_PARTY.md)。
