# 构建、运行与打包

本页维护构建、运行与打包方法；功能范围、原版依据及缺口见[项目基线](../../BASELINE.md)与对应专题。实际构建、运行和打包情况在当次交付中说明，不维护包身份或冒烟历史台账。命令按用户有效授权执行，默认不构建、运行或打包。

需要人工操作时留存消息，用根目录`Play-Diagnostic.cmd`；它沿用普通Play的程序和角色目录，打开游戏及默认调试管道，分别记录客户端／嵌入宿主的双向D2GS消息、程序输出和EXE身份。它不自动构建或修改角色。目录、脱敏范围和故障现场读取方式见[原服收包记录](DEBUG_PIPE.md#原服收包记录)。

## Linux 支持状态

核心代码使用 C++20、标准文件系统、raylib 和 StormLib。存档替换单独区分 Windows 的 MoveFileExW 与其他系统的 rename；玩法、数据和存档编码不依赖 Win32。两个依赖都有 Linux 构建支持，运行时读取 `assets/mpq2` 的原始 MPQ。CMake 只在 Windows 定义相关宏，只对 MinGW 使用 Windows 静态链接选项。

目前 Windows 已实际编译和运行。Linux 已完成源码及依赖配置检查，但开发机器没有可用的 Linux/WSL 环境，**尚未在 Linux 实际编译或运行**。下面使用 X11 后端；Wayland 桌面需要 XWayland。运行游戏需要图形桌面和支持 OpenGL 3.3 的驱动。

## Ubuntu / Debian 构建

建议使用较新的桌面发行版，例如 Ubuntu 24.04；要求 CMake 3.25+ 和支持 C++20 的编译器，建议 GCC 12+。

安装编译工具和窗口、音频、OpenGL 开发依赖：

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git \
  libasound2-dev libx11-dev libxrandr-dev libxi-dev \
  libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev
```

进入项目根目录，执行：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF
cmake --build build --parallel
./build/bin/d2x
```

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib、StormLib、Asio 和 BNCSutil，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 图形应用不需要 Wine；联网认证会只读配置中的原版客户端文件。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。功能范围见基线，Linux验证限制见本页支持状态。

游戏内由原服提供角色、单位和区域状态，客户端重建 MPQ 地形。技能菜单悬停后用 `F1–F8` 绑定，`1–4` 饮药，`B` 展开腰带，`I` 整理库存；菜单不暂停联机会话。本地读写档与直进区域参数已退出产品入口。

## Windows 运行

构建已有EXE后，在项目根目录使用 `Play.cmd`，或运行 `build/bin/d2x.exe`。显式 `--mpq <目录或文件>` 优先；未指定时，依次搜索工作目录本身、EXE 所在目录本身、工作目录的 `assets/mpq2`，再搜索 EXE 所在目录及最多四级父目录中的 `assets/mpq2`。自动定位以 `d2data.mpq`／`D2Data.mpq` 为入口，找到首个目录即停止，挂载该目录全部 MPQ，不递归搜索子目录。无论是否显式 `--mpq`，另在工作目录、EXE 所在目录及其最多五级父目录中查找 `assets/mpq`：其中的补充包最先挂载，优先级低于 `--mpq` 目录，原版成员始终优先，补充包只填补原版未提供的路径；两目录指向同一位置时不重复挂载。工作目录与 EXE 所在目录可以不同。MPQ 不参与编译，但运行时必须可用。源码入口为 `CMakeLists.txt`、`cmake/` 和 `src/`；许可和素材来源见 `LICENSE`、[第三方说明](../resources/THIRD_PARTY.md) 及 `docs/licenses/`。

`--lang <eng|chi|deu|fra|esp|ita|jpn|kor|pol|rus 或 0-9>` 覆盖语言：字符串取自 `data/local/lng/<目录>/` 的原始 string／expansionstring／patchstring.tbl，缺失回退 `eng`；未指定时读 MPQ 的 `data/local/use`。字形取自 `data/local/font/<语言目录>/`（西语系共用 `latin`，中日韩各用 `chi`/`jpn`/`kor`）；所选语言只提供 `latin` 字体时回退 `latin`，此时该语言的非 ASCII 字符无字形可绘。原版五个基础 MPQ 只含 `latin` 字体，中日韩界面需资源包内含对应 `data/local/font/<chi|jpn|kor>/` 字体；`Play.cmd -Lang chi` 转发该参数。字形纹理按需上传：字体表可含上万码点（中日韩 font16 约 16651 帧），仅在实际绘制时上传对应 DC6 帧，避免启动时逐帧建纹理。散装数据目录可用 `d2x_assets <data目录> pack-dir <前缀> <新.mpq>` 打成 MPQ 后挂载（成员名为前缀加相对路径）；繁体中文优先取原版 MPQ 自带的 `data/local/lng/chi/` 文本，仅当原版缺 `expansionstring.tbl` 与 `data/local/font/chi/` 字形时才补入 `PD2/Data` 的对应内容，不使用 SOE 资源；该缺口由 `assets/mpq` 的补充包自动补齐（如 `pd2-chinese.mpq` 提供 chi 的 `expansionstring.tbl` 与 `data/local/font/chi/`），否则这些文案回退英文。ESC 菜单与选项页标签按原版 `%s\UI\%s\...` 形式读 `data/local/ui/<语言目录>/<名称>.dc6`，所选语言缺同名文件时回退 `data/local/ui/eng/`；本机原版 MPQ 中各语言标签图与英文逐字节相同，非英文标签图须由资源包提供。

当前源码普通启动进入原图主菜单：Battle.net沿账号／Realm／角色／房间链，Single Player使用内存MCP／D2GS；TCP/IP在本机选角后按原版直连4000并交换本地存档，详见联网模块。F11保存、Ctrl+F11校验后重载；ESC／失焦暂停单机，共享房间继续推进。角色、战斗、库存及保存由所连接服务端执行，当前支持范围和缺口见[基线](../../BASELINE.md)。客户端与独立服务端的分发目录分别为`dist/current`和`dist/server`，目录名不表示其现有程序的源码版本或构建配置。

Windows EXE 需要同目录 `d2x_bncs_legacy.dll`。`--online-config <路径>` 指定配置，原版客户端文件只读参与认证；PvPGN 模式无需 CD-key。网络 worker 独立于菜单、资源加载和绘制持续推进，`--hidden` 也不会进入本地玩法。`--load <file.d2s>`或`--class <MPQ职业名>`可启动嵌入宿主并通过原协议快捷入局，`--save <新file.d2s>`指定新目标；已存在目标拒绝覆盖，继续用--load。--seed／--map-seed／--population-seed／--level／--region／--difficulty等旧世界参数仍不可用。存储约束见[存档](../modules/SAVES.md)。

先通过 UI 记忆一次账号，可简化重复入局：

```powershell
.\Play.cmd -OnlineCharacter <角色名> -OnlineCreateGame <新房间>
.\Play.cmd -OnlineCharacter <角色名> -OnlineJoinGame <已有房间>
# EXE 对应参数：
.\build\bin\d2x.exe --online-character <角色名> --online-create-game <新房间> --debug-pipe d2x-debug
```

快捷流程只执行一次正常认证、选角、建房／加入；不绕过原服票据、不重复注册或建房。仅指定角色则进入大厅。失败或人工操作结束自动步骤，继续使用页面或 command；有密码或其他房间设置使用这些正式入口。记忆按配置文件路径隔离，复制到包后需在包目录登录一次。

ESC 优先关闭面板，无面板时打开游戏菜单；Save and Exit 请求原服退局保存，成功离局返回对应服务器／本机选角。关闭窗口／quit 仍持续服务退局交换至响应或期限后退出。测试 `pause/resume` 仅在显式调试管道启用，冻结客户端画面／界面输入，服务器与网络持续运行；不提供原服单步，恢复使用最新副本。详见[调试入口](DEBUG_PIPE.md)。

聊天操作与实现范围见[联网模块](../modules/NETWORK.md#多人只读副本)，本页不维护功能完成清单。

启动脚本不隐式构建：缺EXE时要求显式执行scripts/build.ps1。

EXE 的可选 `--startup-profile <路径.tsv>` 按单调时钟记录从 `runGame` 开始的首次生命周期节点：MPQ 挂载、平台、菜单资产、首个显示帧、服务端世界、客户端内容、游戏 UI 及首个已绘制游戏帧。每个节点只记录一次并立即刷新；未指定不产生文件。`--frames <数量>` 可有限运行后正常退局，`--hidden` 仍建立 OpenGL 上下文但跳过音频设备初始化。测速需注明运行条件，文件首帧时间不包含操作系统创建进程之前的耗时，也不代表首次磁盘冷启动或后续每次入局耗时。

启动测量在当次交付中说明程序版本、资源、启动参数及采样条件；临时测量文件可清理，不作为功能或原版一致性的长期依据。

双客户端从两个终端分别运行 `dist/current/Play.cmd -PipeName d2x-player-one` 和 `dist/current/Play.cmd -PipeName d2x-player-two`，在各自窗口登录不同账号并选择不同角色。第一位创建房间，第二位点击 Join、选择真实列表项查看详情后加入，或输入名称／密码直接加入。参考服可能不在列表展示密码房间或资格不符房间；原服决定加入结果。调试管道名必须不同，账号记忆仍按配置路径共享；第二个窗口修改记忆不会替换第一个窗口已登录的会话。

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式见[存档说明](../modules/SAVES.md)，旧档不迁移。原资源只维护项目 `assets/mpq2` 一份，EXE 通过 `--mpq` 读取，不为本地打包抽取或复制 MPQ，也不生成精简资源包。原 MPQ 文件清单见 [MPQ 资源](../resources/MPQ.md)。

## 当前本地分发目录

独立无图形PvPGN游戏服务端使用`d2x_pvpgn`目标，输出`d2x_server.exe`，发行目录为`dist/server`，不覆盖下面的客户端包。配置、独立启停和保存限制统一见[PvPGN服务端](PVPGN_SERVER.md)。原服服务启动脚本不变；默认游戏端口统一4000，同机原D2GS必须先释放该端口。

客户端运行目录固定为 `dist/current/`，入口 `Play.cmd`，根目录放置 `d2x.exe`。下面是显式重新构建Release客户端包的操作示例，不代表当前已交付包的构建类型：

```powershell
.\scripts\build.ps1 -Configuration Release -Target d2x
.\scripts\package.ps1
.\dist\current\Play.cmd -Mpq assets/mpq2
```

`build.ps1` 默认只构建`d2x`及其依赖；独立服务端用`-Target d2x_pvpgn`，资源工具用`-Target d2x_assets`，需要全部目标时显式指定`-Target all`。公共头文件仍会使依赖它们的编译单元重新构建；Ninja增量不会因为只选客户端而跳过必要依赖。

`package.ps1` 只复制已构建程序、两份运行脚本及说明／许可，覆盖更新同一目录，不复制 MPQ、不删除该目录的存档或截图、不运行程序。程序、脚本和文档按长度／SHA256比较，内容相同则跳过复制，并报告复制文件数、字节数与跳过数；连接配置也只在内容变化时更新。同步文档时移除源目录已不存在的说明文件，避免目录调整后留下过期文档。运行目录已有的`artifacts`、旧程序和用户文件不会进入复制清单，也不会被清理，因此整个目录占用不代表本次交付大小。未指定 `-Mpq` 时启动脚本先查运行目录本身和 EXE 同目录，再从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处可把完整原 MPQ 放在 `d2x.exe` 旁边，或传实际原资源路径。`-Mpq/-OnlineConfig` 按调用时工作目录解析为绝对路径。程序工作目录为包根，客户端偏好与截图归包，角色持久化由原服处理。整个 `dist/` 忽略Git。示例路径按仓库根作为调用目录；例如从仓库根启动 `dist/current/Play.cmd -Mpq assets/mpq2`，不要把包目录当原资源相对根。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。

独立服务端在CMake Tools中选择`d2x_pvpgn`构建目标，成功后运行[scripts/package-server.ps1](../../scripts/package-server.ps1)。原`d2x`目标和客户端打包脚本继续保留；`d2x_server`是内核库目标，不能误当成EXE目标。两份脚本均复制当前已构建文件，不隐式编译或切换配置。服务端启动／关闭请使用[PvPGN部署页](PVPGN_SERVER.md)中的独立脚本，不使用客户端Play入口。
