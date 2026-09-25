# MPQ 资源与寻找方法

## 最新状态：完整资源已提供

用户已提供 `assets/mpq2` 五个原始 MPQ，程序运行时直接使用这套原表和美术。第一幕 39／39 项地形在种子 210 的 Windows Release 短帧启动中可装载，Levels 1–37 已接探索路线；任务门、通用野外主题和完整布局规则仍有缺口。

| 原始文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| d2data.mpq | 267642202 | `99e53768ddb87ca70b54529ff15043aaa71353cc250e757ae20e9c3d002ca431` |
| d2char.mpq | 269965091 | `b808d1bb10a980079b3ba3a04c7f54d3ac7f5cc1bee184ec83a6ad225b74e1d3` |
| d2exp.mpq | 250156780 | `04e35bffeae8d9959042832055702f8b5b4567ca5505ff1df0cf3fd6d5beb6c2` |
| d2sfx.mpq | 55321534 | `64f018a11ffdbd3bad8051207ffec29b6a876eb3f8c1988d3c209436da4b6e69` |
| Patch_D2.mpq | 3912204 | `0d0d49be0a30b3e9b1913296af7450c1b77227e611bbc9e33ecfe437b1ec83d8` |

这些是本机文件摘要，不代表官方签名。数据使用资料片命名类型、TreasureClassEx 及按名称引用的 MonStats，适配器标识 `lod-named-txt-v1`；不凭文件大小推断具体补丁号。

历史 HUD 精简包 `assets/mpq2/d2x-act1.mpq` 为 13,830,018 字节（13.19 MiB），397 个资源；SHA-256：`b1205cbfb31783165af5bd3c0e47583fca0038207acbb0976be11843c77e3c36`。含 22 个 DS1、所需 DT1、原表、当时角色／物件／UI／音效。当前工作资源目录仍保留该历史文件，挂载优先级低于五个原始 MPQ；修正后的 `dist/d2x-runtime-20260925-v89-r123-original-mpq/` 不含它，今后不再制作精简 MPQ。

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 maps
.\build\bin\d2x_assets.exe assets/mpq2 maps 37
.\build\bin\d2x.exe --mpq assets/mpq2 --level 38
```

启动默认读取 `assets/mpq2`；同目录旧精简包仅作为最低优先级历史资源，显式 `--mpq` 可覆盖目录。修正后的五 MPQ 分发目录按用户要求尚未启动验证。运行方法见 [构建与运行](BUILD_AND_SHARE.md)。以下保留取得完整包之前的试玩调查，旧资源数量和下载需求仅适用于当时。

后续坚持使用原版素材和已核对的原版数据。素材、原版规则或版本对应关系不足时明确标为未实现，不增加自定义地图物件或随意编写掉落权重来冒充原版功能。

## 历史试玩调查：资源与缺口

当前输入是经典 1.04 官方试玩版的原始资源：d2data.mpq 约 42.25 MiB、d2char.mpq 约 19.82 MiB、d2sfx.mpq 约 10.38 MiB、patch_d2.mpq 约 1.28 MiB。运行用的 `d2x-mvp.mpq` 是按实际使用文件重新压缩的子集，不能仅凭它约 10 MiB 就判断原始资源不够。

- 私人储物箱所需原场景实体、bank.dc6、物品图标、格子和操作距离数据齐备，本轮已经使用。
- 当前四张 DS1 没有原生可掉落箱子记录；原始资源目录能枚举到更多 DS1，不能由这四张地图推断整个试玩包没有箱子地图。
- L1／L2 箱子动画存在；`treasureclass.txt` 含 Chests-act1A／B／C 等旧版条目。本轮读取到该表 9,412 字节。
- 试玩包读取不到 `treasureclassex.txt`，这是版本／内容差别，不能仅据此判断文件损坏。
- 地图箱子的原生分布、表到执行函数的对应、金币和物品生成还需完整接入；更大的 MPQ 不能代替这部分代码。当前暂缓，不生成自定义箱子。
- 自定怪物掉落权重现已删除；原表已经导入，等待核实 1.04 的生成算法。已有技能数值、药效时长和部分表现仍待逐项替换，并非本轮已完成原版规则。
- 本轮从现有源包补入 361 条物品、410 条怪物和 95 条旧 TC，无需重新下载；字段与概率边界见 [原版物品数据](ITEM_DATA.md)。

## 历史试玩调查：完整版文件清单

请准备 **Diablo II 经典正式完整版，以及如需资料片内容则同一安装版本的 Lord of Destruction**，不需要 Resurrected。优先保留同一安装目录里的文件和版本信息，避免把不同补丁的表、图形及定义混在一起。

| 文件 | 用途与优先级 |
| --- | --- |
| `d2data.mpq` | 优先：完整基础地图、对象、UI、数据表和物品等基础资源 |
| `d2exp.mpq` | 资料片内容需要：扩展资源及数据，用于后续确定资料片版本的导入目标 |
| `patch_d2.mpq` | 与所选安装版本配套，覆盖基础／资料片内容 |
| `d2char.mpq` | 完整基础角色动画，后续增加人物和装备外观需要 |
| `d2sfx.mpq` | 原版效果音 |
| `d2speech.mpq`、`d2xtalk.mpq` | 对话／语音阶段再准备 |
| `d2music.mpq`、`d2xmusic.mpq` | 音乐阶段再准备 |
| `d2video.mpq`、`d2xvideo.mpq` | 当前不需要过场视频，可不发送 |

不要只找一个体积很大的 MPQ。最有用的是一套来源和版本一致的原始数据。现有物品目录在运行时读取 1.04 试玩表；取得新版 MPQ 后，需要按选定版本重新导入和对齐规则，不能承诺仅替换文件便支持完整资料片。OpenDiablo2 自身的历史文档以 1.14b 为目标，这也说明版本条件属于具体引擎实现，不能直接当作本项目兼容保证。[OpenDiablo2 的 MPQ 说明](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/docs/mpq.md)

## 怎么找，怎样减少不必要的下载

1. **优先检查已有经典版安装目录或安装介质。** 搜索文件名 `d2data.mpq`、`d2exp.mpq`、`patch_d2.mpq`。常见目录是 `C:\Program Files (x86)\Diablo II`，也可能是自行选择的游戏目录。拿到现成安装目录后，只需复制上表的必要 MPQ，不必把 EXE、音乐、视频等全发来。[OpenDiablo2 定位说明](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/docs/mpq.md)
2. **没有现成文件时，从自己的 Battle.net 经典游戏授权入口获取客户端。** 寻找名称 `Diablo II (2000)` 和 `Diablo II: Lord of Destruction (2001)`，不要进入 Resurrected。其历史操作流程可参考 [OpenDiablo2 官方游戏获取说明](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/docs/purchase.md)。该文档较旧；本轮官方安装支持页重定向后未返回可核对的正文，因此不提供未经确认的直链或保证当前菜单位置不变。
3. **GitHub 用于查代码、数据格式、listfile 和提取工具。** 可搜索 `OpenDiablo2 MPQ`、`Diablo II DS1 DT1`、`StormLib MPQ`。OpenDiablo2 明确不随仓库分发游戏素材，要求从自己的游戏安装中取得；“找到引擎仓库”不等于找到完整 MPQ。[项目说明](https://github.com/OpenDiablo2/OpenDiablo2)

当前运行所需原始 MPQ 已在 `assets/mpq2`，没有额外资源下载步骤。

把待导入原始 MPQ 放在独立目录，例如 `D:\DiabloII-MPQ`，不要与旧的 `d2x-mvp.mpq` 混在一起。查看／提取可使用项目已有资源工具：

```powershell
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ list '*.ds1'
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ extract data/global/excel/objects.txt objects.txt
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ extract data/global/excel/treasureclassex.txt treasureclassex.txt
```

这些是资源查看命令，不是测试脚本。原始 MPQ 常依赖外部 listfile，枚举为空不等于按已知路径读取不到文件。请同时记录游戏版本、语言、各文件字节数和 SHA-256，以便区分补丁覆盖、压缩包损坏和实际内容缺失。

经典底栏使用 Sky 调色板、800ctrlpnl7、overlap、runbutton、普通攻击／法师技能图集及 SkillDesc 原表；原有野蛮人技能图集保留。详见 [底栏与技能 UI](CLASSIC_HUD.md)。当前 `assets/mpq2` 已提供这些素材，旧试玩获取脚本只作历史工具。
