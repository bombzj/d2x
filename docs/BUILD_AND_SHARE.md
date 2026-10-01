# 构建与运行

最新批次（2026-10-01，圣骑士光环技能树）：20 个已实现光环接原职业树学习／右手选择／F1–F8／武器组切换，统一原基础点数等级门槛和被动刷新，按 immediate／perdelay 同步持有者状态；树和菜单数值来自玩法解析，切换清旧来源，提示显示和排版仍属项目适配。Windows Release 已完成最终 EXE 链接，固定运行包更新至 dist/current，入口 Play.cmd。本轮获授权运行包内 EXE 冒烟并提交源码／文档：临时角色覆盖前置／下一点等级拒绝、20 项学习与逐项菜单选择、力量立即增伤、圣火首周期、反抗防御、I／II 武器组和重置清状态；两个实例正常退出，退出码均为 0，无角色读写或新测试程序。最终包技能树截图在运行目录 artifacts/debug-pipe.png；F1–F8 按用户要求未验证，完整战斗、装备加成／读档重建及原版画面对照仍待验收。不改 MPQ、用户存档或 D2S v96，完整依据见 [技能](SKILLS.md#圣骑士光环学习与切换)。

最新交付（2026-10-01，第一幕第二阶段）：用户授权缺完整 reference 的部分按 MPQ 和公开资料做可用适配，不再因此禁用功能。随机勇士／金怪及直属随从数值、词缀事件、单次直接伤害、当帧闪电、传送、姓名／标签／原配色和勇士组规则已接。启动内容导入因审判含逗号公式被 TXT 外层双引号包裹而失败，已修正；已有资源工具 item cap 成功完成同一内容加载路径。Windows Release 已完成最终 EXE 链接并更新固定 dist/current，入口 Play.cmd；未启动游戏／执行测试／提交 Git，实际启动与画面仍待用户验收。原 MPQ、存档及旧产物保留；姓名组合、精英选色、模式／弹体创建时序和客户端声音等适配差异见 [第二阶段](MONSTERS.md#第二阶段基础接入与暂缓)。下方分批交付描述为历史记录，不覆盖此状态。

光环分批交付（2026-10-01）：通用入口当前登记 20 个圣骑士光环，包含元素、防御／抗性、恢复／移动、反伤／专注／庇护／救赎；最新原命中标志及审判 intrinsicCombat 基础抗性修正已完成 Windows Release 最终 EXE 链接。防御抗性批次已修复前批光环切换名称遮蔽编译错误；前批提前打包不代表包含当时全部最终源码。按用户最新要求每完成几个统一更新包、提交源码／文档，不再每项打包提交。自然精英只装载已登记池项，不开启其余词缀技能事件。入口仍为 `dist/current/Play.cmd`，共用现有 MPQ、保留角色存档和产物；不启动游戏或执行测试，用户人工验收。精灵光环真实召唤链、同等级来源规则、完整命中快照及客户端精确渲染／声音仍未完成，不能按登记数宣称全部准确；D2S v96 不变，见 [光环](SKILLS.md#通用光环逐项实施)。

当前批次（2026-10-01，自然精英基础）：接入自然勇士／随机金怪、勇士变体原数值与准确直属随从基础强化、家族 AI、承伤与收益，修正共用难度字段；特殊技能按用户要求待通用能力完成再安装，原随机姓名／精英调色缺完整依据仍暂缓，第二阶段不是全部完成。最终源码按要求仅编译并更新固定 `dist/current`，不启动游戏、不做最终运行测试、不提交 Git，留用户验收；要求停止前的旧临时实例已经退出，不代表最终版本验证。原 MPQ、用户存档、旧产物保持，D2S v96 不变，详细范围见 [第二阶段交接](MONSTERS.md#第二阶段基础接入与暂缓)。

第一阶段收尾交付（2026-10-01）：本轮确认第一阶段尚未结束，因此先完成当前阶段，不进入第二阶段。新增收尾遵守本地 D2MOO 的巫师两次射击／直属尸体回调、Nest 许可与保存出生坐标、自身游走单次掷骰、整数退避、Vampire 原概率／失败后继续决策、Fallen 自身命令、GH 专用 FHR／基础 50% 及 A1/A2 中途速率同步。Windows Release 编译通过；临时实例确认 Brute GH .48 秒、巢六次后 NOTC／无掉落、地狱冰弹约 1.5 秒冷时长及无 GH 的法师 A1 100%→75% 进度保持。此前 .24 秒 GH 和冷效果延长 GH 的说法已被 reference 核对纠正。最终包入口仍为 `dist/current/Play.cmd`，资源与用户存档保留；源码／文档按本轮授权提交，产物不入 Git。人工验收及共用世界适配边界见 [怪物](MONSTERS.md#第一阶段修复与交接)。

当前交付（2026-10-01，Act 1 怪物第一阶段）：Windows Release 已编译，固定 `dist/current` 更新本批移动动作、弓手／法师参数、起手／近战命中范围、速度／ColdEffect、冷时长难度除数、软受击／GH、直属键与巢 NOTC 修订。包目录普通／噩梦／地狱有限帧启动均退出码 0；既有管道临时角色覆盖女盗贼软命中保持 85% 走近、法师接近完成再 A1、Brute 软／硬受击、正式冰弹及巢达到 6 次后保留经验而无 TC，最后包内 GH／巢复验通过。未新增测试脚本、用例或专用程序，未覆盖全部变体／概率分支／高难度战斗；画面、手感和完整流程留给用户测试，准确剩余边界见 [怪物第一阶段](MONSTERS.md#第一阶段修复与交接)。临时实例均已退出，无角色读写，D2S v96 不变，未提交。有限帧日志位于 `artifacts/monster-stage1-20261001/`；入口 `dist/current/Play.cmd`，包共用已有原 MPQ，保留用户存档、截图、旧产物。

当前交付（2026-10-01，MPQ 同目录定位）：当前完整源码 Windows Release 编译通过，固定运行包更新至 `dist/current`。MPQ 自动定位新增工作目录与 EXE 同目录，并同步启动脚本；显式资源路径保持最高优先级。面板／弹窗／HUD 的按下由 UI 持有直到松开，修复 X 关闭后继续移动；井／箱子／祭坛按原矩形条带范围及操作射线走近并执行；照明加入 D2Gfx 四邻点整数平均与角点对齐。包内保留此前冰封球原弹体／数值／PL2 混色／WAV 循环、全局原表光源／昼夜及明暗查表，以及场景排序、移动碰撞、战斗阵营、召唤骷髅、佣兵与物品修改。构建仍有既有聚合初始化与范围循环复制告警；协作 agent 没有编写或运行测试、没有启动游戏；用户已确认此前场景修订运行包没有问题；本次 MPQ 定位未做运行验收，未提交。户外整屏亮度报告尚未证实为回归；D2Client 距离衰减、彩光合成／遮挡、闪烁与完整索引取光管线仍暂缓，不能据编译通过宣称全项目照明与原版一致，见 [照明](LIGHTING.md)、[经典 HUD](CLASSIC_HUD.md) 和 [交互物体](INTERACTIVE_OBJECTS.md)。D2S v96 不变；入口为 `dist/current/Play.cmd`，继续引用已有 `assets/mpq2`，不复制原 MPQ，保留现有存档、截图、旧压缩包及 `mvp/`。资源、reference、构建缓存和抽取图不进源码提交，实际画面与行为由用户验收。

鉴定／拾取／随机机制批次（2026-09-27）：四项源码修复完成，Windows Release 已编译通过，固定运行包更新至 dist/current（包含 d2x.exe、启动脚本与本批参考文档）。构建仍报告既有物品、佣兵及商店聚合初始化告警。没有编写测试脚本、用例或专用测试程序，不运行游戏；交互和画面等待用户验收。依据与适配边界见 [背包](INVENTORY_UI.md)、[随机机制](RANDOMNESS.md) 和 [存档](SAVES.md)。

上锁宝箱交付（2026-09-27）：宝箱生成、原概率初始化、背包钥匙扣减、刺客例外、普通／特殊箱掉落及提示／开锁声音完成后，Windows Release 编译通过，固定运行包更新至 `dist/current`。构建仍有此前物品、药水、商店及佣兵的聚合初始化告警，本批修改的宝箱代码未报告新告警；没有编写或运行测试，未启动游戏，实际交互与视觉等待用户验收。规则、参考及箱陷阱／任务宝箱等边界见 [交互物体](INTERACTIVE_OBJECTS.md#上锁宝箱2026-09-27)。包包含先前已交付的祭坛效果，仍引用已有 `assets/mpq2`，保留现有存档和旧产物，不复制 MPQ 或 reference。

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

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib 和 StormLib，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 不需要 Wine，也不使用 Windows EXE。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。

游戏内由营地步行进入连续野外，点击洞口／楼梯旅行；`Ctrl+F2` 是开发用区域目录，独立区域也可用 `--level 38` 等参数查看。打开鼠标技能菜单，悬停技能图标按 `F1–F8` 绑定，之后单按该键切换对应鼠标技能；`1–4` 饮药，`B` 展开腰带。原表人口计划按附近房间创建敌人；未实现类型使用保留真实身份的沉沦魔替身。按 `I` 整理，F11 保存、Ctrl+F11 读取。营地内 `Ctrl+F4` 走近原版私人储物箱。

## Windows 运行

在项目根目录使用 `Play.cmd`，或运行 `build/bin/d2x.exe`。显式 `--mpq <目录或文件>` 优先；未指定时，依次搜索工作目录本身、EXE 所在目录本身、工作目录的 `assets/mpq2`，再搜索 EXE 所在目录及最多四级父目录中的 `assets/mpq2`。自动定位以 `d2data.mpq`／`D2Data.mpq` 为入口，找到首个目录即停止，挂载该目录全部 MPQ，不递归搜索子目录。工作目录与 EXE 所在目录可以不同。MPQ 不参与编译，但运行时必须可用。源码入口为 `CMakeLists.txt`、`cmake/` 和 `src/`；许可和素材来源见 `LICENSE`、[第三方说明](THIRD_PARTY.md) 及 `docs/licenses/`。

普通图形启动及默认 `Play.cmd` 先显示资料片角色列表；可新建七职业普通角色、输入角色名、选择已有角色进入游戏，或确认删除角色。角色以原版 D2S v96 存放在运行目录的 `saves/`；按 F11 保存，正常退出自动保存。仅指定 `--mpq`、`--debug-pipe`、`--debug-run` 不再跳过角色界面；显式 `--class`、`--load`、`--hidden`、`--level`、`--region` 等场景／批处理选项仍直进游戏。启动器仅在显式传入 `-Level`／`-Region` 时附带区域参数。角色确认后重新显示原 MPQ 加载动画，覆盖读档、世界构造和场景资源上传。角色前端只支持资料片普通角色，转换和专家模式控件禁用；D2S 的明确支持范围见[存档](SAVES.md)，旧内部档不迁移。

直接选择职业开始测试，不经过 UI 或存档：

```powershell
.\Play.cmd -Class Sorceress
.\Play.cmd -Class Necromancer -Level 8 -DebugPaused
.\build\bin\d2x.exe --class Sorceress --level 8 --hidden --debug-pipe d2x-skill
```

`--class` 按当前 MPQ 原职业名匹配：`Amazon`、`Sorceress`、`Necromancer`、`Paladin`、`Barbarian`、`Druid`、`Assassin`；生成名为 Hero 的全新一级角色及本职业初始装备。未指定 `--save` 时不在退出时自动保存，手动 F11／管道 save 仍可保存。需要保留场景输入时使用独立输出：

```powershell
.\Play.cmd -Load .\saves\Scenario.d2s -Save .\artifacts\scenario-result.d2s -DebugPaused
```

示例输入必须是已有 D2S。`-Class` 与 `-Load` 互斥；脚本保存路径相对调用时工作目录解析，EXE 路径相对进程工作目录。存档加载仍从城镇开始，进入后可通过开发目录或管道 `travel` 到目标地图。不会通过测试入口原地改变现有角色职业。

游戏内 Esc 优先关闭面板，无面板时返回角色列表；角色来自列表／`-Load` 或显式指定了 `-Save` 时先保存，失败则留在游戏。无保存路径的临时新角色不自动保存。旧会话与视图销毁后重新打开列表，下一角色不会继承上一局地图参数或保存路径。关闭窗口与调试 quit 仍退出进程。

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式见[存档说明](SAVES.md)，旧档不迁移。原资源只维护项目 `assets/mpq2` 一份，EXE 通过 `--mpq` 读取，不为本地打包抽取或复制 MPQ，也不生成精简资源包。原 MPQ 文件清单见 [MPQ 资源](MPQ_RESOURCES.md)。

## 当前本地分发目录

当前运行目录固定为 `dist/current/`，入口 `Play.cmd`，根目录放置 `d2x.exe`。在仓库根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\package.ps1
.\dist\current\Play.cmd -Mpq .\assets\mpq2 -Class Sorceress
```

`package.ps1` 只复制已构建程序、两份运行脚本及说明／许可，覆盖更新同一目录，不复制 MPQ、不删除该目录的存档或截图、不运行程序。未指定 `-Mpq` 时启动脚本先查运行目录本身和 EXE 同目录，再从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处可把完整原 MPQ 放在 `d2x.exe` 旁边，或传实际原资源路径。`-Mpq/-Load/-Save` 按调用时工作目录解析为绝对路径。程序工作目录为包根，默认角色存档和截图与包放在一起。整个 `dist/` 忽略 Git。

2026-09-27 技能重构及通用攻击通过 Windows Release 构建并更新上述目录：原命中帧、逐武器定点数值、弓弩／投掷出手扣弹药以及共享范围爆炸／移动毒云已接入；本包同时包含瘟疫标枪和爆炸箭，见 [亚马逊技能](AMAZON_SKILLS.md)，入口和限制见 [通用攻击](COMMON_ATTACKS.md) 与 [技能](SKILLS.md)。按用户要求未编写／运行测试、未启动游戏；旧包的简测结果不能代替本次行为验收。八个历史包已清除 40 份与 `assets/mpq2` SHA-256 完全相同的 MPQ 副本及纯程序目录，共回收约 6539.4 MiB。原始 MPQ、旧压缩包、存档、截图、日志和参考资料保留；旧包目录仅供历史证据，不再作为运行入口。物品显示、技能及角色启动的既有短程记录保留在各 `artifacts/*2026092*/` 目录。

包同时包含当前源码既有的行走、鼠标锁定、物品显示和 Esc 游戏菜单改动；这些功能此前的验收边界见 [项目基线](../BASELINE.md)。本次技能状态重构不改变 D2S v96 字段或保存语义，详见 [存档](SAVES.md)。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
