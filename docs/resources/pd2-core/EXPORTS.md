# 核心真实PE导出

[返回总览](README.md)。以下四项按目标PE导出表读取，名称可信度与Ghidra自动匹配名称不同。导出名只确认接口身份，不证明内部全部行为，也不是面向D2X的稳定插件ABI。

| ordinal | 核心RVA | 真实导出名称 |
| --- | --- | --- |
| 1 | [0x365720](functions/1b.md#rva-365720) | `_D2Client_GetItemDiscoveryState@12` |
| 2 | [0x34AFC0](functions/1a.md#rva-34afc0) | `_D2Client_GetItemStorageType@4` |
| 3 | [0x2C65B0](functions/16.md#rva-2c65b0) | `_D2Client_PlaySoundWithCustomVolumeOrPriority@20` |
| 4 | [0x2957C0](functions/14.md#rva-2957c0) | `boost_stacktrace_impl_return_nullptr` |
