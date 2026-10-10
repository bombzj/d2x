# MPQ 资源

当前唯一运行目标是资料片，使用本机 `assets/mpq2` 的五个原始 MPQ。试玩／经典版旧资源保留，但不提供运行入口。原图、原表来自 MPQ，不提交资源或导出物。

## 当前文件摘要

| 原始文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| d2data.mpq | 267642202 | `99e53768ddb87ca70b54529ff15043aaa71353cc250e757ae20e9c3d002ca431` |
| d2char.mpq | 269965091 | `b808d1bb10a980079b3ba3a04c7f54d3ac7f5cc1bee184ec83a6ad225b74e1d3` |
| d2exp.mpq | 250156780 | `04e35bffeae8d9959042832055702f8b5b4567ca5505ff1df0cf3fd6d5beb6c2` |
| d2sfx.mpq | 55321534 | `64f018a11ffdbd3bad8051207ffec29b6a876eb3f8c1988d3c209436da4b6e69` |
| Patch_D2.mpq | 3912204 | `0d0d49be0a30b3e9b1913296af7450c1b77227e611bbc9e33ecfe437b1ec83d8` |

这些是本机文件摘要，不代表官方签名。数据使用资料片命名类型、TreasureClassEx 及按名称引用的 MonStats，适配器标识 `lod-named-txt-v1`；不凭文件大小推断具体补丁号。

历史 HUD 精简包 `assets/mpq2/d2x-act1.mpq` 为 13,830,018 字节（13.19 MiB），397 个资源；SHA-256：`b1205cbfb31783165af5bd3c0e47583fca0038207acbb0976be11843c77e3c36`。含 22 个 DS1、所需 DT1、原表、当时角色／物件／UI／音效。当前工作资源目录仍保留该历史文件，挂载优先级低于五个原始 MPQ；当前打包不复制 MPQ，也不再制作精简 MPQ。

## 文件职责与覆盖

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


`Archives` 按 Patch_D2 高于 d2exp、高于基础包的优先级读取；旧精简包仅作最低优先级历史资源。资源必须来自一致的安装版本，不混用不同补丁的表和图。语音／音乐／视频文件是否必需取决于相应消费者的实现，现有五个文件不代表完整原版声音和视频功能。

显式 `--mpq` 优先；自动目录定位的准确顺序见 [构建与运行](../development/BUILD_AND_RUN.md#windows-运行)。当前没有额外下载步骤；资源存在不代表原客户端规则已经实现。

## Trees.ds1 原尾部兼容

`data/global/tiles/act1/outdoors/trees.ds1`实际存在于d2data.mpq，6960字节，SHA-256为`9d1de76d43b28051f2c67b4f3b4d3ca96ece808f99c0e330eb224bc929826954`。当前完整挂载、直接读基础包及保留的旧试玩基础包提取结果相同；当前其他四包无此成员，未发生补丁覆盖，没有证据支持本机下载损坏。原 MPQ 不修改。

该DS1为v12、替换方法1，存储37×9格，图层完整、物件数0。偏移6744声明14组，6748起实际含13个完整的四整数记录；偏移6956仅留末组x=0，y／width／height共12字节不存在。完整13组均为3×3，坐标依文件顺序为`(0,0), (0,5), (5,0), (5,6), (11,0), (11,6), (17,0), (17,6), (23,0), (23,6), (29,0), (34,0), (29,6)`。

D2MOO 的 `D2Hell/src/Archive.cpp::ARCHIVE_AllocateBufferAndReadFile` 分配文件长度加800字节，只读取文件本身；`DrlgPreset.cpp` 按声明数量读取全部组，不检查 EOF 或删除末组；`DrlgTileSub.cpp::DRLGTILESUB_DoSubstitutions` 在全部组中抽签。当前 MPQ Trees 的 CheckAll=0，五个主题的 Trials 均为5，Max 为2／2／2／12／2。零尺寸组仍有可用位置；抽到后掷 x／y 各一次，空匹配循环成功，空替换不写瓦片并结束本次试放。删除组或在位置抽签前跳过会改变后续随机流。

当前共用资源解码保留14组，对这个已知末组将缺失的 y／宽／高作为零尺寸兼容占位；不是将14组减少为13组。仅接受同路径、v12／方法1、37×9、6960字节、无物件、声明14组及最后恰余x=0四字节的情形，其他截断仍拒绝。正常完整的零尺寸组也保留。替换在抽组、位置及匹配后作为成功空操作，不产生零尺寸地图片段。诊断 `groups=14 / declared=14 / zeroFilled=1`，旧 skipped 字段已移除，地图规则指纹为 `map-rules-v13-shared-retail-all-acts`；独立D2S v96编码不变；产品已删除探索侧文件入口。

本机1.13c `D2Common.dll`静态读取确认：RVA `0x9900` 的加载函数在 `0x994d` 按文件长度加800字节分配；`0xb560` 的 DS1 解析调用该加载函数，`0xba38` 读取声明数量，`0xba71..0xbabc` 直接读取组字段。Fog ordinal10042／内存池返回已有块不清零，另一路 HeapAlloc flags=0；D2Common `0x9610` 只读取并检查文件长度，未初始化额外尾部。零尺寸是本项目确定性兼容策略，不是 D2MOO／原 DLL 保证的尾部值，不复现未初始化内存读取。既有原DLL新进程导出与共同C++核心的第一幕主题、最终DT1和完整碰撞一致；该证据不证明任意未初始化尾部必为零。

当前包已包含14组兼容，旧13组说明已被替代。122组原版结果对照和五种子全世界加载已通过，见[地图验证](../gameplay/world/MAPS.md#对照与验证边界)及[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。原MPQ未改动。

## 查询与提取

运行时按已知路径查 MPQ 哈希表，挂载时不加载内部／外部 listfile；只有 `d2x_assets list` 第一次枚举时补载文件名清单。包覆盖优先级不变。`Archives` 缓存不超过 32 MiB 的已解压成员，单成员上限 2 MiB；超过总预算清空缓存，大成员直接读取。缓存归当前挂载集合，增加包时失效，不写资源侧文件。

同一个 `Archives` 的 `sharedClassicData` 只构建一次只读规则集合，供客户端和嵌入宿主共享；独立进程各有自己的集合。TXT 字段查询使用不区分 ASCII 大小写的索引，保留原列顺序、重复列及行号。世界、角色、存档和 GPU 对象仍归各自消费者，不放入该共享集合。

将示例目录替换为实际资源目录：

```powershell
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ list '*.ds1'
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ extract data/global/excel/objects.txt objects.txt
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ extract data/global/excel/treasureclassex.txt treasureclassex.txt
```

这些是资源查看命令，不是测试脚本。原始 MPQ 常依赖外部 listfile，枚举为空不等于按已知路径读取不到文件。请同时记录游戏版本、语言、各文件字节数和 SHA-256，以便区分补丁覆盖、压缩包损坏和实际内容缺失。


资源适配路径见 [数据流](../architecture/DATA_FLOW.md)，原始素材与参考代码的许可分别见 [资料来源](THIRD_PARTY.md)。保留原 MPQ，不用修改包内文件来绕过解析错误。
