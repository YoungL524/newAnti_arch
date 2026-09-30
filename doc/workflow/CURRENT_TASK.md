# 当前任务：归档停机排空与架构复核

## 状态

代码已修改，待用户编译和运行验证；代理未编译、未运行测试程序。

## 本轮要求与实施

1. 全项目架构静态复核确认 Producer、Topic、Consumer、Lane 与唯一控制 owner 主链未见系统性偏离；发现正常停机时存储运行时直接销毁 ArchiveReader，可能遗漏最后的固定批次。
2. `StorageRuntime::shutdown()` 在 UDP 与 HTTP 入口关闭后冻结 Track/Event 当前水位，复用同一个 ArchiveReader 逐批提交，两个 committed cursor 都追平后才允许正常关闭 run。
3. 任一批次无进展或归档已发现 retention gap 时，释放数据库连接但保留 run 为未正常退出，向组合根返回失败诊断；不新增持久队列、重试线程或第二个数据库写入者。

## 改动范围

- 改动限于 `core/app/StorageRuntime.{h,cpp}`、本任务文档与归档合同；前次 DP 心跳位姿改动继续等待用户联试验收。

## 静态验收

- 静态差异与注释审计；停机后数据库 run 标记、航迹/事件归档结果由用户编译运行验证。
