# 资源加载

PD2与CN散装资源、中文覆盖及挂载顺序；通用MPQ读取见[MPQ资源](../resources/MPQ.md)。 目标身份与证据口径见[版本与证据](BUILDS.md)。

## 更新与兼容性检查

单机／TCP/IP、Pd2Local和CN前端共用`resources/pd2/archives.cpp`的资源挂载：原目录之后依次读取`patch_d2.mpq`、`pd2data.mpq`、`pd2assets.mpq`、`pd2maps.mpq`、`pd2monchars.mpq`；存在的`Data/local`覆盖同名成员。所需MPQ必须存在并能被StormLib打开，表、成员、格式及未实现程序由实际消费者检查。整包SHA-256仅用于定位分析证据，不作为启动白名单；更新图形、地图或表数据不要求逐次登记包摘要。更新后重启D2X以重新准备资源，不能在同一进程混用更新前后的缓存。

CN账号认证在登录时检查实际所需文件、MPQ密钥资源及长度，随后按服务端挑战对实际Game／Bnclient／D2Client计算CheckRevision，不要求这些文件匹配旧样本摘要；BH报告同样由所选磁盘文件实际计算CRC32。未知认证模块、公式、消息／物品格式仍由对应消费者明确拒绝，服务端拒绝也不伪造为成功。协议兼容的资源／文件小更新无需重建D2X，资源可加载不等于新核心所有规则已复刻，存档格式变化仍不静默迁移。

核心摘要只选择已核实的扩展计数映射，不再阻止账号登录；9c2b、17d5及41d49分别使用已核定的构建标识，41d49的完整选择表与地图代码和17d5相同，见[F63](parity/ITEMS.md#f63-41d49核心计数映射)。其他未知核心不猜最近版本，保留原0x85计数数组及0x86未映射增量，`pd2State.coreMaterials.mappingAvailable=false`。CN共享／材料仓库的独立协议和权限不受该映射替代；这一可选元数据限制不阻止正常单机、战网认证或普通游戏输入。

## 空事件标记

当前Objects 594（empty_eventmarker）使用ZZ原资源。`pd2assets.mpq`的`data/global/objects/zz/cof/zz{nu,op,on}hth.cof`头部为零层、一帧、一方向；这是没有绘制组件的合法组合，不能当作损坏COF中断整张地图或补造图形。通用COF解码保留真实帧／方向，Graphics在零层时不创建GPU动画；真实物件身份、房间和事件消费者仍保留。其他非法头部／截断数据继续拒绝，错误附带实际COF路径。

<a id="资源加载散装中文成员"></a>

## 资源加载：散装中文成员

来源：[docs/resources/MPQ.md](../resources/MPQ.md)。

D2X 自己另有散装成员入口 `Archives::mountLooseDirectory`：PD2 CN 副本把中文字符串表、字形和菜单图放在散装 `Data/local`（无对应 MPQ 成员），启动时登记为 `data/local` 前缀下的成员，`read`／`contains`／`list` 先查散装再查已挂载 MPQ，并与 MPQ 成员共用同一读取缓存和 `used` 记录。该入口只覆盖登记的根目录前缀，不改变 StormLib 在 MPQ 之间的搜索顺序；原版 profile 不登记任何散装根，行为不变。

<a id="资源加载启动覆盖顺序"></a>

## 资源加载：启动覆盖顺序

来源：[docs/development/BUILD_AND_RUN.md](../development/BUILD_AND_RUN.md)。

PD2 CN 副本把中文字符串表、字形和菜单图放在散装 `Data/local`，启动时 `Archives::mountLooseDirectory` 把它登记为 `data/local` 下的成员并优先于所有已挂载 MPQ，因此该 profile 不依赖补充包。

## 认证资源证据

PD2账号大厅的新增静态依据为固定CN核心`9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f`与Bnclient `3631ca889dfa3cb149dd5b1722d3231851661b40514d4fbb4f00c529e76a56f5`：核心RVA1E7560／1E75C0／1E7620／1E76E0、3943E0、393F80、3947E0恢复分组／掩码、MPQ比较与0x51；Bnclient RVA158F0／15960／15E70／16070、12260／12390／125E0、16810／16EC0恢复认证资源解码。原资源、密钥和目标DLL不提交或分发，不执行native模块；算法范围、限制及当前源码状态见[DLL证据](../resources/PD2_CN_DLL_ANALYSIS.md#三个掩码与原认证资源)。新增SHA-256按[NIST FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf)第4.2.2／5.1.1／6.2.2节实现，不表示经过NIST认证。
