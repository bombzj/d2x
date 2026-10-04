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

## 查询与提取

将示例目录替换为实际资源目录：

```powershell
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ list '*.ds1'
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ extract data/global/excel/objects.txt objects.txt
.\build\bin\d2x_assets.exe D:\DiabloII-MPQ extract data/global/excel/treasureclassex.txt treasureclassex.txt
```

这些是资源查看命令，不是测试脚本。原始 MPQ 常依赖外部 listfile，枚举为空不等于按已知路径读取不到文件。请同时记录游戏版本、语言、各文件字节数和 SHA-256，以便区分补丁覆盖、压缩包损坏和实际内容缺失。


资源适配路径见 [数据流](../architecture/DATA_FLOW.md)，原始素材与参考代码的许可分别见 [资料来源](THIRD_PARTY.md)。保留原 MPQ，不用修改包内文件来绕过解析错误。
