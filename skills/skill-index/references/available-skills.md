# Available Skills

这个目录总结了当前仓库可见的两类 skills：

- system skills：预装在 Codex 环境中的通用能力
- repo-local skills：本仓库 `skills/` 目录下的项目定制能力

本目录用于导航，不替代各自的 `SKILL.md`。

进入复杂、多步、需要调试或可能跨轮继续的任务时，默认要记得维护仓库根目录 `log` 与 `debug`；这类场景通常应尽早启用 `external-memory-files`。

## System Skills

- `imagegen`
  - 作用：生成或编辑位图图片资产。
  - 适用：网站素材、产品图、插画、sprite、透明底图、基于参考图出变体。
  - 不适用：直接改 SVG、现有矢量体系、纯代码原生图形。

- `openai-docs`
  - 作用：基于 OpenAI 官方文档与 MCP 工具回答产品/API/模型问题。
  - 适用：最新模型选择、GPT-5.4 升级、官方接口用法、需要引用来源。
  - 特点：优先 MCP 文档源，必要时才退回官方站点搜索。

- `plugin-creator`
  - 作用：创建本地 plugin 脚手架与可选 marketplace 条目。
  - 适用：需要生成 `.codex-plugin/plugin.json`、`plugins/<name>/`、`.agents/plugins/marketplace.json`。
  - 特点：强调结构完整、占位符齐全、命名规范化。

- `skill-creator`
  - 作用：创建或更新 skill 本身。
  - 适用：定义 skill 的 `SKILL.md`、`agents/openai.yaml`、references/scripts/assets 结构。
  - 特点：强调简洁、渐进披露、不要把 skill 写成杂乱文档堆。

- `skill-installer`
  - 作用：列出或安装外部 skill。
  - 适用：从 curated 列表或 GitHub 路径安装 skill。
  - 特点：依赖网络；运行相关脚本时通常要申请提权。

## Repo-Local Skills

- `staged-engineering-governance`
  - 作用：给长周期、复杂仓库任务提供阶段治理、证据化验证、边界控制与人机协作约束。
  - 适用：阶段计划、多层验证、复杂调试、长期推进、repo 级规范收敛。
  - 特点：强调“本地通过不等于系统完成”、显式保留负面信号、遵守目录与所有权边界。

- `external-memory-files`
  - 作用：维护仓库根目录 `log`、`debug`、`break` 三份外部记忆文件。
  - 适用：多轮实现、长期调试、断点恢复、bug 生命周期跟踪。
  - 特点：区分时间线、缺陷记录与当前断点，不把三者混写。

- `readability-design-check`
  - 作用：在开发过程中定期做代码可读性、可维护性和设计一致性检查。
  - 适用：实现中途、重构后、跨文件修改、公共 API 变更、收尾前健康检查。
  - 特点：优先做小而快、可落地的结构优化，不把个人风格当成硬问题。

- `code-audit-loop`
  - 作用：协调独立审计 agent 进行只读审计，并在报告返回后继续完成至少一轮修复或补测。
  - 适用：高风险变更、状态机、协议栈、并发、资源生命周期、安全边界、用户明确要求 review/audit。
  - 特点：先本地实现，再受限交接审计；报告回来后不能只转述，必须闭环。

## 快速选择

- 想知道“OpenAI 最新推荐模型是什么”：`openai-docs`
- 想创建一个新的 skill：`skill-creator`
- 想安装别人仓库里的 skill：`skill-installer`
- 想做 plugin 脚手架：`plugin-creator`
- 想生成一张项目图片：`imagegen`
- 想给复杂任务加阶段治理：`staged-engineering-governance`
- 想维护 `log/debug/break`：`external-memory-files`
- 想做代码健康检查：`readability-design-check`
- 想追加独立审计闭环：`code-audit-loop`

## 推荐组合

- 复杂实现主线：`staged-engineering-governance` + `external-memory-files`
- 收尾前健康检查：`readability-design-check`
- 高风险改动复核：`code-audit-loop`
- 新建 skill 工作流：`skill-creator`
- 图像资产工作流：`imagegen`

如果一个请求已经明显匹配某个具体 skill，就直接使用那个 skill；只有在需要导航、比较或组合时，才先使用 `skill-index`。
