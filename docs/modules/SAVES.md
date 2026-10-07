# 独立D2S工具与客户端持久化

当前产品的角色保存由D2GS／D2DBS负责；客户端不读取本地D2S恢复游戏，不调用旧characterSave／restore，也没有F11、--load或--save入口。产品不链接d2x_persistence。保留的D2S编码仅供独立资源／存档工具使用，原用户文件不迁移或删除。

## 文件与值边界

| 入口 | 职责 |
| --- | --- |
| [character_save.hpp](../../src/persistence/character_save.hpp) | CharacterSaveData及尸体／容器／物品快照；字段搬迁后未改变 |
| [record.hpp](../../src/gameplay/character/record.hpp) | CharacterRecord／HirelingRecord身份、成长、选择／绑定、任务和原生未知段 |
| [d2s_codec.cpp](../../src/persistence/d2s_codec.cpp) | 原D2S v96编解码编排与内容校验 |
| [d2s_header.cpp](../../src/persistence/d2s_header.cpp)、[d2s_fixed_sections.cpp](../../src/persistence/d2s_fixed_sections.cpp) | 原头、长度／校验和、任务／传送点／初见等固定段 |
| [d2s_stats.cpp](../../src/persistence/d2s_stats.cpp)、[d2s_skills.cpp](../../src/persistence/d2s_skills.cpp)、[d2s_items.cpp](../../src/persistence/d2s_items.cpp)、[d2s_inventory.cpp](../../src/persistence/d2s_inventory.cpp)、[d2s_quests.cpp](../../src/persistence/d2s_quests.cpp) | 原属性、技能、JM位流、容器与任务槽 |
| [save_file.cpp](../../src/persistence/save_file.cpp) | 编码／解码验证、文件锁、备份及原子替换；不是联机保存入口 |
| [asset_tool.cpp](../../src/asset_tool.cpp) | save-info只读摘要；其他工具能力以usage为准 |

## 原格式与保存内容

原AA55AA55、版本96、小端低位优先位流、长度及循环移位累加校验和；文件上限16MiB。属性宽度、参数、Save Add等从当前ItemStatCost读取；磁盘不写本项目规则指纹、私有尾部或运行时状态。

保留角色身份、基础属性／经验／等级／余点、技能等级／选择／快捷绑定、资源与金币；Woo!任务、WS传送点、0x7701初见；装备／两组手部、背包、腰带、私人箱、方块、Cursor、佣兵装备和原JM尸体及kf铁魔制造物品。品质、词缀、耐久、数量、孔内顺序、符文之语独立列表、无形／署名与套装附加属性保存原值，不重掷。

Cursor使用原mode4；孔内子项mode6紧随宿主，不重复根记录。支持最早非空尸体的12装备位及一个死亡鼠标物品位置；其他布局明确拒绝。佣兵保留类型、名字偏移、seed、经验和死亡位；原档无当前生命字段，不补写。地图seed／难度／幕是原字段，动画、预测、弹体、怪物AI、商店、地面掉落与短时状态不是。

## 任务槽与奖励语义

日志顺序、原任务号、图像槽及磁盘槽分别声明，见[任务身份](../gameplay/quests/SYSTEM.md)。旅行原槽7／15／23／28与首领死亡分开；A2Q0欢迎为槽8位0，A3Q0为槽16位0，不混普通初见。

黄金鸟领药／饮用分开，CUSTOM1保留未用；古书属性点与衣卒尔技能点计入原成长校验。安亚卷轴已发／已用和职业奖励位分别保留，不由完成位推断抗性奖励。打孔／署名、俘虏奖励档位、凯恩代救／牛王CUSTOM6及重置原位保持独立；未知任务位保留，不自动语义转换。巴尔原头部0x25难度／五幕进度保留已有较高值。

这些是独立编码的映射与校验，产品任务状态来自原服，不从解码值重建本地剧情／NPC／世界。格式或语义后续变化须同步本页、相关指纹与拒绝边界，旧档不静默迁移。

## 支持与拒绝边界

- 仅原v96资料片普通非Ladder角色；普通版、D2R、Hardcore／永久死亡和Ladder头位拒绝。网络角色选择资格是另一条原服流程。
- 耳朵、自动词缀、Realm数据及未支持布局／属性、损坏长度／校验／成长不匹配明确拒绝；物品网络位流能解码某字段不表示D2S工具支持。
- 当前保存值校验carry1、任务物品互斥、需求／容器和尸体原件冲突；不静默删物品或修正非法组合。TXT前后缀编号一基、特殊品质身份零基，不迁移旧错误编号。
- 缺未适配Properties／真实掷值时不能少写属性后保存；原版菜单外观及全部容器组合未完整零售客户端认证。

save-info读取不修改输入。最新包曾读取原服正常保存的v96并核对读前后哈希不变；首次保存前无效原服建角文件被拒绝，见[交付证据](NETWORK.md#最终依赖清理)。旧离线保存重载不认证产品恢复能力或全部原版兼容。

## 客户端偏好、凭据与探索

client-settings.json只保存客户端走跑、底栏与地图选项，原子替换由resources/atomic_file承担；不引入D2S编码。写失败保留待保存状态／旧文件并限频重试。

OnlineLoginMemory按配置路径隔离：Windows当前用户凭据管理器；Linux配置旁权限0600的.credentials.local，Linux实际运行未认证。密码不写连接JSON、只读会话、日志或角色D2S。

产品探索仅为本局AutomapExploration，同局返回保留、新局清空；旧app/automap_save及.d2xmap读写入口已删除。旧侧文件和用户存档保留，但不能继续照旧说明备份／读取以期待当前产品恢复。原.map／.ma*互通尚未核实，详见[自动地图](../gameplay/world/AUTOMAP.md)。

原位证据：本地D2MOO PlrSave2、Items／ItemMods、PlrIntro、D2QuestRecord；D2CE ActsInfo只交叉核对重置位；独立@dschu012/d2s只用于既有格式对照，不是运行依赖。固定版本及许可见[资料来源](../resources/THIRD_PARTY.md)。
