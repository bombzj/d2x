# 自动地图

更新：2026-10-06。地形／连接规则见[地图](MAPS.md)，所有权见[地图模块](../../modules/MAP.md)。本页区分本地持久探索与联网本局记忆，不混用旧整房揭示记录。

## 当前揭示与保存边界（2026-10-04）

client/automap_exploration.*唯一维护探索。观察者邻房候选与主场景可见地形相交后才记忆：IMapAssetSource提供原DT1地板／墙像素包围范围，SceneView用世界投影、邻区偏移、更新后的摄像机和可用视口判断相交，部分瓦片露出也纳入。屋顶、阴影、透明像素、遮挡与照明不参与判定，联合矩形在边角可能略宽。

关闭地图照样累积；大小图、地图平移和Fade不改变探索范围。城镇进入后保留完整原城镇图，附近野外仍按可见范围累积。连续区域共享世界坐标候选，楼梯／洞口／门户隔层；传送仅采样落点，不补中途路线。邻房参考D2MOO DrlgDrlgRoom::sub_6FD77BB0的每轴间隔小于六地图格，以及D2Game 0x07发送链和OpenD2 MAPREVEAL定义。逐格视口选择按用户原版观察适配，本地reference无完整D2Client实现，不声称揭示节拍逐帧一致。

读取角色后恢复同名.d2xmap，核对角色、难度、地图种子及固定八张地图／标记表的内容指纹，首次加载每区再核对实际地形／房间指纹。不匹配明确清除对应旧层；未知版本、坏文件和角色不符拒绝覆盖。地图生成修订会改变地形指纹，不把旧探索静默套进新布局。

侧文件仍v1，规则标识d2x-visible-terrain-v2。已知旧d2x-near-rooms-v1提示停用，不转换整房记忆；下次保存重积累结果并备份旧文件为.bak。原D2S仍v96，不写私有尾段，不冒充原.map／.ma*。双文件保存失败边界见[存档](../../modules/SAVES.md#自动地图探索2026-10-04)。

## 联网本局探索

RemoteTown按当前局／幕／种子／区域／全局地图格记忆，RemoteScene提交实际可见原DT1实例；重用AutomapCatalog，不再起地图规则副本。关闭地图仍揭示，换区／换幕后返回保留，新局清空。连续步行组件同层，楼梯／门户隔层；传送只揭示落点。联网不读写本地.d2xmap或服务器D2S，也不生成未揭示房间来补全路线。

城镇原整图、物件AutoMap及中立NPC标记沿当前MPQ。鲁高因跳过原客户端诊断帧表，单机／联机共用；本机1.13c文件偏移0xD2DB8与libd2的lut_town_skip一致，不能画MPQ中的红叉。Tab／V／方向键／Home已接入，online-automap控制大小和显示；完整选项、名称、队伍显示尚未接入，当前地形／物件alpha128、NPC／本人不淡化，原客户端精确锚点／色表仍未认证。

实际包五幕旅行返回保持第一幕已探索格，城镇整图与大小图截图已查看。准确连服及服务端失败范围见[联网地图冒烟](../../modules/NETWORK.md#地图交互与探索冒烟)。本批地形算法、D2S v96和本地侧文件v1均未改变。

## 原图与标记

运行时读取LvlTypes、Automap、Objects及原maximap.dc6／maximaps.dc6，按LevelType、DS1方向／style／sequence匹配cel，不要求先找到DT1图形。裁剪取cel真实纹理范围，普通怪物不显示红点，物件只绘制原AutoMap标记。

专用城镇图沿LvlPrest.AutoMap选择act2map(s)、act4map(s)、extnmap(s)；鲁高因按LutW／LutN选两组，每组5×4行优先拼接，精确中心锚点／缩放未认证。第五幕冰洞Automap类型名称已映射原LevelType名称。

NPC通过MonStats.MonStatsEx查MonStats2.automapCel。当前城镇交互中立NPC无非零cel时采用原蓝十字317适配；人物保留既有十字。原红221／蓝317帧已核实，但身份到帧的完整客户端映射、人物标记及动态门户仍暂缓。NPC与Stash名称受名称开关控制，原Portal59／60的Objects.AutoMap为0，不造替代标记。

## 输入与选项

默认关闭，Tab开关，V切换小图左右，方向键平移内容，Home居中，F12名称；项目截图键Ctrl+F12。尺寸由Esc→Options→Automap Options的Full Screen／Mini Map选择。Center When Cleared为Yes时关闭再打开居中，为No保留内容偏移；方向键不移动地图显示区域。

Show Party保存队伍显示偏好，单玩家没有其他玩家实体，不把佣兵／召唤物当队员。侧栏占用后的视口是当前布局适配。选项保存在client-settings.json，不写角色D2S；未实现的Sound／Video／Configure Controls页面仍拒绝进入。

Fade按用户2026-10-02实测及明确近似授权：小图No→Everything→Auto，大图另有Center。No alpha255；Everything／Auto alpha128淡化地图地形／物件，玩家／NPC及名称不淡化。Auto暂与Everything一致，不随移动变化。Center在显示区域中央半宽／半高矩形内alpha128，区外255，跨边图块按像素分割；方向键不移动此区域。Center切小图回退Everything，缺省／非法配置回退Auto。强度、中央范围及循环顺序是适配，不认证原D2Client算法。

原选项DC6来自当前MPQ；OpenDiablo2 escape_menu提供菜单层级、循环及返回行为，game_event给出不淡化单位与重新打开居中的依据，其Fade消费者未实现。D2MOO本地无D2Client；D2Gfx TRANS枚举不能证明原Auto／Center混色算法，不据此继续猜测。

## 验证与限制

可见格揭示此前Windows Release隔离角色复验：区域3移动探索136→152→168，同进程保存／读档及新进程返回保留原168格，丢失0；不同返回落点新增至298，区域8原墙线截图已查看。产物在artifacts/automap-visible-smoke-20261004/fixed/，无用户角色档读写或新增测试程序。该证据验证揭示／保存，不能认证本批生成地图的所有视口与边界。

本批地图构建及运行范围统一见[地图验证](MAPS.md#本批验证)。精确原客户端锚点、Fade、NPC宽十字、全地图／缩放／侧栏／光照组合及Linux仍未认证。
