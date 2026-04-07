# Audit Agent Contract

在把任务交给独立审计 agent 时，优先读取本文件。

## 交付给审计 agent 的最小上下文

- 用户目标：本次修改要解决什么问题。
- 改动摘要：已经做了哪些实现。
- 触及文件：只列相关文件。
- 相关 diff 或关键代码片段：只发必要范围。
- 已执行验证：哪些测试、命令、人工检查已经完成。
- 重点关注：协议、并发、状态机、回归、安全边界等。

不要额外提供你怀疑的 bug 结论或预设修复方案，除非用户明确要求定向核查。

## 审计提示词模板

```markdown
你是独立代码审计 agent。只做审计，不做代码修改。

任务：
- 审计下面这组改动，重点寻找 bug、行为回归、边界条件遗漏、协议或状态机错误、并发或资源生命周期问题，以及缺失测试。
- 只根据提供的材料下结论；证据不足时明确写“需补充上下文”。
- 不要输出大段改写代码；给出最小可执行修改方向即可。

输入：
- 用户目标：<...>
- 改动摘要：<...>
- 触及文件：<...>
- 相关 diff/代码：<...>
- 已执行验证：<...>
- 重点关注：<...>

输出必须严格遵循下列 Markdown 模板。
```

## 审计报告模板

无发现时，在 `Findings` 段写 `- none`。有发现时，按 `F1`、`F2` 递增编号。

```markdown
# Audit Report

## Summary
- overall_risk: <low|medium|high>
- verdict: <pass|needs-fix|blocked>
- top_issue: <一句话摘要；无则写 none>

## Findings
### F1
- severity: <blocker|high|medium|low>
- confidence: <high|medium|low>
- file: <path:line 或 none>
- title: <简短问题标题>
- impact: <会造成什么错误、回归或风险>
- evidence: <基于代码的事实、触发条件、复现线索>
- requested_change: <建议的最小修改方向>
- test_gap: <建议增加或更新的测试；无则写 none>

### F2
- severity: <blocker|high|medium|low>
- confidence: <high|medium|low>
- file: <path:line 或 none>
- title: <简短问题标题>
- impact: <会造成什么错误、回归或风险>
- evidence: <基于代码的事实、触发条件、复现线索>
- requested_change: <建议的最小修改方向>
- test_gap: <建议增加或更新的测试；无则写 none>

## Context Gaps
- <缺失上下文；无则写 none>

## Recommended Next Fix
- target: <优先修改的文件、函数或模块>
- reason: <为什么优先处理这里>
- minimum_change: <最小可执行改动>
```

## 报告验收标准

只有满足以下条件时，才把报告作为后续修改输入：

- 至少一个 finding 具备明确证据和定位信息，或者报告明确说明 `- none` 且解释充分。
- `requested_change` 可以落地成具体代码或测试变更。
- `Recommended Next Fix` 与前面的 findings 一致。
- 结论聚焦缺陷与风险，不把风格偏好伪装成缺陷。
