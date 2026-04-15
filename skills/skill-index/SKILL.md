---
name: skill-index
description: 作为当前仓库与当前会话可用 skills 的总入口与目录。用于用户想查看有哪些 skills、为当前任务选择最合适的 skill、决定多个 skills 的组合顺序、或希望先通过一个导航型 skill 再进入具体 skill 的场景。不要在任务已明显对应某个单一 skill 时使用它。
---

# Skill Index

这个 skill 是导航器，不是替代品。它的职责是为当前请求选出最合适的一个或几个下游 skill，并给出最小组合顺序。

## 入口提醒

- 只要任务会持续多步推进、涉及调试、验证、修复或中断恢复，就要尽早维护仓库根目录 `log` 与 `debug`。
- 需要维护 `log`、`debug`、`break` 时，优先尽快启用 `external-memory-files`，不要等到任务后半段再补记。

## 何时使用

- 用户想查看当前有哪些 skills。
- 用户询问“这个任务该用哪个 skill”。
- 用户希望有一个目录型、提纲型、总入口型 skill。
- 一个任务可能需要多个 skills 配合，但还没确定先后顺序。
- 你不确定该选 system skill 还是 repo-local skill。

## 何时不要使用

- 当前任务已经明确命中某一个具体 skill，此时直接使用该 skill。
- 用户已经明确点名要使用哪个 skill 或哪几个 skill。

## 工作流

1. 先把请求归到一个主需求：
   - 图像生成或编辑
   - OpenAI 文档与模型选型
   - 安装 skill
   - 创建或更新 skill
   - 创建 plugin
   - 长周期工程治理
   - 外部记忆文件维护
   - 可读性与设计检查
   - 审计闭环或第二意见
2. 优先只选一个主 skill。
3. 只有在确实能提升结果时，再补充 1 到 2 个辅助 skill。
4. 如果任务是多步骤、长周期或包含调试闭环，先确认是否需要立即开始写 `log` 与 `debug`。
5. 用一句短说明告诉用户你将按什么顺序使用哪些 skill。
6. 接着只打开被选中的下游 skill，并按其说明执行。
7. 不要在本 skill 里重复下游 skill 的完整细节。

## 快速路由

- 位图图片生成、编辑、变体产出：`imagegen`
- OpenAI 官方文档、最新模型、GPT-5.4 升级：`openai-docs`
- 列出或安装 skill：`skill-installer`
- 创建或更新 skill：`skill-creator`
- 脚手架化创建 plugin：`plugin-creator`
- 长任务治理、阶段推进、证据化汇报、边界控制：`staged-engineering-governance`
- 维护仓库根目录 `log`、`debug`、`break`：`external-memory-files`
- 代码可读性、可维护性、设计一致性检查：`readability-design-check`
- 独立审计 agent 闭环与报告后续修复：`code-audit-loop`

## 常见组合

- 新功能开发且验证链较长：
  - `staged-engineering-governance` -> `external-memory-files`
  - 开始后尽早写 `log` 与 `debug`
  - 收尾前可补 `readability-design-check`
- 高风险协议、状态机、并发或生命周期修改：
  - `staged-engineering-governance` -> `code-audit-loop`
  - 如需长期追踪，再补 `external-memory-files`
- 新建 skill：
  - `skill-creator`
  - 若该 skill 同时涉及仓库级流程治理，再补 `staged-engineering-governance`
- 新建 plugin：
  - `plugin-creator`
  - 若 plugin 是更大工程任务的一部分，再补 `staged-engineering-governance`
- 产出项目用图片资产：
  - `imagegen`
  - 若它只是更大实现任务的一部分，再视情况补 repo-local skills

## 目录入口

当前完整技能目录见 [references/available-skills.md](references/available-skills.md)。

读取目录后，优先做的是“选择最小可用 skill 集合”，而不是把所有 skill 一次性都加载进来。

## 硬约束

- 优先少而精，不要为同一任务堆过多 skill。
- 优先选择一个主 skill，再决定是否补辅助 skill。
- 这个 skill 负责导航，不负责替代下游 skill。
- 若用户明确要求“阅读所有 skills”或“比较所有 skills”，再读取完整目录。
- 若环境中新增、删除或重命名 skill，需要同步更新 `references/available-skills.md`。
