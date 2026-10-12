# 物品历史运行证据与验证边界

更新：2026-10-12。本页维护物品批次的条件、包身份、实际路径、失败样本与未覆盖范围。当前包另见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)；旧证据只覆盖对应源码，不认证当前新增分支。仅使用既有EXE、原服与调试管道，没有新测试脚本／用例／专用程序。

## 客户端与服务端物品核对

2026-10-12，当前`assets/mpq2`及本地D2MOO，Windows Release、D2S v96。忽略目录`artifacts/items-completion-20261012/`保留原表提取、`mpq-item-audit.json`逐配方／启用属性来源及表哈希、构建记录和后续运行副本；参考源码及原资源不纳入提交。

| 链路 | 源码核对与本批修复 | 依据／边界 |
| --- | --- | --- |
| 原库存请求／客户端 | 2A Cursor／地面直接入本人方块，63空Cursor背包入带，37最后成功购入身份鉴定；D2X Shift入带、列预览和原请求编码同步 | PlrMsg、ItemMode、D2Inventory、SUnitNpc；自动入带优先底格同类药水，明确拖放仍保留指定格 |
| 实际属性／保存 | 奥玛斯长袍随机技能、火炬随机职业的原stat107／83层；冻结编号和等级，未知函数不静默丢属性 | ItemMods PropertyFunc12／36与MPQ Properties.val；函数18未有启用来源，未实现自造时间规则 |
| 掉落 | NoDrop总人数包含已入场死亡玩家，同区域存活同队成员完整贡献；佣兵实际击杀叠加主人及佣兵MF／GF | Items_DropItemByMonster；TC、首杀、暗金一次限制及失败品质耐久沿现有路径，不替换概率 |
| 战斗到掉落的客户端资格 | 移除网络血量百分比为零即推断尸体的分支，仅原DT／DD模式判尸体；修复实际剩余不足1点生命时无法补最后一击 | MonsterMode的血量先整点取整，原0C可以对活怪报告零；不改服务端真实伤害／生命 |
| 金币 | 同区域同队自然金堆整除分配／拾取者余数，原存活人数除数及死亡队员分支、有符号余数，所有钱包及地面同笔提交；玩家手动／死亡丢金不分配 | ItemMode／Party及当前1.13c反汇编，已纠正D2MOO队友容量指针，见[GOLD](GOLD.md)；极限分支静态核对，未逐项录包认证 |
| 方块 | 146条enabled（36条Crafted），141条普通op与5条op28；现有修复、升级、词缀、孔、去孔、任务物品及门路径逐输入／输出登记。`useitem,rep,qty`补量含实际额外堆数，书本不套用；导入允许已实现属性函数12／13／36 | CubeParser／PlrTrade；`useitem`保留品质／等级，`usetype`才重生基底，未改正确分支；表级登记不等于146条运行验收 |
| 镶嵌／符文之语 | 原0x28实际子项、宿主类型、品质、顺序、孔数、冻结属性及D2S保留沿既有路径 | MPQ有78条complete，其中23条server；Realm Ladder过滤证据仍不足，见[SOCKETS](SOCKETS.md) |
| 商店／维修／鉴定／消耗／任务加工 | 正常请求、本人来源、NPC会话、金额、材料及库存输出仍由各系统准备和事务提交；客户端不执行第二套生成或消费 | 全商店刷新、折扣、充能、护符／套装条件、多尸体、争用／背压组合未逐项运行认证 |

牛门／钥匙门／最终门物品合成与地图准备已有；Pandemonium真实首领、固定奖励、完整世界活动及部分视觉仍是独立缺口，未用管理生成冒充自然事件。没有新增测试脚本、用例或专用程序。

客户端及独立服务端Windows Release构建、打包成功。最终客户端SHA256 `BF2FDDE219A2C12E2934C9E7115460D4D6435E4B583AA6574405EED42B179B21`，服务端 `796E0D45A1878B6FB049DC6C191A28562D0668727BBB953CBC41AD67E3D14FC4`，BNCS DLL `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7`；磁盘包与`build/bin`一致。以下用隔离存档、当前MPQ与既有调试管道操作正常客户端请求；管理生成／旅行／伤害只准备条件，不认证自然生成概率。

| 代表冒烟路径 | 实际结果及证据 |
| --- | --- |
| 拾取、鉴定、关闭方块直接入箱、合成 | 六件准备物拾取，实际鉴定卷轴消费；三枚碎裂紫宝石经2A入箱，再由77／4F合成一枚裂开紫宝石。`smoke/picked-items.json`、`cube-closed-items.json`、`belt-cube-items.json` |
| 同类跨等级自动入带及随机属性 | 腰带扩为8格，hp5由63放入底格hp1同列上格（原Slot4）；奥玛斯长袍实际stat107层51、等级3。正常保存后最终包新进程恢复方块内gfv、腰带位置及相同随机属性。`smoke/reentry-items.json`、`reentry-server.json` |
| 真实双客户端TCP组队分金 | 41012隔离宿主，双方实际入同区域且远距同队：自然TC的45金分为拾取者23、队友22；玩家手动丢100金拾回不分队友。双方原回包余额一致，正常退局保存；最终包新进程恢复队友799金。`tcp/*/shared-gold-after.json`、`owned-gold-after.json`、`tcp/host/final-server.json`、`tcp/guest/reload-server.json` |
| 最终包低生命最后一击、实际TC掉落和拾取 | 自然沉沦巫师经Fire Bolt及管理扣1点准备为0.97265625生命，客户端原mode3、lifePercent0；正常右键Fire Bolt成功击杀，原mode12、rewardComplete，TC掉出lbl，走近后正常拾入背包。`smoke/final-low-life-server.json`、`final-low-life-client.json`、`final-drop-server.json`、`final-drop-items.json`、`final-picked-drop.json`、`final-server.json` |

前三条操作包客户端SHA256为`AAAE6C68B9D8FD3F7B6783DFA4FF8DCB1C4BE93AC8D6DE83FC83116214F73EFA`：已含全部本批物品／分金修复，仅缺随后补齐的客户端低生命尸体判定；最终包另做上述新进程重入及最后一击。对应宿主failures=0、characterIssues为空，进程stderr为空，均正常退出。中间发现旧客户端拒绝对不足1点生命的活怪施放，修复及最终复验如上；Town零等级箱子无TC档位、过远拾取等准备请求明确拒绝，不计作链路通过。

未启动PvPGN、未重做原版人工Join、Linux、噩梦／地狱及全部配方／品质／概率运行验证。佣兵MF／GF、购入鉴定37、地面直接入方块及死亡队员／容量极限分金为源码与原版证据核对，未逐项冒烟；23条server符文之语模式资格和世界事件限制仍见上表，不能宣称全物品功能已完整认证。

## 相关功能收尾复核

2026-10-11 Windows Release、当前`assets/mpq2`、D2S v96；最终客户端SHA256 `904F10248A000AA2F7CEE3D51F464591777B46C40CE8C45C0B6E62CD04699A4C`，独立服务端 `0ECBC1F47DFD4E5AE0B1460ADA6CBB20363B6991A64928AE4C68849004213165`。静态核对手位、命中来源、持续状态复制及事务重试，补齐环境弹体对伙伴成功格挡时遗漏的公共BL／恢复入口；容量失败仍保留接触随机及目标阶段。两目标构建、两包打包成功，磁盘包与`build/bin`一致。

使用隔离84级法师与22级野蛮人的存档副本，从最终`dist/current`新进程启动；管理旅行、人数、敌人及受伤只准备条件，施放／攻击／装备／保存使用既有正常入口。证据在忽略目录`artifacts/weapon-effects/closeout`。

| 最终包代表路径 | 实际结果与证据 |
| --- | --- |
| 元素法术隔离 | 保留四项武器效果装备，对新P8 Andariel施放右手Fire Bolt：生命4608→4599.24609375，伤口字段全零，无147／151／152新叠加。`final-firebolt-before/after-server.json`、`final-firebolt-before/after-client.json` |
| 近战四项效果及喷血 | 同目标普通攻击：生命4599.24609375→4006.375，法师生命106→134.15234375、法力199.28125→201；伤口rate2461、source／credit1，原147／151收到，客户端实际生成18／19／21血粒子。`final-melee-before/after-server.json`、`final-melee-client.json` |
| 野蛮人握持与物品说明 | 前一进程正常保存双剑后，最终包新进程恢复右手Two-Handed Sword／左手Claymore；人物伤害3–14。正常卸下左剑后为13–28，原双手握持与物品说明单手2–9／双手8–17实际可见。`final-hands-reentry-server/items.json`、`final-hands-dual.png`、`final-hands-alone-tooltip.png` |
| 普通攻击别名与资源／装备重入 | 法师保存后重入，原选择[-1,36,-1,-1]、Cathan's Seal／Manald Heal与两组装备恢复；不重选普通攻击即完成上列近战。`reload-server/items.json`记录收尾前同包重入，最终包原请求及装备副本在`final-firebolt-before-client.json` |

收尾前的`083FB6…`包另复查剑盾3–14→单剑13–28→双剑3–14，`hands-*.png/json`保留；不将这些旧包路径计为最终包逐项复测。最终两个进程宿主failures=0、characterIssues为空，客户端ProtocolReady、ignoredPackets=0、unavailableUnits=0、mapErrors为空，stderr为空，均正常save／quit。既有组件着色／无形透明限制仍保留。

没有新增测试脚本、用例或专用程序；`ctest -N`发现旧缓存登记的engine指向2026-09-12残留EXE，当前CMake没有该登记，因此未运行它。环境弹体伙伴格挡与背压分支为源码核对，此次未专门构造陷阱场景；独立服务端仅构建／打包，未启动PvPGN。全部职业／武器／难度、TCP多人、Linux及逐像素一致性未整体认证，历史证据保留下述各自包身份。

## 武器命中动画有限冒烟

2026-10-11 Windows Release、当前`assets/mpq2`、隔离D2S v96法师84级；最终客户端SHA256 `083FB6673C58109F701896A0055A4D9BD8766EF1F485748781A5C75B0ED018C9`，独立服务端 `CB6F7DB6C25FD231125BA8AAF210169743042C87C5C791C15E765312E13F1B7D`。两包构建、打包成功，客户端从`dist/current`新进程启动，单机内存宿主与原C2S请求完成下表验证。证据在忽略目录`artifacts/weapon-effects/visual`；未提交Git，没有新增测试脚本／用例／专用程序。实际规则与只读原版DLL证据由[命中动画](../combat/WEAPON_EFFECTS.md#客户端动画)维护。

| 代表路径 | 当前包实际结果与证据 |
| --- | --- |
| 单独吸魔与压碎 | 只装备Manald Heal吸魔戒指，对P8 Andariel普通近战；原本人152与目标147收到，截图实际显示蓝色吸魔与红色压碎叠加。`blood-mana-client.json`、`blood-mana-contact.png` |
| 单独吸血与压碎 | 原take／equip／unequip／place换为Cathan's Seal，移走Manald；对新P8 Andariel普通近战，原本人151、目标147收到，截图显示吸血原图。`blood-life-client.json`、`blood-life-server.json`、`blood-life-contact.png` |
| 持续喷血与地面血迹 | 原状态62持续解码；Andariel的MPQ Bleed2使本地18／19／20／21均实际出现。血粒子完成9帧后位置停止、remaining持续128/25秒，原DCC血迹可见。`blood-mana-client.json`与截图；不是仅凭服务端扣血认定动画成功 |
| 离区、回区与首次状态快照 | 离开鲜血荒地后血粒子为0、旧目标状态移除；返回仍有伤口的Andariel时，新原状态快照sequence1含62，重新喷血2粒。`blood-left-client.json`、`blood-return-client.json` |
| 死亡停止喷血、保留落地血迹 | 管理kill仅走原怪物伤害事实；生命与wound归零、62移除。两次后续读取血粒子均108，最旧粒子age26.16→27.86、位置相同，未新喷血。`blood-dead-client.json`、`blood-dead-later-client.json`、`blood-dead-server.json` |
| 到期与重新赋值 | 第二个Andariel接触后离区，分批推进200权威帧并回区；目标仍存活2192.83984375，wound全零，新状态快照为空且血粒子0。`blood-expired-return-server.json`、`blood-expired-return-client.json` |
| Drain零 | 原skeleton1 Drain0被普通近战击杀，仅目标147，无本人151／152，血粒子0。`blood-drain-zero-client.json`、`blood-drain-zero-server.json` |

中间包 `1ADE28BDBC994E51FCE9292E0FEAA4B8733AEEBB4BE7D151E9F88C21FFF560E1`暴露服务端怪物快照漏掉wound，首次开启后sequence3又变为空；`final-contact-*.json`保留。修正快照后，中间包 `12E11AAA27F3069C8DFCE1143F6A407A335DE75A074B172AD4891B608A0D9D1B`已保留62，但误用MonStats2.localBlood使Andariel无血；`fixed-*.json`及原D2Common字段装载证据保留。最终包改读原0x11D的Bleed，已按上表复测。

当前最终进程ProtocolReady、ignoredPackets=0、unavailableUnits=0、mapErrors为空、宿主failures=0、characterIssues为空、stderr为空；正常save／quit收尾。既有装备组件着色／无形透明限制仍在effectLimitations，不能写成全部为空。独立服务端仅构建／打包；此批未启动PvPGN链路，也未复测全部职业／技能、三难度、TCP多人或逐像素原客户端一致性。下节结算批次的历史证据保持对应旧包身份。

## 武器命中效果有限冒烟

2026-10-11 Windows Release、D2S v96、当前`assets/mpq2`，未提交工作区；最终客户端SHA256 `9F47D5711CC30C4FC594A1E8E1EA596D3C178528114B22973F4B038B8CBAC8B3`，独立服务端 `2BFB0210A57F2E373FFE0798153DB2EF4C490D8E24F3BE98B96F16E937BE613E`。两包已构建、打包；使用原游戏、原C2S操作及既有管道，没有新增测试脚本／用例／专用程序。证据在忽略目录`artifacts/weapon-effects`，规则与原版依据见[武器命中效果](../combat/WEAPON_EFFECTS.md)。

隔离法师84级、普通难度；管理经验及生成仅准备条件，鉴定、装备、选技与攻击用正常请求。原MPQ Fleshripper／Swordback Hold／Rattlecage／Goblin Toe／Guillaume's Face提供本件及全局压碎／撕裂，Cathan's Seal吸血6%、Manald Heal吸魔5%；近战概率累加后压碎／撕裂均100%。副组为Riphook左手、右手原箭，本件吸血9%、撕裂30%，全局压碎85%。图与原属性记录见`sor/final-reentry-items.json`、`bow-equipped.json`、`riphook-tooltip.png`。

| 代表路径 | 最终包实际结果与证据 |
| --- | --- |
| 近战四项结算 | P8原Andariel、Drain100：生命4608→3978.140625；法师生命104.9296875→135.28515625、法力185.453125→201。原Overlay147／152收到；伤口rate2461、source／credit1。`sor/final-melee-before.json`、`final-melee-after.json`、`final-melee-client.json` |
| 刷新、背压及到期 | 再次命中until1147→1165，原rate／source保留；换区后大批单步使next落后，恢复后补齐，最后191帧总扣1836.13671875，目标3346.98046875→1510.84375，最后一帧后状态归零。`final-wound-refresh.json`、`final-wound-backed-up.json`、`final-wound-expired.json` |
| Drain零及死亡清理 | 原skeleton1 Drain0，生命40.5→0；仅目标Overlay147，无本人151／152；9帧生命+0.24609375、法力+0.703125均为原装备／人物自行回复，未吸取。`drain-zero-before.json`、`drain-zero-after.json`、`drain-zero-client.json` |
| 远程、相反手弹药及过量伤害上限 | 新进程单次Riphook普通攻击，P8原brute1生命49.5→0；右手最后1箭消失，原147／151收到。远程压碎为1.375，剩余48.125限制物理吸取；30帧生命96.328125→104.3671875、法力202.6875→207.90625，扣除自行回复后吸血7.21875／吸魔2.40625，符合15%／5%及原固定点取整。`range-clean-before.json`、`range-clean-after.json`、`range-clean-client.json`、`range-clean-ammo-after.json` |
| 保存及新进程重入 | 正常quit后以同一D2S启动新进程，原装备及掷值、武器组、等级和技能恢复；不重选普通攻击即成功命中。新P1 Andariel接触后生命614.09375，之后不再攻击，伤口单独击杀，rewardComplete=true、经验1000000000→1000000088，状态清零。`reload-snapshot.json`、`reload-items.json`、`wound-kill-contact.json`、`wound-kill-after.json` |

中间包客户端 `5E1272BEBEA7AD73FDC30BE8DCAEF5B01130924AC669A74544B6A728E1F7C430`的大批单步暴露了输出容量不足导致伤口漏帧，证据`wound-penultimate.json`／`wound-expired.json`保留；最终包改为成功提交后推进欠付帧标记并补齐，已按上表复测。首次启动用小写职业名被菜单拒绝，改用MPQ准确名称Sorceress。远程早期18帧快照及交错操作不足以单独认证吸取，最终另起进程以单次攻击完成核对，早期`bow-*.json`保留。Andariel条件运行中法师曾被正常AI击杀，之后按原复活／尸体恢复请求继续；这不是无受伤场景。

最终宿主failures=0、characterIssues为空、ignoredPackets=0、mapErrors为空，日志无stderr错误；已正常退出本轮进程。客户端仍明确报告既有装备分部件染色／虚无透明未实现，不把此限制写成通过。独立服务端完成同源构建／打包，未启动PvPGN／原D2GS；没有穷举技能、三难度、多人、概率分布、全部装备／触发组合、Linux或逐像素表现。盾击、纯元素、多重箭中央资格及反噬重试为源码／D2MOO核对，未宣称本轮均运行通过。

## 双手剑与左右手有限冒烟

2026-10-11 Windows Release、D2S v96；最终客户端SHA256 `1AB4AE1192705708BE31153BE56D6E1F66E33CD2B221EF4484DD0C7731F68C92`，独立服务端 `C64519088C3136685DC31B4D37FF79E41F1063979619E11631263C4DA3FC69A1`。两包已构建、打包；证据在忽略目录`artifacts/barbarian-hands`，规则及D2MOO依据见[EQUIPMENT](EQUIPMENT.md#手位与双手剑)。抽取代表路径，没有新增测试程序。

隔离普通难度22级野蛮人与圣骑士；经验、原MPQ物品和目标由既有管理入口准备，属性加点、装备、切组和普通射击沿游戏原请求执行。最终包的新进程实际验证如下：

| 典型路径 | 最终包实际结果与证据 |
| --- | --- |
| 野蛮人单剑／剑盾 | 原Two-Handed Sword单独装备时，人物伤害13–28；另一手装Buckler后为3–14。原图握持随装备变化，物品说明同时列单手2–9／双手8–17。`final/sword-alone.json`、`sword-shield.json`及同名截图 |
| 野蛮人双剑／两组重入 | I组右手Two-Handed Sword、左手Claymore，II组左手Short Bow、右手Arrows。正常保存后的新进程恢复等级、活动II组和四个原body位置；切回I组能显示双剑，Claymore说明列单手5–12／双手13–30。`final/reentry.json`、`dual-swords.png` |
| 左手弓／右手弹药及命中 | 新进程不重新选技能，普通射击生成原箭弹，右手最后1支箭耗尽移除；目标生命7→5.55078125。`final/bow-release.json`、`bow-impact.json`、`bow-events.json` |
| 非野蛮人说明 | 圣骑士新进程恢复双手剑，说明仅列双手8–17，左手为空、原盾牌在背包；原双手握持可见。`pal/final/reentry.json`、`twohand-tooltip.png` |

中间包客户端SHA256 `046FBA3EA5D14635C2EF25D1924B68EF0ABB30C02D6DF699B5D8BE37D812A9E4`实际完成从剑盾替换成圣骑士双手剑：原剑进入Cursor、原盾移入背包，见`pal/twohand-replace.json`。该包首次重入射击出现notImplemented：存档内既有普通攻击别名-1未按原技能0分派；`barb/reentry.json`、`bow-before.json`及`bow-events.json`保留条件与失败。最终包在运行分派归一化该别名，未改变原D2S字段；上述最终射击成功不抹去中间失败。

最终两角色宿主failures=0、characterIssues为空，ignoredPackets=0、mapErrors／effectLimitations为空、stderr为空；正常退局后进程已退出。独立服务端只构建／打包，未启动PvPGN／原D2GS。没有穷举全部武器、职业、双持专用技能、破损／需求失效组合、三难度、多人、精确动画逐帧／像素或Linux；管理准备不认证自然掉落或升级全流程。

## 历史物品经济包

2026-10-09 P1／P5及原生物品身份布局收尾已完成Windows Release构建，该批运行包当时在`dist/current`。EXE SHA256为`740E8E7B392BFBF0DB4E17F28A78AF4945439074058991E812AAA10D4094C3C8`，网络DLL为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`；该批包当时与`build/bin`一致。最终构建成功，证据在忽略目录`artifacts/items-economy-smoke-20261009`。打包曾因退局后进程尚未退出而失败，等待退出后成功，失败日志保留。没有新增测试脚本、用例或专用程序。

普通难度临时女巫Hero，地图种子3666265640；经验／金币和MPQ物品由既有管理入口准备，正常操作仍发原C2S。该历史最终包的新进程实际验证如下，列出路径为有限V2，其余分支仍为V1：

| 实际路径 | 有限证据 |
| --- | --- |
| 原充能技能源／施法／热键 | Hexfire鉴定装备后，Hydra为6级、36次；营地正常走跑到鲜血荒地施法，充能36→35，法力不变。保存后新进程把来源重新绑定至GUID16，右技能与热键2一起恢复。final-reloaded-snapshot／items |
| 自恢复期间等待拾取 | Titan’s Revenge在地面自补充，人物靠近期间属性revision继续变化；原请求成功拾取，两端数量均为4。修复此前属性版本变化导致待拾取请求取消，改用独立的地面生命周期代次。final-restored-pickup／snapshot；旧失败样本保留 |
| 金币丢弃／拾取 | 原C2S丢100金币并拾回，钱包恢复500000。final-gold-dropped／recovered |
| 商店周期与充能修理 | Charsi货架63件在6000权威帧后重开为61件，与旧货架无GUID重叠；Hexfire报价6133，金币500000→493867，充能35→36。final-shop-before／after、final-repair-quote／repaired |
| 地面过期边界 | 普通hp1超过15000帧后两端移除；任务方块仍在地面且可解码。final-expiry-before／after／snapshot。没有验证所有品质、金币阈值或区域卸载分支 |
| Token与保存重入 | 学习Fire Bolt后使用Token，退回375属性点及75技能点，清基础技能、选择与16个热键；保存后新进程恢复同值，金币493867、Hexfire36次。final-token-before／after、final-save-info、reentry-snapshot／status |

最终新进程ProtocolReady，8件物品均decoded=true；ignoredPackets=0、unavailableUnits=0、mapErrors为空；宿主failures=0、characterIssues为空、stderr为空。已有组件着色／无形透明表现限制仍在effectLimitations中，不把它记为空或归因于本次协议。构建后的`save-info`保留原只读检查，并增加耳朵／Realm／Tempered／普通身体部件数量摘要。

原磁盘版本仍为D2S v96。耳朵、身体部件、Realm尾部及Tempered布局已有源码支持，未取得符合准入条件的真实特殊身份存档进行往返；既有EpicSorc样本因Hardcore／dead／Ladder资格被拒绝，不能算布局验证。原38／58任务加工成功、全部方块配方、牛门／Pandemonium、首杀及特殊奖励、三难度、多人争用／背压、原D2GS与Linux均没有该批运行认证。任务加工的后续各幕证据单独列于下节，不能用后续成功抹掉此批未覆盖范围；当前仍缺的队伍NoDrop、其他职业及专用物品程序见所属专题，[P5](../../architecture/MULTIPLAYER.md#p5完整物品配方与城镇经济)保持开放。

### 历史物品与掉落有限冒烟

此前物品包的有限V2证据保留在忽略目录`artifacts/items-closeout-20261009`：普通方块／Crafted、Stealth镶嵌及回购、AutoMagic、个人赌博、批量书本／药水、耐久维修、自然普通掉落拾取与保存重入。其源码／包身份及失败样本留原日志；历史成功不认证本轮新增分支。当前功能、原规则依据与限制由分类专题维护，入口见[物品目录](README.md)，避免重复记录旧范围或把旧数值当作当前认证。

## 任务加工、消耗与组装证据

| 历史批次 | 物品相关有限证据与限制 |
| --- | --- |
| 第一幕 | 原马勒斯生成／拾取／授予灌注，白cap经原38／58加工为Rare并保存；准备等级／输入，非自然全流程通关，详见[第一幕任务](../quests/ACT1.md#有限运行证据) |
| 第二幕 | 原材料／方块组装、技能书使用及插杖／材料消耗与新进程恢复，详见[第二幕任务](../quests/ACT2.md#有限运行证据)；管理准备不认证自然收集／真实Boss战斗 |
| 第三至第五幕 | 生命药剂／抗性卷轴、熔炉奖励、Larzuk打孔与Anya署名及保存，详见[ACT3–5](../quests/ACT3_5.md#依据与运行证据)；预先准备意志不认证三器官／方块完整链 |

## 其他代表证据与尚未覆盖范围

### 职业装备与原版拒绝对照

2026-10-12用户操作现有Windows Release客户端，EXE SHA256 `BF2FDDE219A2C12E2934C9E7115460D4D6435E4B583AA6574405EED42B179B21`、当前MPQ、女巫ShotSorceress。忽略目录`artifacts/diagnostics/20261012-074253-556462fc`保存S2C记录及只读快照。自研单机拒绝ktr装备后，权威GUID24仍在Cursor、原dgr仍装备，客户端同样保留mode4，但UI隐藏至超时；命令为invalid-request，未发生物品删除。源码补齐职业预览／提交检查及已确认Cursor的持续显示，不把这个旧EXE现场当新修改运行通过。

随后原版1.13c Host／D2X Join入场以9C/action18指派ktr GUID15到Cursor（本人GUID2），客户端发送计数含两次18放置和一次1D装备交换。用户报告放回背包和装备均超时；此局除了入场指派，没有ktr后续物品位置／删除回包。原2C消息为NPC GUID7的事件0x12，不能当本人库存拒绝ACK；正常退局收到B3／B0／05／06。旧记录未捕获C2S原字节，也不保存每次请求参数历史，因此背包放置失败原因仍未确定，不能归因于职业限制或补造通用ACK。新双向诊断用于对照原版Join／自研Host的实际GUID／x／y／InvPage／body及宿主处理结果，人工操作待完成。

本地D2MOO PlrMsg::Rcv0x18及ItemMode::sub_6FC45E60用于解释资格分支：背包原InvPage为0，S2C存储page为InvPage+1；装备交换的身体／职业资格可在无物品回包的分支直接返回。存档Cursor加载另核对PlrSave::sub_6FC895E0及sub_6FC4B9D0，不能仅凭入场手持指派断言原版指针恢复失效。当前存档格式和旧档保持不变，证据不足部分保留为开放项。

原版Join／自研Host使用诊断EXE `34BA7883B448FCBEB01E70E5451764E8A8DACACFABDF7BD22E88C1D06FFC78E3`，用户报告原版单机库存正常，加入后卸装／拿取无鼠标反馈。`artifacts/item-protocol-20261012-080843/host-packets.jsonl`连接3记录C2S1C/body4，随后S2C9D/action8把dgr GUID205移到Cursor；后续重复1C及19/GUID204、203没有位置回包。恢复存档也保存dgr为mode4。因此这不是所有拿取请求都未执行，首次卸装已提交但客户端未呈现，之后Cursor占用阻止继续取物。

该9D/action8的mode4、body0／x0／y0／page0与既有原版Host记录不同：`npc-host-comparison/original-items-received.jsonl`序号560为body4／x4，序号618为body8／x8；`original-combat-received.jsonl`序号1062为body3／x3。原版19取背包／箱子回包另保留来源page1／5。D2MOO ItemMode::sub_6FC45B30及Items::ITEMS_SerializeItemToBitstream确认移入Cursor时保留原body／坐标／InvPage用于移除原位置。源码改从InventoryFact.before提供这些仅用于原包的来源字段，并让装备x对应原body；不修改D2S格式或借此强制刷新原版。自有客户端仍以mode4判定Cursor，不要求body／page为零；兼容检查及修正版列出操作的原版人工结果见下文。

准备存档时的错误另明确保留：提供的1127字节`native-character/ShotSorceress.d2s`经物品头核对实际含15个记录（包括孔内子项），没有ktr或Cursor。C2S6C总长度也是1127，入场输出与该清单一致。因此用户此次两种模式“没有刺客爪”不能说明新宿主吞掉爪；该副本未复现最初条件。原件及原日志保留，后续含爪隔离副本须重新确认物品清单，不覆盖用户角色。

修正版Windows Release诊断EXE SHA256 `5B82BE77B893472E4BEBC30EB19B367936B28A685230AA5B26E522CEA122BDD4`，证据在`artifacts/item-protocol-20261012-090747-corrected`。使用现有程序和独立ShotSorceress副本，自有客户端经原1C卸下dgr，收到mode4／body4／x4；18放背包(7,0)、19拿取收到mode4／x7／page1、1A重新装备均确认更新，Cursor随回包变化。原包字段见probe-client／host日志，快照self-unequipped／self-backpack-taken；这不是原版客户端实机认证。

管理入口按当前MPQ生成普通ktr，仅准备对照条件；真实16拾取到Cursor、客户端拒绝异职业Equip（`This equipment is for another class`）、18放背包及19拿回Cursor后正常quit保存。权威failures=0，准备进程正常退出、stderr为空。新的1155字节副本已只读核对含16个物品记录及ktr/mode4，save-info通过既有D2S v96校验；没有覆盖原角色或沿用先前错误副本。修正后的8人普通难度Host已恢复到ProtocolReady，原版保存重入尚未验证；旧失败现场保留。

用户随后用原版1.13c Join完成操作并确认鼠标显示、拿取／放置均正常，无消失或超时。连接3的原18把ktr GUID206放背包(8,0)，19拿起返回9D/action5、mode4／x8／page1，再18放回；原1C/body4卸下dgr GUID205返回9D/action8、mode4／body4／x4，随后18放背包(7,1)。前五次权威命令快照均applied、Cursor为空，两件物品位置与回包一致，failures=0、characterIssues为空、stderr为空；随后原19拿起dgr及1A/body4重装也收到对应9D/action5／6（序号1257／1265）。现场为vanilla-after-actions-status／snapshot／item-fields及host-packets原日志。没有收到1A／1D装备爪的请求，用户确认原版本地直接禁止异职业装备；这是原版已有规则，不是D2X给原版新增的检查。D2X预览／提交补齐同一职业限制，自有客户端上述拒绝另有证据。此轮不认证原版箱子／方块／腰带及保存重入；反向原Host对照见下文。

同一修正版D2X Join原版1.13c Host（ShotDruid）采用独立1155字节含Cursor爪的ShotSorceress副本，证据`artifacts/item-protocol-20261012-093315-original-host`。入场ktr GUID16为mode4，原18立即放背包(8,0)、19拿回Cursor均Updated；异职业Equip在客户端直接拒绝，不发送1A／1D。实际UI装备位为红色，点击后Cursor仍为16、无待发队列或Pending，截图claw-hand-preview／claw-denied-click及claw-after-denied-click保存。再18放回，原1C卸下dgr GUID15、18放背包(7,1)、19拿起及1A/body4重装全部确认，无超时或连接错误。request-comparison把物品GUID归一到类型后，与此前原版Join／D2X Host七条C2S的包号／坐标／page／body逐条一致；原Host的卸装回包同样保留body4／x4，背包拿取保留来源page1。当前请求编码未发现该组差异，旧只有S2C的失败记录仍不足以断言其历史根因；本次同类隔离条件未复现旧放置失败。

反向保存使用正常69退局，收到B3／B0／05／06返回选角，无错误；本机隔离D2S写为1150字节。同进程重入及随后全新进程再次Join都恢复ktr为背包(8,0)、dgr为右手body4，Cursor为空，decoded=true；证据after-save-leave／rejoined／fresh-process-reentry及独立reentry原包日志。两个D2X进程均正常quit退出，stderr为空，用户原版Host保持运行。没有重建／打包或修改该固定运行副本，也不认证全部容器、存档组合、三难度、多人争用、原服账号环境或Linux。

历史873ac34的`artifacts/closeout-smoke-20261008`保留库存／腰带／装备／切组、成长加点、两区旅行、普通近战／初始法术及跨进程保存代表路径，同机TCP观察同房互见／移动／成长隔离及嵌入宿主双房，不认证独立EXE或跨机器。历史8623446的`artifacts/p1-smoke-20261008`保留掉落／金币／鉴定、药水、普通死亡／拾尸、物件／商店／仓库、洞穴奖励与回城保存；普通箱桶不等于陷阱或特殊机关。

通用卷轴／书本及弹药／药瓶见[GENERAL](../skills/GENERAL.md#有限运行证据)，施法表现中的开门代表路径见[COMMON](../skills/COMMON.md#通用施法表现有限证据)。原服交易／金币、药剂和商店的既有有限观察归[NETWORK](../../modules/NETWORK.md#既有有限证据)，不复制其失败和包记录。

管理员经验／金币／MPQ生成只准备条件，不能证明正常掉落概率、任务材料来源、全经济循环或完整战斗触发。全部物品／属性／品质组合、配方、特殊身份样本、牛门／Pandemonium、首杀掉落、三难度、多人争用／背压、精确报价、音频／像素与Linux仍未整体认证；明确范围以所属批次与专题为准，不能把空诊断当全量验收。
