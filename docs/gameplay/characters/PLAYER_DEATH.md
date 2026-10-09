# 玩家死亡与尸体

死亡、损失、宠物处理、装备转移、回城、尸体归属及保存由原服执行。客户端显示原动作／模式并提交请求；具体协议及确认条件只维护在[联网模块](../../modules/NETWORK.md#联机死亡与尸体)。

## 当前行为

原0x0D动作8／9、0x0E死亡／DEAD模式驱动Dying／Dead；生命整数0不能单独覆盖原服存活动作。真正死亡关闭玩法面板、终止未发送手势／队列，但网络与原服继续运行。共用原字体、职业DD，缺DD时停DT末帧。

死亡后ESC等待原服DEAD，再单次发0x41；收到原服存活位置／资源确认后回城，不在客户端补满资源。用户原版实测：ESC回城满血；死亡直接退出，或离开后服务端死亡，再次进入显示1血满蓝。当前HUD在原服存活动作及初始零整数生命采样时显示1血，副本仍保留原始0；蓝量直接读原服，客户端不补满。

本人可见尸体以真实type0 GUID靠近并0x13取回。取回数量、库存、经验和0x8E归属解除由原服回包确认；`accepted`不是回收完成。不可用旧本地装备规则替代部分回收。

## 编码与原版依据

自研宿主的伙伴联动以death::Transition.ready为同一权威DT结束点：调用companions::ownerDied后，罗格写死亡位并开始自身DT，召唤物进入死亡／移除。复活与保存须等待companionsSettled，背压不提前清理或保存存活佣兵；怪物本身受伤或寿命到期仍可独立死亡。依据PlrModes::PLRMODE_StartXY_Dead → PlayerPets::D2GAME_KillPlayerPets／sub_6FC7CDC0，不能仅在玩家hp归零时立即杀伙伴。当前源码未做本批运行测试，详见[佣兵](HIRELINGS.md#主人死亡时序)。

独立D2S工具保留原JM尸体段，编码支持最早非空尸体及物品位置；不提供产品恢复入口，详见[存档](../../modules/SAVES.md)。D2S没有客户端死亡动画、未完成施法或短时状态字段。

本地D2MOO PlrModes尸体创建／回收与DT→DEAD、PlrMsg::sub_6FC828D0／Rcv0x41、PlayerPets死亡处理、ItemMode装备回收、Player::PLAYER_ApplyDeathPenalty与PlrSave2尸体段仅作原服规则／格式证据。客户端不移植损失结算。专家、多尸体、满背包部分回收和离开后死亡精确时序未完整认证；有限单人观察见[联网记录](../../modules/NETWORK.md#既有有限证据)。
