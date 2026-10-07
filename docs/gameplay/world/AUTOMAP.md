# 自动地图

自动地图只有一套揭示、原标记、原图缓存和绘制入口。RemoteTown适配原服房间／位置及活动地形，AutomapExploration拥有本局区域记忆，AutomapCatalog选格标记，SceneAssets载原DC6／城镇拼图，SceneView::drawAutomap绘制；没有联机专用绘制器或Local探索侧文件入口。

## 揭示与生命周期

城镇进入后准备原AutoMap已初始化房间或原整图变体／中心，全揭示沿safe区域规则。附近野外仍按活动房间与主场景实际可见DT1相交后逐格记忆；部分瓦片露出也纳入，屋顶／阴影／照明不参与揭示，联合矩形边角可能略宽。

关闭地图继续累积；大小／平移／Fade不改变探索范围。连续步行区域同层，楼梯／门户隔层；传送只采样落点，不补路线。同局换区／换幕后返回保留，新局清空，不补生成未揭示房间。

产品不读写.d2xmap、原.map／.ma*或服务器D2S；旧侧文件保存／迁移说明已退场，原用户文件保留。偏好client-settings.json与探索掩码分开。

## 原图与标记

LvlTypes／Automap／Objects、maximap(s).dc6及当前DT1／DS1方向、style、sequence提供cel；普通怪物不画红点，物件只画原AutoMap标记。LvlPrest.AutoMap选择act2map(s)、act4map(s)、extnmap(s)；鲁高因按LutW／N分组拼接并跳原诊断帧，原1.13c偏移0xD2DB8与libd2跳帧表交叉核对。

NPC由MonStatsEx→MonStats2.automapCel；城镇交互中立NPC无非零cel时原蓝十字317为已有适配。原221／317帧已核对，但完整身份→帧、人物标记／精确锚点／动态门户规则未认证。原Portal59／60 AutoMap=0，不造标记。

## 输入与选项

默认关闭；Tab开关、V小图左右、方向键平移、Home居中、F12名称、Ctrl+F12项目截图。ESC→Options→Automap Options选Full Screen／Mini Map；Center When Cleared控制重开是否居中，Show Party保留偏好，队伍实体显示仍未完成。

Fade是用户授权的既有显示适配：小图No／Everything／Auto，大图另有Center；No alpha255，Everything／Auto地形／物件alpha128，单位／名称不淡化，Auto目前等同Everything。Center中央半宽／半高矩形alpha128、外部255，跨边图块按像素裁分，平移不移动淡化区域；Center切小图回退Everything，非法／缺省配置回退Auto。强度、范围与循环不认证原D2Client算法。

## 证据与限制

原城镇全图依据D2MOO DrlgPreset整房AutoMap及pfTownAutomap；邻房／视野事件、原图布局与选项参考固定本地仓库和当前MPQ。四个参考没有完整D2Client揭示节拍／Fade程序，不能用D2Gfx TRANS枚举推导这些规则。

营地原大地图／NPC及五幕旅行返回探索有有限截图观察；最新包证据见[联网记录](../../modules/NETWORK.md)。旧离线168格保存重载不认证当前产品持久化。精确锚点、NPC宽十字、全部缩放／侧栏／光照组合、队伍标记和Linux仍未认证。地形规则见[地图](MAPS.md)，所有权见[地图模块](../../modules/MAP.md)。
