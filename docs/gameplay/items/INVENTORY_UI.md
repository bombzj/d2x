# 包裹界面

更新：2026-10-18。本页只维护面板／手势及等待语义；权威移动与容量见[INVENTORY](INVENTORY.md)，装备资格见[EQUIPMENT](EQUIPMENT.md)，原图／反馈见[PRESENTATION](PRESENTATION.md)。

I／底栏按钮打开公共包裹，当前MPQ invchar6.dc6／invchar6Tab.dc6提供原石框和I／II武器标签；格子、部位、物品图及说明按原表读取。面板只消费InventoryView和提交意图，不访问可写库存或会话。

关闭按钮使用buysellbtn第10帧的原32×32尺寸，对齐invchar6背包底板凹槽内的(18,385)，相对旧坐标向左1、向上4原生像素；绘制和点击命中共用inventoryClose及侧栏缩放。此定位已完成原资源及调用链静态核对，并纳入当前Windows Release客户端包；未启动游戏，等待用户实测。

每职业背包／装备布局由ClassicData从当前MPQ inventory.txt的800像素职业行（名称后缀2）准备，减去该行invLeft／invTop成为原图内坐标；gridX／gridY／gridBoxWidth／Height及十个部位的四边／宽高共同校验。RemoteUiClients按当前职业投影backpackLayout／equipmentSlots／itemCellSize，绘制、装备／鉴定／维修命中、拖放和手持尺寸读取同一值，备用手共用主手几何；删除表现层的手写装备坐标和背包10×4／29像素参数。此项只更改只读布局，不改库存容量、物品事务或D2S。仓库、方块和交易格继续使用各自原表行。

金币输入改用当前MPQ dialogbackground的210×158原图、buysellbtn的确认／取消帧及strDropGoldHowMuch／strBankGoldDeposit／strBankGoldWithdraw文案，仓库上限读取GoldMax格式。首次输入替换预填金额，Backspace／Delete编辑，Escape／右键只取消弹窗；弹窗接管输入及提示，不穿透到下面的属性／物品操作。原版输入框全部选中／箭头表现和完整本地化排版仍未认证。上述修改已进入当前Windows Release客户端，背包拖放／装备及金币丢弃／存入取得有限运行和保存重入证据；全部大面板的原版对照边界与实际范围统一见[经典UI](../ui/CLASSIC_HUD.md#主要面板对照边界)。

## 操作

镶嵌材料换拿按本地1.13c D2Client RVA8C2B0及D2MOO ItemMode::D2GAME_ITEMSOCKET_PlaceItem核对：只有已鉴定、未损坏、有原socketed标记且确认还有空孔的目标才提交SocketItem；符文／宝石／珠宝遇到药水、卷轴、无孔或满孔装备走原交换／装备路径，不因材料类型单独抢占操作。背包与人物装备共用目标判定；移除自制Insert／Move／Merge／Equip等拖拽操作提示卡，保留原物品说明、占格颜色及真实操作错误。2026-10-19当前Windows Release包经真实UI输入确认符文↔药水、宝石↔卷轴、珠宝↔药水、符文↔无孔／满孔帽子，以及已装备两孔帽子依次填入两颗符文；原回包均Updated，正常退出后新进程恢复物品位置与两颗孔内符文。隔离角色、管理生成仅用于准备条件；未覆盖仓库／方块／交易、未鉴定／损坏目标、多人／原服及全部堆叠／装书组合，包身份及日志见[运行包记录](../../development/BUILD_AND_RUN.md#当前镶嵌材料换拿修正包)。

左键拿起、放置、交换、装备或投向地面；Shift转移到当前已确认箱子／方块，未打开容器时背包药水按原0x63自动入带。同类跨等级药水列与MPQ AutoBelt的预览规则见[腰带与消耗](BELT_AND_CONSUMABLES.md)。右键按物品提交使用／装备，卷轴／书鉴定进入目标流程；物品光标与装备／容器安置等待原回包。W或I／II标签提交武器切换，0x97确认后更新。

落格源码已按本地1.13c D2Client RVA945D0／94820修正：宽、高分别计算，偶数占格的一轴先加原DC6对应尺寸的整数四分之一，再除目标格距；随后减去占格数的整数一半，左／上边界钳至0，占满该轴的物品固定从0开始。预览与提交经SceneView::inventoryDropAt读取同一当前MPQ图像尺寸，不再使用与居中Cursor矛盾的旧抓取格；按住拖放、点击拿起、原回包Cursor共用算法，腰带一格物品仍使用原格命中。此修正已完成源码／调用链静态核对，并纳入当前Windows Release客户端包，身份见[运行包记录](../../development/BUILD_AND_RUN.md#当前客户端大面板修正包)；本轮未运行，旧包有限拖放证据不覆盖半格边界，等待用户实测。

包裹打开时地面拾取可请求Cursor；关闭时请求原服通常拾取／安置。自动装书、AutoStack、腰带补位及空位由连接的服务端决定，客户端不自行合并物品。已有手持物品不得用关闭面板清掉；ESC只关闭面板，不取消服务器Cursor。

拖放提交后，InventoryUi保留待处理来源GUID／原位置的可见性覆盖：从格子拿起的组合操作等待真实Cursor／安置回包期间，不重新绘制原格或中间Cursor图。若来源已是服务器确认的Cursor，提交放置／装备时保持手持图，直到原回包改变其位置；Pending仍禁止重复提交，不预画目标位置。背包、腰带、仓库／方块、装备／佣兵与交易格共用hidesItem；数量和占格只读原包投影。拒绝、超时、取消或切区按请求生命周期解除覆盖，实际Cursor由syncCursor恢复。旧master直接完成本地事务，不能照搬其立即清拖拽时序。旧覆盖路径随2026-10-10佣兵批取得代表证据；此次保留已确认Cursor的修改及原版对照边界见[EVIDENCE](EVIDENCE.md#职业装备与原版拒绝对照)，全部容器／交易及拒绝／切区组合未认证。

## 权威与限制

UI命中区域消费手势；关闭／视口变化后等待松键，不把同次按下转成世界攻击。世界／网络继续运行。占格预览、提示颜色及需求不是原服事务结果。

灌注／打孔／署名使用原38／58及既有Cursor移物序列；充能技能源选择和热键保留原GUID。任务资格与一次性奖励由服务端决定；Akara免费重置已按原服务与slot41接入，准确任务前置及缺口见[第一幕](../quests/ACT1.md)，不保留旧本地事务旁路。原服操作参数见[命令](../../development/DEBUG_PIPE.md#联网物品操作)，模型见[物品模型](MODEL.md)，加工边界见[QUEST_ITEMS](QUEST_ITEMS.md)。
