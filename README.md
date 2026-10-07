# D2X — Diablo II: Lord of Destruction C++

基于当前1.13c资料片MPQ的C++20联机客户端，连接既有PvPGN／D2CS／D2GS。正常链路为账号、Realm、服务器角色、房间与原服世界；角色保存由D2GS／D2DBS负责。产品没有单机入口，普通ESC／面板／失焦不暂停网络或原服。

[文档目录](docs/README.md) · [当前基线](BASELINE.md) · [代码架构](docs/architecture/OVERVIEW.md) · [协作约定](AGENTS.md)

## 当前范围

局前、五幕原生地图、部分行走／打怪／库存／成长／任务与原图UI已接；世界、动画、输入、提示、自动地图和声音使用公共入口。完整职业效果、任务／服务、多人与Ladder仍未完成。最新源码与已交付包有差异，准确范围和未入包修正见[基线](BASELINE.md)，原服协议与有限证据见[联网模块](docs/modules/NETWORK.md)。

## 构建与运行

需要当前支持的完整原MPQ，本机位于assets/mpq2；也可--mpq指定或放在EXE同目录。原版认证文件只读，配置见[开发指南](docs/development/BUILD_AND_RUN.md)及[模板](docs/development/online.example.json)。

```powershell
# 在仓库根显式构建，再运行；Play.cmd不会隐式构建。
.\scripts\build.ps1 -Configuration Release
.\Play.cmd
```

主菜单点击Battle.net登录／注册，选择服务器角色并建房／加入。先通过UI记忆登录后，`Play.cmd -OnlinePlay <角色名>`可正常认证、选角并一次创建随机普通房间；指定房间用-OnlineCharacter配-OnlineCreateGame或-OnlineJoinGame。失败或人工操作停止自动步骤，不绕过认证／取票或反复建房。

scripts/package.ps1更新固定dist/current，只复制已构建程序、脚本、文档与许可，不复制MPQ。已有包可在仓库根用 `dist/current/Play.cmd -Mpq assets/mpq2` 启动；偏好和截图归包目录，角色保存归原服。Windows有有限运行证据，Linux尚未实际编译／运行；构建与打包方法见[开发指南](docs/development/BUILD_AND_RUN.md)。

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
