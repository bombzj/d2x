# 自动地图

自动地图只有一套揭示、原标记、原图缓存和绘制入口。RemoteTown适配原服房间／位置及活动地形，AutomapExploration拥有本局区域记忆，AutomapCatalog选格标记，SceneAssets载原DC6／城镇拼图，SceneView::drawAutomap绘制；没有联机专用绘制器；本地角色的侧文件由hosting/AutomapStore负责。

## 揭示与生命周期

城镇进入后准备原AutoMap已初始化房间或原整图变体／中心，全揭示沿safe区域规则。附近野外仍按活动房间与主场景实际可见DT1相交后逐格记忆；部分瓦片露出也纳入，屋顶／阴影／照明不参与揭示，联合矩形边角可能略宽。

关闭地图继续累积；大小／平移／Fade不改变探索范围。连续步行区域同层，楼梯／门户隔层；传送只采样落点，不补路线。同局换区／换幕后返回保留；新局先清内存，再按当前角色、难度、资源与服务器实际地图种子恢复匹配记录，不补生成未揭示房间。

## 跨进程探索保存

Single Player与TCP/IP本地角色使用同名`.d2xmap`，路径来自hosting已持有的角色租约，不能通过UI姓名解析或覆盖其他D2S。原服账号角色不写本地探索侧文件。手动Save／Reload、正常离局、进程正常退出和已观察到的断线都会保存本人记忆；强制终止进程不保证最后一次探索落盘。偏好client-settings.json独立保存。

版本2、规则`native-visible-terrain-v1`同时保存二值探索掩码、布局指纹、已观察的原cel与城镇原变体／中心，不只保存格数。身份含角色姓名／职业、难度、各幕实际种子和内容指纹；后者包含MPQ挂载顺序、文件名／大小／修改时间及Levels／LvlPrest／LvlMaze／LvlSub／LvlTypes／Automap／Objects原表字节，并非安全签名。同一内容复制但修改时间不同会安全地从空探索开始。

单机每难度保留一份，TCP/IP另保留最近四个世界；两种模式互不覆盖。LAN客户端只保存自己实际看到的地形，不共享队友探索，不把本地D2S的旧seed当服务器seed。换幕后逐幕复验种子；已加载布局的尺寸／指纹不符时不画旧层。恢复不创建单位、碰撞或服务器房间兴趣。

侧文件上限16MiB，记录数量／尺寸／坐标／掩码均复验，使用校验和、原子替换和`.bak`备份；损坏、身份不符、旧版本或未知规则保留原文件并提示，不静默迁移master的旧版`.d2xmap`。原`.map`／`.ma*`的二进制互通未实现。D2S v96及角色规则v37不变，因为探索是客户端观察缓存。

本地原1.13c D2Client RVA5F3E0的`.map`索引保存四个seed并轮换`.ma0`–`.ma3`；D2MOO PlrSave2的单机恢复D2S地图种子条件用于交叉核对。当前侧文件是该生命周期的项目适配，未声称兼容原文件格式。

## 原图与标记

LvlTypes／Automap／Objects、maximap(s).dc6及当前DT1／DS1方向、style、sequence提供cel；普通怪物不画红点，物件只画原AutoMap标记。LvlPrest.AutoMap选择act2map(s)、act4map(s)、extnmap(s)；鲁高因按LutW／N分组拼接并跳原诊断帧，原1.13c偏移0xD2DB8与libd2跳帧表交叉核对。

动态单位标记采用原1.13c D2Client的0x5F1C0／0xD2DE8十二段闭合线形，不以DC6的221／317替代人物标记。0x61490与0x5F090核对本人RGB(0,0,255)、未组队玩家(255,0,0)、本人尸体(255,0,255)、同队(0,255,0)、NPC(244,244,244)、本人随从(68,112,116)、队友随从(72,160,52)及蓝／红门户共用(244,244,0)黄色标记；RGB沿当前MPQ调色板取最近索引。单位投影偏移为(8,-8)，小图另加(-1,5)，地图格与原Objects.AutoMap图标仍由原DC6绘制。Portal59／60虽AutoMap=0，原客户端另画动态标记；60在125–127／111／112／117的原排除分支保留，普通门／入口继续使用原表与地形图标。

队友资格只取本人和对方相同且非FFFF的原partyId；近处取真实空间单位位置，远处取0x7F区域及0x90坐标，限当前连续步行地图层。公开队伍坐标不创建空间单位、不触发地形揭示。尸体只使用8E已确认归属及真实可见单位，活人蓝色标记在死亡时撤除；其他人的尸体不冒充本人紫色标记。随从按原7A归属和当前MPQ PetType.automap筛选；敌怪及无归属的普通尸体不新增标记，不能以职业／外观猜归属。267箱子沿原特殊名称分支，NPC身份读MonStats.npc并保留原537–539排除项。姓名随Names；Show Party控制队友／队友随从标记及玩家姓名，未组队玩家红色标记仍可显示；离队、退出名册和新局随真实状态撤除。

## 输入与选项

默认关闭；Tab开关、V小图左右、方向键平移、Home居中、F12名称、Ctrl+F12项目截图。ESC→Options→Automap Options选Full Screen／Mini Map；Center When Cleared控制重开是否居中，Show Party控制原队友标记和姓名。

Fade是用户授权的既有显示适配：小图No／Everything／Auto，大图另有Center；No alpha255，Everything／Auto地形／物件alpha128，单位／名称不淡化，Auto目前等同Everything。Center中央半宽／半高矩形alpha128、外部255，跨边图块按像素裁分，平移不移动淡化区域；Center切小图回退Everything，非法／缺省配置回退Auto。强度、范围与循环不认证原D2Client算法。

## 证据与限制

原城镇全图依据D2MOO DrlgPreset整房AutoMap及pfTownAutomap；邻房／视野事件、原图布局与选项参考固定本地仓库和当前MPQ。四个参考没有完整D2Client揭示节拍／Fade程序，不能用D2Gfx TRANS枚举推导这些规则。

营地原大地图／NPC及五幕旅行返回探索有此前有限截图观察；最新包身份见[基线](../../../BASELINE.md#当前运行包与验证边界)。2026-10-10动态标记依原版截图、D2MOO公开队伍包及本地原客户端静态程序补齐，该批TCP路径与未覆盖范围见[组队有限证据](../../modules/NETWORK.md#组队有限证据)。

2026-10-11最终Windows Release包的单机跨进程观察位于`artifacts/act1-map-closeout-20261010`：Hero死灵角色副本、普通难度、种子2044627023。关闭地图探索鲜血荒地后，营地2337格／鲜血荒地245格、801个原图元写入版本2侧文件；`recheck/before-normal-quit.json`、`normal-reloaded-town.json`及最终包`verified-entry.json`确认正常保存退出后的新进程保留同样格数／图元，回到野外仍保留。`autosave-before-quit.json`与`autosave-reloaded.json`另确认新增野外探索后不手动Save、直接正常退出：鲜血荒地292格／842图元在新进程恢复。没有重建未揭示房间或分享队友探索。

`new-seed/entry.json`使用同姓名／职业新角色及旧侧文件，服务器实际seed566203250，只有营地2337格／747图元，没有旧野外245格。`old-version`、`bad-checksum`与`trailing-bytes`中的entry／preserved记录确认原文件错误提示、角色正常入局和退出后SHA256未变。没有改动原角色或MPQ，未新增测试脚本／专用程序。TCP/IP保存／四世界轮换只实现并静态核对，本批按要求未联机冒烟；全部缩放／侧栏／光照组合、五幕跨进程、难度／内容指纹变更、原服逐场景一致性及Linux未验收，旧离线168格证据不代替本批。地形规则见[地图](MAPS.md)，所有权见[地图模块](../../modules/MAP.md)。
