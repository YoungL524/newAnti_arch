# PostgreSQL 持久化

项目运行时数据库已切换为 PostgreSQL（Qt 驱动 `QPSQL`）。默认连接：

- 主机 `127.0.0.1`
- 端口 `5432`
- 数据库 `postgres`
- 用户 `postgres`
- 密码从环境变量 `ANTIDRONE_PG_PASSWORD` 读取

启动程序前在 PowerShell 7 中设置密码：

```powershell
$env:ANTIDRONE_PG_PASSWORD = "你的 postgres 密码"
```

程序启动时会自动创建/升级 `pipeline_metrics`、`system_events`、`tracks`、`devices`、`tasks` 等表。Qt 部署目录必须包含 `qsqlpsql.dll`（构建流程已自动部署）。

## 旧 SQLite 数据

当前切换只改变新写入目标，不会自动搬迁 `data/antidrone.db`。如需保留历史数据，应先导出 SQLite，再按目标表导入 PostgreSQL，并在停机窗口执行校验：行数、时间范围、`task_id/device_id` 关联和索引。

## 配置覆盖

可直接修改 `config/database.json` 的 `host`、`port`、`database`、`user`；密码建议继续使用 `passwordEnv`，避免将凭据提交到仓库。
