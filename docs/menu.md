# Docs 检索菜单（Agent 优先）

本文件是 `docs/` 的统一入口，目标是让 Agent 用最少阅读量拿到正确结论。

维护规则：
- 新增、删除、重命名 `docs/` 下文件时，必须同步更新本文件。
- 检索默认从本文件开始，不应直接全量扫描 `docs/`。

## 区块 1：30 秒快速入口

| 任务                         | 首选文档（默认先读 1-2 个）                                                                       | 说明                                   |
|:-----------------------------|:------------------------------------------------------------------------------------------------|:---------------------------------------|
| 规划 QUIC 实现阶段与验收路径 | `docs/plans/plan-quic.md` + `docs/quic-interop-runner/quic-test-cases.md`                       | 先定阶段目标，再对齐测试用例映射。       |
| 互操作测试执行/排障          | `docs/quic-interop-runner/how-to-run.md` + `docs/quic-interop-runner/implement-requirements.md` | 先确认运行机制，再确认退出码约束。       |
| 选择对比实现与镜像           | `docs/quic-interop-runner/quic-implement-images.md`                                             | 快速确认可用实现、镜像与角色。           |
| 核对 QUIC 工程语义（非权威）   | `docs/ietf/notes/9000.md`                                                                       | 工程化摘要，适合快速定位实现要点。       |
| 补充流状态机/帧细节          | `docs/ietf/notes/9000 copy.md`                                                                  | 仅在 `9000.md` 信息不足时再读。         |
| 进行 RFC 权威核对            | `docs/ietf/txt/rfc9000.txt` + `docs/ietf/txt/rfc9001.txt`                                       | 先传输层，再 TLS 集成。                  |
| 核对丢包恢复与拥塞控制       | `docs/ietf/txt/rfc9002.txt`                                                                     | Loss Detection/PTO/拥塞行为以该文为准。 |
| 核对 HTTP/3 与 QPACK         | `docs/ietf/txt/rfc9114.txt` + `docs/ietf/txt/rfc9204.txt`                                       | HTTP/3 语义与头压缩配置。               |
| 核对 DATAGRAM 与 qlog 事件   | `docs/ietf/txt/rfc9221.txt` + `docs/ietf/txt/draft-ietf-quic-qlog-quic-events-12.txt`           | 扩展能力与观测事件定义。                |
| 配置/运行 MegaLinter         | `docs/mega-linter/config.md` + `docs/mega-linter/run-locally.md`                                | 先配置优先级，再执行本地运行。           |

## 区块 2：任务阅读包（最小阅读集合）

### 阅读包 A：实现阶段规划
- 默认读取：
  - `docs/plans/plan-quic.md`
  - `docs/quic-interop-runner/quic-test-cases.md`
- 何时升级：
  - 若涉及团队流程/审计体系，再读 `docs/plans/plan-ai.md`。

### 阅读包 B：互操作测试排障
- 默认读取：
  - `docs/quic-interop-runner/how-to-run.md`
  - `docs/quic-interop-runner/implement-requirements.md`
- 何时升级：
  - 若需要映射具体测试语义，再读 `docs/quic-interop-runner/quic-test-cases.md`。
  - 若需要选择对端实现，再读 `docs/quic-interop-runner/quic-implement-images.md`。

### 阅读包 C：RFC 规范核对
- 默认读取：
  - `docs/ietf/notes/9000.md`
  - 对应 RFC 原文（按问题选择一个：`rfc9000.txt`/`rfc9001.txt`/`rfc9002.txt`）
- 何时升级：
  - 需要 HTTP/3 / QPACK 时，追加 `rfc9114.txt` / `rfc9204.txt`。
  - 需要 DATAGRAM 或 qlog 时，追加 `rfc9221.txt` 或 `draft-ietf-quic-qlog-quic-events-12.txt`。
  - 仅当状态机/帧细节仍不够时，追加 `docs/ietf/notes/9000 copy.md`。

### 阅读包 D：MegaLinter 配置与执行
- 默认读取：
  - `docs/mega-linter/config.md`
  - `docs/mega-linter/run-locally.md`
- 何时升级：
  - 需要具体 Linter Key 时，再读 `docs/mega-linter/linter-list.md`。

## 区块 3：全量文档索引表

> 说明：优先级以“排障/实现落地价值”为准。
>
> - `P0`：首轮优先读。
> - `P1`：首轮不足时追加。
> - `P2`：仅在专项核对时读取。

| 路径                                                    | 优先级(P0/P1/P2) | 作用                                       | 何时查阅                             | 建议先读/后读                 |
|:--------------------------------------------------------|:-----------------|:-------------------------------------------|:-------------------------------------|:------------------------------|
| `docs/menu.md`                                          | P0               | `docs/` 统一检索入口与路由规则             | 任何文档检索任务开始前               | 先读                          |
| `docs/plans/plan-quic.md`                               | P0               | QUIC 实现 8 阶段规划、模块划分、验收目标     | 需要制定实现计划或确认阶段目标       | 先读                          |
| `docs/plans/plan-ai.md`                                 | P1               | 知识体系与 AI 外审计流程规划               | 需要定义治理流程、审计节奏            | 后读（在 `plan-quic.md` 之后）  |
| `docs/quic-interop-runner/how-to-run.md`                | P0               | Interop Runner 运行机制、环境要求与日志结构 | 运行互操作测试、定位运行环境问题      | 先读                          |
| `docs/quic-interop-runner/implement-requirements.md`    | P0               | 容器退出码契约（0/1/127）                    | 排查测试结果判定、支持性问题          | 先读（与 `how-to-run.md` 同级） |
| `docs/quic-interop-runner/quic-test-cases.md`           | P0               | QUIC 测试用例定义与 `TESTCASE` 映射        | 设计测试矩阵、解释失败用例语义        | 先读（排障时）                  |
| `docs/quic-interop-runner/quic-implement-images.md`     | P1               | 可用实现镜像、仓库地址、角色信息             | 选择对比对象、切换 server/client 组合 | 后读（在测试流程确认后）        |
| `docs/mega-linter/config.md`                            | P0               | MegaLinter 配置规则、优先级、过滤与安全项    | 编写/审阅 `.mega-linter.yml`、调参    | 先读                          |
| `docs/mega-linter/run-locally.md`                       | P0               | 本地安装、命令参数与执行示例                | 本地跑 linter、启用 `--fix`           | 先读（与 `config.md` 配套）     |
| `docs/mega-linter/linter-list.md`                       | P2               | 全量 Linter Key 对照表                     | 需要精确启用/禁用特定 Linter Key     | 后读（仅查表）                  |
| `docs/ietf/notes/9000.md`                               | P0               | RFC9000 工程化笔记（实现导向）               | 快速建立传输层实现语义               | 先读                          |
| `docs/ietf/notes/9000 copy.md`                          | P1               | `9000.md` 的扩展版，含更细状态机/帧说明     | 需要补齐状态机或帧细节时             | 后读（默认不首读）              |
| `docs/ietf/txt/rfc9000.txt`                             | P1               | QUIC 传输层权威规范原文                    | 需要权威条文校对传输语义             | 后读（在 `9000.md` 之后）       |
| `docs/ietf/txt/rfc9001.txt`                             | P1               | QUIC + TLS 集成权威规范原文                | 核对握手、密钥、TLS 交互细节           | 后读                          |
| `docs/ietf/txt/rfc9002.txt`                             | P1               | 丢包检测与拥塞控制权威规范原文             | 核对 PTO、ACK 时序、拥塞行为           | 后读                          |
| `docs/ietf/txt/rfc9114.txt`                             | P2               | HTTP/3 映射规范原文                        | 涉及 HTTP/3 语义与帧层行为时         | 后读（专项）                    |
| `docs/ietf/txt/rfc9204.txt`                             | P2               | QPACK 头压缩规范原文                       | 涉及 HTTP/3 头压缩与动态表时         | 后读（专项）                    |
| `docs/ietf/txt/rfc9221.txt`                             | P2               | QUIC DATAGRAM 扩展规范原文                 | 涉及不可靠消息扩展时                 | 后读（专项）                    |
| `docs/ietf/txt/draft-ietf-quic-qlog-quic-events-12.txt` | P2               | qlog 事件草案定义                          | 需要精确事件字段/日志语义时          | 后读（专项）                    |

## 区块 4：去重与冲突规则

1. 同主题优先级规则：
   - 工程快速落地优先读 `notes`，权威核对再读 `ietf/txt`。
2. `9000.md` 与 `9000 copy.md`：
   - 默认首选 `docs/ietf/notes/9000.md`。
   - 仅当需要补充状态机、帧类型或更细工程细节时才读 `docs/ietf/notes/9000 copy.md`。
3. MegaLinter 三件套读取顺序：
   - `config.md` -> `run-locally.md` -> `linter-list.md`。
4. Interop 文档读取顺序：
   - `how-to-run.md` + `implement-requirements.md` 为基础；
   - `quic-test-cases.md` 用于语义映射；
   - `quic-implement-images.md` 用于实现选择。
5. 阅读预算冲突处理：
   - 首轮最多 2 个文件；结论不足时，先声明信息缺口，再追加 1 个最小必要文件，禁止直接全量扫描。
6. 资料缺口处理协议：
   - 当阅读包无法覆盖问题时，优先输出“缺失资料/工具请求”；
   - 请求内容必须包含：缺失项、用途、建议补充位置或安装方式，且与 `skills/doc-reference/skill.md` 输出模板保持一致；
   - 先上报缺口，再继续追加检索。
