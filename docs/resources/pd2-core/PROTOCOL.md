# 协议回调目录

[返回总览](README.md)。仅登记固定ProjectDiablo核心的确定补丁，不能覆盖BH／mdll1的附加钩子。0或未登记回调不表示消息在当前线上一定被接受或拒绝。长度含逻辑包头，不含TCP／压缩外框。

## 核心改写的S2C表项

| 包号 | 原长度 | 目标长度 | 核心回调 |
| --- | --- | --- | --- |
| 0x0A | 6 | 6 | [0x38F5A0](functions/1c.md#rva-38f5a0) |
| 0x22 | 12 | 12 | [0x38F5B0](functions/1c.md#rva-38f5b0) |
| 0x2D | 0 | 5 | [0x38F680](functions/1c.md#rva-38f680) |
| 0x2E | 0 | 18 | [0x38F6B0](functions/1c.md#rva-38f6b0) |
| 0x2F | 0 | 11 | [0x38F790](functions/1c.md#rva-38f790) |
| 0x30 | 0 | 6 | [0x34B4A0](functions/1a.md#rva-34b4a0) |
| 0x31 | 0 | 411 | [0x34B140](functions/1a.md#rva-34b140) |
| 0x32 | 0 | 10 | [0x34B3B0](functions/1a.md#rva-34b3b0) |
| 0x33 | 0 | 407 | [0x34B400](functions/1a.md#rva-34b400) |
| 0x35 | 0 | 411 | [0x34B440](functions/1a.md#rva-34b440) |
| 0x36 | 0 | 411 | [0x34B2B0](functions/1a.md#rva-34b2b0) |
| 0x55 | 0 | 210 | [0x38F5C0](functions/1c.md#rva-38f5c0) |
| 0x56 | 0 | 66 | [0x38F5E0](functions/1c.md#rva-38f5e0) |
| 0x66 | 7 | 5 | [0x38FA00](functions/1c.md#rva-38fa00) |
| 0x83 | 0 | 16 | [0x38FA50](functions/1c.md#rva-38fa50) |
| 0x84 | 0 | 4 | [0x38FA90](functions/1c.md#rva-38fa90) |
| 0x85 | 0 | 393 | [0x38FAD0](functions/1c.md#rva-38fad0) |
| 0x86 | 0 | 7 | [0x38FBB0](functions/1c.md#rva-38fbb0) |
| 0x87 | 0 | 4 | [0x38FB30](functions/1c.md#rva-38fb30) |
| 0x88 | 0 | 387 | [0x38FB00](functions/1c.md#rva-38fb00) |
| 0x97 | 1 | 1 | [0x38FBE0](functions/1c.md#rva-38fbe0) |
| 0x9C | -1 | -1 | [0x38FC20](functions/1c.md#rva-38fc20) |
| 0xA5 | 8 | 8 | [0x38FC40](functions/1c.md#rva-38fc40) |
| 0xA9 | 7 | 7 | [0x38FD20](functions/1c.md#rva-38fd20) |
| 0xAC | -1 | -1 | [0x396350](functions/1c.md#rva-396350) |
| 0xAD | 0 | 6 | [0x38FA40](functions/1c.md#rva-38fa40) |

## 核心改写的C2S长度

| 包号 | 原长度 | 目标长度 |
| --- | --- | --- |
| 0x22 | 5 | 11 |
| 0x2B | 0 | 5 |
| 0x2C | 0 | 3 |
| 0x2D | 1 | 8 |
| 0x2E | 3 | 5 |
| 0x39 | 5 | 12 |
| 0x3B | 3 | 4 |
| 0x42 | 1 | 7 |
| 0x43 | 1 | 10 |
| 0x45 | 9 | 258 |
| 0x4A | 0 | 260 |
| 0x4C | 5 | 11 |
| 0x4E | 0 | 4 |
| 0x52 | 5 | 22 |
| 0x55 | 0 | 2 |
| 0x56 | 0 | 131 |
| 0x57 | 0 | 5 |
| 0x5A | 0 | 1 |
| 0x5B | 0 | 5 |
| 0x5C | 0 | 13 |

## 核心改写的C2S回调

原D2Game回调表RVA为0xFA078，每项8字节；下列按`(目标RVA−0xFA078)/8`定位包号。包0x5B没有出现在本表，而0x5C有独立接收器；前者长度改为5不等于已恢复其线上消费者。

| 包号 | D2Game表项RVA | 核心回调 |
| --- | --- | --- |
| 0x3B | 0xfa250 | [0x391090](functions/1c.md#rva-391090) |
| 0x15 | 0xfa120 | [0x38FD60](functions/1c.md#rva-38fd60) |
| 0x16 | 0xfa128 | [0x38FDC0](functions/1c.md#rva-38fdc0) |
| 0x18 | 0xfa138 | [0x38FF40](functions/1c.md#rva-38ff40) |
| 0x19 | 0xfa140 | [0x390120](functions/1c.md#rva-390120) |
| 0x1A | 0xfa148 | [0x3901B0](functions/1c.md#rva-3901b0) |
| 0x1B | 0xfa150 | [0x390620](functions/1c.md#rva-390620) |
| 0x1C | 0xfa158 | [0x3906B0](functions/1c.md#rva-3906b0) |
| 0x1D | 0xfa160 | [0x390730](functions/1c.md#rva-390730) |
| 0x1E | 0xfa168 | [0x3907C0](functions/1c.md#rva-3907c0) |
| 0x1F | 0xfa170 | [0x390850](functions/1c.md#rva-390850) |
| 0x22 | 0xfa188 | [0x390940](functions/1c.md#rva-390940) |
| 0x28 | 0xfa1b8 | [0x390C90](functions/1c.md#rva-390c90) |
| 0x2C | 0xfa1d8 | [0x392D00](functions/1c.md#rva-392d00) |
| 0x2D | 0xfa1e0 | [0x392DC0](functions/1c.md#rva-392dc0) |
| 0x2E | 0xfa1e8 | [0x391000](functions/1c.md#rva-391000) |
| 0x33 | 0xfa210 | [0x391010](functions/1c.md#rva-391010) |
| 0x39 | 0xfa240 | [0x393770](functions/1c.md#rva-393770) |
| 0x41 | 0xfa280 | [0x391220](functions/1c.md#rva-391220) |
| 0x42 | 0xfa288 | [0x391530](functions/1c.md#rva-391530) |
| 0x43 | 0xfa290 | [0x392BB0](functions/1c.md#rva-392bb0) |
| 0x44 | 0xfa298 | [0x391C00](functions/1c.md#rva-391c00) |
| 0x45 | 0xfa2a0 | [0x393780](functions/1c.md#rva-393780) |
| 0x4A | 0xfa2c8 | [0x393920](functions/1c.md#rva-393920) |
| 0x4C | 0xfa2d8 | [0x391E70](functions/1c.md#rva-391e70) |
| 0x4E | 0xfa2e8 | [0x3939D0](functions/1c.md#rva-3939d0) |
| 0x52 | 0xfa308 | [0x393A80](functions/1c.md#rva-393a80) |
| 0x55 | 0xfa320 | [0x392770](functions/1c.md#rva-392770) |
| 0x57 | 0xfa330 | [0x3927B0](functions/1c.md#rva-3927b0) |
| 0x5A | 0xfa348 | [0x392890](functions/1c.md#rva-392890) |
| 0x5C | 0xfa358 | [0x3928B0](functions/1c.md#rva-3928b0) |
| 0x60 | 0xfa378 | [0x392A10](functions/1c.md#rva-392a10) |
| 0x61 | 0xfa380 | [0x392A50](functions/1c.md#rva-392a50) |
| 0x36 | 0xfa228 | [0x392B30](functions/1c.md#rva-392b30) |
| 0x62 | 0xfa388 | [0x392B70](functions/1c.md#rva-392b70) |

## 消费者复核边界

0x5B的原D2Game回调槽0xFA350为0；核心368600将长度改为5，已恢复的登记未为该槽安装消费者。实际客户端发送规则、独立C2S5C／FF00周期tick及其回显见[CN运行协议](../PD2_CN_DLL_ANALYSIS.md#cn入场选项与自动地图报告)。
