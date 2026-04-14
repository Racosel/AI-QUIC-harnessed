---
name: staged-engineering-governance
description: Use when working in a long-running or complex repository that needs staged delivery, layered validation, persistent project memory, evidence-based reporting, and clear human-agent boundaries. Helps Codex keep negative findings visible, respect ownership boundaries, avoid overstating completion, and improve existing project skills without turning them into bloated policy dumps.
---

# Staged Engineering Governance

## When To Use

Use this skill when any of the following are true:

- The task spans multiple rounds, stages, or subsystems.
- The repository has recurring bugs, regressions, or hard-to-reproduce failures.
- Local tests are not enough to prove end-to-end correctness.
- The project already has, or would benefit from, persistent project memory such as status docs, bug logs, or handoff notes.
- The user wants the agent to help maintain engineering discipline, not just produce code quickly.
- You are updating an existing repo-local skill and need to strengthen its workflow, validation, or collaboration rules.

## Core Contract

1. Do not confuse local success with system success.
2. Do not hide, soften, or narratively smooth failures.
3. Do not cross ownership boundaries without explicit approval.
4. Keep project state externalized so work survives context loss.
5. Update tests and docs as part of implementation, not as an afterthought.
6. Treat the human as the final authority on architecture, completion, and acceptance.

## Hard Rules

### 1. Preserve Evidence

- Every important claim about correctness, completion, regression status, or readiness must be backed by repo evidence: code paths, tests, logs, traces, docs, or reproduced behavior.
- If something is only locally verified, say so explicitly.
- If integration, performance, interoperability, or end-to-end validation is missing, say so explicitly.
- Never rewrite a failure as a neutral or positive result.
- When summarizing, distinguish facts from inferences.

### 2. Respect Ownership Boundaries

- Do not modify external dependencies, vendored code, generated artifacts, third-party fixtures, external test harnesses, or user-owned archives unless the user explicitly asks for it.
- When blocked, diagnose the local implementation first.
- If the most convenient fix is outside the owned code boundary, surface that boundary instead of silently crossing it.
- If the repository already defines protected directories or files, follow that policy exactly.

#### AI-QUIC 代码目录边界：`ai-quic/`

在 AI-QUIC 仓库中，所有新的开发代码和测试代码都应放在仓库根目录下的 `ai-quic/` 中，不允许直接放在仓库根目录。

- 新增源码、测试、测试夹具、辅助脚本、构建相关代码目录时，默认都放在 `ai-quic/` 下组织，不要在仓库根目录直接新建同类目录或文件。
- 除非用户明确要求，不能把新的开发代码或测试代码直接放到仓库根目录。
- `docs/`、`skills/`、`log`、`debug`、`break`、仓库根目录 `Makefile` 这类文档、治理或仓库级入口文件不属于此限制，可以继续放在现有位置。
- 如果仓库中已经存在历史代码目录、第三方目录或外部项目目录，不要因为这条规则自动搬迁它们；只有在用户明确要求迁移时才调整。
- 当任务需要新增 repo-owned 的实现或测试时，先检查 `ai-quic/` 是否存在；若不存在，应优先在该目录下创建，再继续后续实现。
- 如果某项改动同时涉及 `ai-quic/` 与仓库根目录的新代码落点，默认视为违反目录边界，先收敛到 `ai-quic/` 方案，再继续实施。

#### AI-QUIC 仓库级操作入口：根目录 `Makefile`

为了简化部分重复操作，可以在仓库根目录建立并维护一个 `Makefile`，作为常用命令入口。

- 根目录 `Makefile` 属于仓库级工作流入口，不视为把新的开发代码直接放到仓库根目录。
- 仅把稳定、可复用、值得降低操作成本的命令收敛进 `Makefile`；一次性的临时调试命令不要长期固化。
- 当常用操作、默认参数或底层脚本入口发生变化时，同步更新 `Makefile`，避免它和真实流程脱节。
- 若目标依赖高权限、外部环境或用户手工步骤，应在目标名或注释中明确说明，不要伪装成完全自动的一键完成。

#### AI-QUIC 新设计执行职责

在 AI-QUIC 仓库中，agent 的默认职责边界如下：

- 先以 `docs/` 为约束来源开展设计与实现；开始编码前，优先读取相关的 `docs/plans/`、`docs/quic-interop-runner/`、`docs/ietf/notes/`。
- `xquic/` 是参考实现与结构对照物，用于借鉴模块划分、接口组织、最小运行路径与观测点；除非用户明确要求，不把新的设计直接落到 `xquic/`。
- 所有 repo-owned 的新设计、实现、测试与 interop 包装，都应放在 `ai-quic/` 下完成；若 `ai-quic/` 尚不存在，先创建最小骨架，再继续实现。
- 当 `docs/` 与 `xquic/` 的现有实现不一致时，以 `docs/` 和用户最新要求为准；若 `docs/` 缺口过大，应先记录缺口并同步规划文档，再继续设计。

### 3. Separate Validation Levels

Treat validation as a ladder, not a binary:

1. Narrow unit or focused tests
2. Local smoke or loopback validation
3. High-fidelity integration, end-to-end, benchmark, or external-environment validation

Rules:

- Passing level 1 does not imply level 2 or 3.
- Passing level 2 does not imply level 3.
- Status updates and final summaries must state which level was actually reached.
- Do not declare a feature complete if the target validation level has not been reached.

### 4. Preserve Negative Signals

- Keep failures, caveats, and unresolved risks visible in summaries, reviews, and status docs.
- Do not optimize for a smoother narrative at the cost of accuracy.
- If a test is weak, toy-like, low-scale, or unrepresentative, say that directly.
- If a passing result depends on narrow fixtures, say that directly.

### 5. Keep Skill Scope Clean

When updating this or related skills:

- Put repo-invariant workflow in the main skill.
- Put repo-specific commands, paths, and examples in references.
- Distinguish hard rules from heuristics.
- Do not add one-off user preferences unless they recur and materially improve future work.
- Do not let a skill become a dumping ground for every past incident.

## Default Workflow

### 1. Establish Boundary First

Before editing:

- Identify the requested outcome.
- Identify the owned code boundary.
- Identify what must not be broken.
- Identify what level of validation would actually count as success.
- Identify which existing docs or repo files already act as the source of truth.

### 2. Externalize Project Memory

Prefer reusing existing project artifacts for persistent memory. If the repo already has equivalents, use them. Typical roles are:

- Status doc: current scope, stage, completion state
- Execution log: timestamped steps taken
- Bug log: issue, impact, root cause, fix, validation
- Handoff note: current checkpoint and next actions

If the repo does not use these patterns, do not create noise by inventing all of them automatically. Add only what is justified by task length, failure complexity, and the user's workflow.

#### AI-QUIC 状态文档：`docs/plans/process.md`

在 AI-QUIC 仓库中，如果任务需要持续维护“阶段规划、完成状态、完成判定”，`docs/plans/process.md` 是默认的状态文档。更新时遵循以下规则：

- 先读取 `docs/plans/process.md`，再与 `docs/plans/plan-quic.md`、`docs/plans/plan-general.md`、当前代码和当前验证证据进行对照，避免只按记忆改状态。
- 沿用仓库现有的阶段命名、依赖顺序和验收表述；除非用户明确要求，不要另起一套并行计划体系。
- 当阶段或条目出现以下任一变化时更新：新建、拆分、开始、阻塞、范围调整、完成局部实现、完成本地验证、完成高保真验证、确认完成。
- 状态不要只写“完成/未完成”；优先使用更精确的状态，例如 `未开始`、`进行中`、`阻塞`、`已本地验证`、`已高保真验证`、`已完成`，或文件中已经采用的等价中文表述。
- 每个条目至少写清：当前状态、完成判定、已具备的证据、仍未验证或仍受阻的部分。
- 代码提交或局部通过测试，不等于阶段完成；如果目标验证层级尚未达到，就写出当前最强但仍真实的状态，不得提前标记“已完成”。
- 如果只完成了阶段中的一部分，就更新对应子项或备注，不要把整个阶段一起标成完成。
- 如果 `docs/plans/process.md` 还是空白文件，先按现有阶段规划初始化，再逐步补充状态；不要从零发明与仓库现有规划脱节的新大纲。

#### AI-QUIC Step 计划文件模板：默认沿用 `step01`

在 AI-QUIC 仓库中，如果需要为新的 `stepXX` 建立 `docs/plans/` 下的步骤计划文件，默认沿用 `step01` 已经形成的双文件模板，尽量保持格式、职责与命名风格一致。

- 参考对象默认是：
  - `docs/plans/step01-handshake-doc-index.md`
  - `docs/plans/step01-handshake-logic-design.md`
- 每个新的 step 默认只新建两类文件：
  - 一个 `*-doc-index.md`，负责记录该 step 的文档入口、相关文件索引、阅读顺序与引用关系。
  - 一个 `*-logic-design.md`，负责记录该 step 的目标、范围、约束、切片、验收口径、验证要求与实现逻辑设计。
- 新 step 的章节组织、标题粒度、表达风格应尽量模仿 step01，而不是为每个 step 重新发明一套文档结构。
- 除非用户明确要求，不要再为同一个 step 额外新建其他设计类文件，例如额外的架构设计、接口设计、时序设计、测试设计、补充说明、实现草案等平级文档。
- 如果某些内容确实需要补充，优先写回该 step 的 `*-logic-design.md`，或补到现有仓库级文档中；不要默认再拆出新的 step 级设计文件。
- 如果历史上已经存在不符合此模板的 step 文档，默认先保持兼容，不因为这条规则自动重构旧文件；只有在用户明确要求收敛时才整理。

### 3. Plan By Stage Or Capability

For large tasks:

- Break work into stages, milestones, or capability slices.
- Define what done means for each slice.
- Keep implementation aligned to those boundaries.
- Do not let the agent freely expand scope because adjacent work seems convenient.
- When the repo already has staged planning, update it instead of creating a competing plan format.

### 4. Implement Incrementally

- Prefer the smallest correct patch that closes one clear loop.
- After each significant step, sync nearby tests and docs.
- If a fix requires extra observability, add high-signal debug output near the failure path.
- Prefer bounded, purposeful traces over generic log spam.
- Remove or gate temporary debug output once it has served its purpose, unless it clearly improves long-term operability.

### 5. Validate With Increasing Fidelity

- Run the narrowest relevant check first.
- Then run the most representative local validation.
- Then run the highest-fidelity validation available within permissions and environment constraints.
- If the highest-fidelity step must be run by the user, prepare the command, explain what evidence to return, and continue analysis after they do.

## Debugging Guidance

When diagnosing recurring or cross-layer failures:

- Assume that some failures only appear under higher-fidelity conditions.
- Watch for false confidence caused by small fixtures, toy payloads, or agent-generated tests that mirror the current implementation too closely.
- If current output is too weak to diagnose the bug, temporarily improve observability.
- Prefer debug output that reveals:
  - current state or phase
  - pending work or blocked reason
  - path, routing, or environment choice
  - ownership of data or control
  - key flags or invariants
  - timeout, retry, retransmission, or pacing context
- After the issue is understood, decide whether to keep, gate, or remove the extra output.

## Review Guidance

When doing review or self-review:

- Prioritize correctness, regression risk, boundary violations, weak validation, and missing evidence.
- Call out large files or mixed-responsibility modules as architectural risks, not just style issues.
- If tests exist but do not reflect real workload, say so.
- If example or demo code is carrying production-like logic, note the boundary erosion.
- If a skill or workflow file is growing too large, say so and propose what should move into references.

## Human-Agent Collaboration Contract

The human owns:

- architecture boundaries
- completion criteria
- acceptance decisions
- tradeoff decisions
- permission to cross ownership domains

The agent owns:

- high-intensity execution
- repo exploration
- incremental implementation
- evidence gathering
- document and test synchronization
- debug trace refinement

The agent must not assume ownership of:

- declaring the system complete
- redefining the acceptance bar
- modifying out-of-scope components for convenience
- weakening negative findings to make progress appear smoother

#### AI-QUIC 决策升级规则（强制）

在 AI-QUIC 仓库中，遇到以下任一情况时，必须交由用户判断，并等待用户回答后再执行：

- 出现任何关键问题或阻塞，且存在多条可行处理路径。
- 对需求、边界、验收标准、风险归因存在不确定性。
- 需要做较大的设计决策，例如模块边界调整、接口语义变化、目录结构重排、验证口径切换。
- 需要在多种实现方案之间取舍，且取舍会明显影响后续实现成本、兼容性或可维护性。

执行要求：

- 先明确说明不确定点、可选方案、主要影响与推荐选项。
- 在用户给出明确回答前，不继续执行该决策相关实现。
- 用户答复后，按用户选择继续推进，并在状态文档或日志中记录决策结论。

## Reporting Rules

In status updates and final summaries:

- State what changed.
- State what was actually validated.
- State what remains unvalidated.
- State the main remaining risks.
- Use precise language such as:
  - unit-tested
  - locally verified
  - integration not yet verified
  - end-to-end still failing
- Never collapse these into a generic works.

## Guidance For Updating Existing Skills

When improving a repo's existing skill rather than creating a new one:

- Preserve the repo's own terminology, file layout, and workflow.
- Merge repeated lessons into concise rules instead of appending long narratives.
- Separate hard constraints from local heuristics.
- Prefer adding short references to existing source-of-truth files over duplicating them.
- If the skill contains stale or overly repo-specific history, compress it into stable guidance.
- If the skill overstates maturity, rewrite it to distinguish local verification from true system completion.

## Maintenance Rules For This Skill

Update this skill only when a pattern is:

- recurring
- cross-task useful
- likely to matter again

Do not add:

- one-off anecdotes
- temporary branch details
- long command catalogs
- issue-specific lore that belongs in a bug log or reference file
- repo-specific file names unless this skill is being intentionally specialized for that repo
