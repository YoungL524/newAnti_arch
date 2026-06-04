# Review Rules

当用户输入 `pull&review` 时：

1. 自动执行 `git pull` 拉取 GitHub 最新代码。
2. 只做代码 review，不主动实现修改。
3. 重点检查代码细节问题、构建/测试风险、以及是否符合现有架构设计。
4. 必要时给出优化建议，并明确写给 Claude Code 的修改方向：哪里有问题、为什么、怎么改。
5. 所有 review 修改建议必须写入当前目录下的 Markdown 文档。
6. Review 文档需要带上对应 GitHub 版本号，也就是当前 `HEAD` commit hash。
7. 最终实现交给 Claude Code，Codex 负责把关和指出问题。

建议 review 文档命名格式：

```text
review-<commit-short-hash>.md
```

如果同一个 GitHub 版本号已经存在 review 文档，则复用并更新该文档。
