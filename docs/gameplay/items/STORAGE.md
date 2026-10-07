# 私人储物箱

私人箱是原服容器，与[方块](CUBE_AND_GOLD.md)及[野外宝箱](../world/OBJECTS.md)不同。公共面板读取当前MPQ tradestash.dc6与Inventory的Big Bank Page 1；当前资料片布局6×8，不提供试玩／经典回退。

真实Objects.OperateFn=32单位先按GUID靠近／交互，只有原0x77确认才开放箱内格子。点击及Shift转移都通过SceneController／RemoteUiClients；范围依原SizeX／SizeY与共同交互几何，不能把OperateRange当中心圆半径。

物品逐步等待Cursor／位置回包；金币存取提交原0x4F，金额、上限及余额由原服决定。关闭可取消未发组合及待确认UI，不撤销已发服务器操作；死亡／换幕／换交互失效旧上下文。

客户端不把本地容器提交到原服，不写D2S或箱内存档。位置／page语义见[模型](MODEL.md)，完整参数见[命令](../../development/DEBUG_PIPE.md#联网物品操作)。全部物品组合、异常与多人访问尚未认证。
