# 自动地图

自动地图只有一套揭示、原标记、原图缓存和绘制入口。RemoteTown适配原服房间／位置及活动地形，AutomapExploration拥有本局区域记忆，AutomapCatalog选格标记，SceneAssets载原DC6／城镇拼图，SceneView::drawAutomap绘制；没有联机专用绘制器或Local探索侧文件入口。

## 揭示与生命周期

城镇进入后准备原AutoMap已初始化房间或原整图变体／中心，全揭示沿safe区域规则。附近野外仍按活动房间与主场景实际可见DT1相交后逐格记忆；部分瓦片露出也纳入，屋顶／阴影／照明不参与揭示，联合矩形边角可能略宽。

关闭地图继续累积；大小／平移／Fade不改变探索范围。连续步行区域同层，楼梯／门户隔层；传送只采样落点，不补路线。同局换区／换幕后返回保留，新局清空，不补生成未揭示房间。

产品不读写.d2xmap、原.map／.ma*或服务器D2S；旧侧文件保存／迁移说明已退场，原用户文件保留。偏好client-settings.json与探索掩码分开。

## 原图与标记

LvlTypes／Automap／Objects、maximap(s).dc6及当前DT1／DS1方向、style、sequence提供cel；普通怪物不画红点，物件只画原AutoMap标记。LvlPrest.AutoMap选择act2map(s)、act4map(s)、extnmap(s)；鲁高因按LutW／N分组拼接并跳原诊断帧，原1.13c偏移0xD2DB8与libd2跳帧表交叉核对。

动态单位标记采用原1.13c D2Client的0x5F1C0／0xD2DE8十二段闭合线形，不以DC6的221／317替代人物标记。0x61490与0x5F090核对本人RGB(0,0,255)、未组队玩家(255,0,0)、本人尸体(255,0,255)、同队(0,255,0)、NPC(244,244,244)、本人随从(68,112,116)、队友随从(72,160,52)及蓝／红门户共用(244,244,0)黄色标记；RGB沿当前MPQ调色板取最近索引。单位投影偏移为(8,-8)，小图另加(-1,5)，地图格与原Objects.AutoMap图标仍由原DC6绘制。Portal59／60虽AutoMap=0，原客户端另画动态标记；60在125–127／111／112／117的原排除分支保留，普通门／入口继续使用原表与地形图标。

队友资格只取本人和对方相同且非FFFF的原partyId；近处取真实空间单位位置，远处取0x7F区域及0x90坐标，限当前连续步行地图层。公开队伍坐标不创建空间单位、不触发地形揭示。尸体只使用8E已确认归属及真实可见单位，活人蓝色标记在死亡时撤除；其他人的尸体不冒充本人紫色标记。随从按原7A归属和当前MPQ PetType.automap筛选；敌怪及无归属的普通尸体不新增标记，不能以职业／外观猜归属。267箱子沿原特殊名称分支，NPC身份读MonStats.npc并保留原537–539排除项。姓名随Names；Show Party控制队友／队友随从标记及玩家姓名，未组队玩家红色标记仍可显示；离队、退出名册和新局随真实状态撤除。

## 输入与选项

默认关闭；Tab开关、V小图左右、方向键平移、Home居中、F12名称、Ctrl+F12项目截图。ESC→Options→Automap Options选Full Screen／Mini Map；Center When Cleared控制重开是否居中，Show Party控制原队友标记和姓名。

Fade是用户授权的既有显示适配：小图No／Everything／Auto，大图另有Center；No alpha255，Everything／Auto地形／物件alpha128，单位／名称不淡化，Auto目前等同Everything。Center中央半宽／半高矩形alpha128、外部255，跨边图块按像素裁分，平移不移动淡化区域；Center切小图回退Everything，非法／缺省配置回退Auto。强度、范围与循环不认证原D2Client算法。

## 证据与限制

原城镇全图依据D2MOO DrlgPreset整房AutoMap及pfTownAutomap；邻房／视野事件、原图布局与选项参考固定本地仓库和当前MPQ。四个参考没有完整D2Client揭示节拍／Fade程序，不能用D2Gfx TRANS枚举推导这些规则。

营地原大地图／NPC及五幕旅行返回探索有此前有限截图观察；最新包身份见[基线](../../../BASELINE.md#当前运行包与验证边界)。2026-10-10动态标记依用户原版截图、D2MOO公开队伍包及本地原客户端静态程序补齐，本批自研TCP有限冒烟及包身份见基线；全部缩放／侧栏／光照组合、原服逐场景一致性和Linux仍待用户查看。旧离线168格保存重载不认证当前产品持久化。地形规则见[地图](MAPS.md)，所有权见[地图模块](../../modules/MAP.md)。
