# 原版消息对比缺陷

当前未关闭条目见下表。X3R已由固定帧冻结的物件事实修复，完成多帧合并、双客户端、迟到视野及单机单步／保存恢复复验；Q1／Q4／Q5／T1、X2及X4的既有结果保留。结果、准备条件及未认证边界见[MESSAGE_COMPARISON_PLAN.md](MESSAGE_COMPARISON_PLAN.md#x3r-多帧合并修复复验)。本轮改动未提交或推送Git。

| 编号 | 差异与证据 | 当前状态／关闭条件 |
| --- | --- | --- |
| J1 | 原版新建且未首次入局的角色加入D2X Host收到Bad file；2026-10-26原包连接3／5／7均返回`B4 0E 00 00 00`，连接7上传总长335；同名原档abab为v96、335字节、flags=0x21。旧角色能加入。`decodeSave`在处理INIT位前读取完整固定段而拒绝合法初始头。D2MOO `PlrSave2.cpp::PLRSAVE2_ReadSaveHeader`及`PLRSAVE2ERROR_NEWBIE_SAVE`分支允许仅头部并按CharStats初始化 | 已修并完成Release构建：MPQ初始人物构造下移至persistence，与本地建角共用；所有decodeSave消费者统一处理INIT，不在TCP握手特判名字／职业。保留头／校验和／模式验证，拒绝截断完整档及INIT带额外段；上传失败诊断保留具体解码原因。共用创角入口已验证女巫右手StartSkill，生成完整档在D2X单机及原版Host重入成功；这些检查不代替335字节INIT实包复验，原版客户端首次加入及后续重入通过前保留本项 |
