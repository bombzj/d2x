# 资源加载

PD2与CN散装资源、中文覆盖及挂载顺序；通用MPQ读取见[MPQ资源](../resources/MPQ.md)。 目标身份与证据口径见[版本与证据](BUILDS.md)。

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
