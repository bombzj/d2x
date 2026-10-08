# NPC交互与对白

NPC是原服真实type1单位，位置／模式、对白、任务提示和服务资格都来自原服。中立单位不使用敌对替身；组件／原图由公共ActorAnimation／Graphics解析。

SceneController统一悬停、高亮、姓名、点击／目标锁定和释放；RemoteControl按真实GUID靠近后0x13交谈，原0x27→0x2F准备及公共对白UI继续消费。0x31确认实际原stringId，已确认话题不重复提交；0x30关闭，旅行0x38 action=0等原幕／位置结果。

当前TBL对白首行数字按a1npc SPEED元数据移除。原0x27消息的menu=0自动对白依次显示未确认集合；menu=2为可选任务评论，通过Talk打开，不在每次点击NPC时抢先弹出。原服仍决定首次介绍及任务自动对白。NPC0x8A提示用原npcalert与MonStats2高度；它不是本地任务资格。原字幕时序、重播间隔及提示精确像素定位尚未完整认证。

最新NPC陈旧位置修正：显示距离或尚未采样的离开方向可阻止过早交谈，靠近必须有新原服位置且距离合格；显示坐标不能授权菜单或写副本。该修正已随当前包构建，具体用户超时未运行复现，详见[联网源码修正](../../modules/NETWORK.md#本批源码修正)。

本地D2MOO PlrMsg::sub_6FC828D0、MonsterAI、SUnitNpc／PlrIntro与任务ActiveFilter核对原流程，Diablerie鼠标消费仅作表现参考。所有文本／图形／声音读当前MPQ；服务边界见[交易](TRADE.md)与[NPC模块](../../modules/NPC_QUEST.md)。
