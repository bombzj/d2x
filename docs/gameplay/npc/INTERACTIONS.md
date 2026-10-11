# NPC交互与对白

NPC是所连接服务端的真实type1单位，位置／模式、对白、任务提示和服务资格均由该服务端提供。中立单位不使用敌对替身；组件／原图由公共ActorAnimation／Graphics解析。

SceneController统一悬停、高亮、姓名、点击／目标锁定和释放；RemoteControl按真实GUID靠近后0x13交谈，原0x27对白列表→0x29公共标志→带NPC身份的0x28交互确认→0x2F准备及公共对白UI继续消费。D2X先保存27待确认列表，收到匹配的28才打开会话并发送2F；2F确认已有权威会话，不重发27／28。一般私人任务刷新使用28/type6，不把玩家GUID编码成NPC身份。0x31确认实际原stringId，已确认话题不重复提交；0x30关闭，旅行0x38 action=0等原幕／位置结果。

当前TBL对白首行数字按a1npc SPEED元数据移除，通用字符串查询仍保留原文。hosting比对当前MPQ的NPC文本与TBL时同时处理UTF-8及原单字节省略号／弯引号，避免遗漏阿卡拉女巫介绍；对白编号仍由实际TBL匹配得到。原0x27消息的menu=0自动对白依次显示未确认集合；menu=2为可选任务评论，通过Talk打开，不在每次点击NPC时抢先弹出。任务原表中的menu=1经QUESTS_InitScrollTextChain转为回包menu=0，不能直接发送为原版的紧急交互菜单。普通闲谈由客户端读取当前MPQ，不占27的任务／介绍槽，也不发31任务确认；D2X通过Talk里的Gossip轮播。首次介绍和任务自动对白由服务端消息及PlrIntro旗标决定，不增加客户端“已问候”状态。NPC0x8A提示用原npcalert与MonStats2高度；它不是本地任务资格。原字幕时序、重播间隔及提示精确像素定位尚未完整认证。

原0x27有8个物理消息槽，但当前1.13c D2Common `TEXT_CreateTextListFromMessageList`要求count小于8；hosting拒绝超过7条的事实，D2X接收端也按0..7校验，避免掩盖原版断言。自研NPC可同时提供一个任务和未确认的首次介绍，分别接受31确认，不附加闲谈。依据为当前DLL的line274断言、本地D2MOO `D2Common/src/D2Text.cpp`及原版Host阿卡拉先64任务、后12介绍且均为menu0的录包；已有任务进度的角色也可只有12介绍。

NPC服务／Talk菜单与交易邀请共用[OriginalMenu](../../../src/presentation/graphics/original_menu.hpp)，读取当前MPQ boxpieces、font16与Sky PL2：金色标题、蓝色悬停、测宽／居中文字及行命中区域。样式依据用户提供的Warriv、Akara与等待交易原版截图；菜单锚点及全部状态未宣称逐像素认证，已有有限证据见[联网交付](../../modules/NETWORK.md#当前批交付)。

NPC靠近时，显示距离或尚未采样的离开方向可阻止过早交谈；必须有新的服务端位置且距离合格。显示坐标不能授权菜单或写副本。该修正已有Release构建证据，用户所述超时未运行复现，详见[联网源码修正](../../modules/NETWORK.md#本批源码修正)。

本地D2MOO PlrMsg::sub_6FC828D0、MonsterAI、SUnitNpc／PlrIntro与任务ActiveFilter核对原流程，Diablerie鼠标消费仅作表现参考。所有文本／图形／声音读当前MPQ；服务边界见[交易](TRADE.md)与[NPC模块](../../modules/NPC_QUEST.md)。
