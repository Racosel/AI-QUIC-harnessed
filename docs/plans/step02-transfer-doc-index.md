# 步骤02 文档目录：流控 + 多流传输基础

更新于 `2026-04-16`

## 目标

本文档是步骤02的专项入口，用于指导后续实现与排障围绕以下最小闭环推进：

- 在单个 QUIC 连接上完成多条双向流的并发 HTTP/0.9 文件下载。
- 建立 `stream manager + connection/stream flow control + send scheduler + receive reassembly` 的最小稳定组合。
- 与 `quic-interop-runner` 的 `transfer` testcase 对齐，支持“小初始窗口 + 传输期间增长窗口”的运行语义。

本地完成标准按更严格口径执行：

- `step2` 完成，意味着 **步骤01仍保持通过，且步骤02能力可独立验证通过**。
- `transfer` 与 `handshake` 共用同一套 `endpoint / connection / packet number spaces / TLS` 骨架，不允许为了步骤02复制一套独立 transport 状态机。
- 步骤02只扩展应用数据期能力，但验收必须覆盖“多流并发 + 流控增长 + 尾部恢复 + 文件落盘一致性”。

说明：

- `docs/plans/plan-quic.md` 已将步骤02定义为“流控与多流传输基础”。
- 本文档只定义步骤02的专项阅读顺序、完成标准与排障入口，不改写主计划的阶段排序。
- 本文档吸收了 `2026-04-16` 的 transfer 审计结论，但不依赖那份审计文件作为长期入口。

## 建议阅读顺序

1. `docs/plans/plan-quic.md`
   - 先对齐步骤02的阶段边界、前置依赖与验收重点。
2. `docs/plans/repo-file-hierarchy.md`
   - 先明确 stream / flow control / demo / interop / test 应落在哪些目录。
3. `docs/quic-interop-runner/quic-test-cases.md`
   - 先锁定 `transfer` 的 testcase 语义：单连接、多流并发、小初始窗口、窗口增长。
4. `docs/quic-interop-runner/how-to-run.md`
   - 先锁定 `/www`、`/downloads`、`REQUESTS`、`SSLKEYLOGFILE`、`QLOGDIR` 与日志目录契约。
5. `docs/quic-interop-runner/implement-requirements.md`
   - 先锁定退出码 `0 / 1 / 127` 的边界。
6. `docs/ietf/notes/9000.md`
   - 先核对 stream state、flow control、final size、`MAX_*` / `*_BLOCKED` 的工程语义。
7. `docs/ietf/notes/9002.md`
   - 核对 ACK、loss、PTO、尾部恢复与拥塞控制边界。
8. `docs/ietf/txt/rfc9000.txt`
   - 对 `MAX_DATA`、`MAX_STREAM_DATA`、`DATA_BLOCKED`、`STREAM_DATA_BLOCKED`、`FINAL_SIZE_ERROR` 做权威核对。
9. `docs/ietf/txt/rfc9002.txt`
   - 对 RTT/PTO/loss detection 的行为边界做权威核对。
10. `docs/refs/xquic.md`
   - 对照 `packet_out + send queue + ACK/retransmit/new data` 调度顺序。
11. `docs/refs/lsquic.md`
   - 对照“重传按 frame descriptor 重编码”和“读取驱动窗口更新”的设计。
12. `docs/refs/mvfst.md`
   - 对照 `pending events + scheduler + flow control regression` 的组织方式。
13. 当前仓库 transfer 相关代码
   - `ai-quic/include/ai_quic/stream.h`
   - `ai-quic/src/transport/stream.c`
   - `ai-quic/src/transport/conn_io.c`
   - `ai-quic/src/transport/loss.c`
   - `ai-quic/src/transport/transport_params.c`
   - `ai-quic/tests/unittest/test_unit.c`
   - `ai-quic/tests/integration/test_handshake.c`
14. `docs/plans/step02-transfer-logic-design.md`
   - 在编码前审查步骤02的行为级实现规格。

## 主题目录

| 主题 | 首选文档 | 本步要点 |
|:--|:--|:--|
| 阶段目标 | `docs/plans/plan-quic.md` | 步骤02是 `transfer`，目标是流控、多流与基础调度。 |
| 目录与代码落点 | `docs/plans/repo-file-hierarchy.md` | 明确新代码、测试和 interop 入口的目录边界。 |
| testcase 语义 | `docs/quic-interop-runner/quic-test-cases.md` | `transfer` 关注单连接、多流并发、窗口增长。 |
| runner 契约 | `docs/quic-interop-runner/how-to-run.md` | 服务端读 `/www`，客户端写 `/downloads`。 |
| 退出码 | `docs/quic-interop-runner/implement-requirements.md` | 成功 `0`，错误 `1`，未支持 `127`。 |
| QUIC 传输约束 | `docs/ietf/notes/9000.md` + `docs/ietf/txt/rfc9000.txt` | stream state、flow control、final size、`MAX_*`。 |
| 恢复与 PTO | `docs/ietf/notes/9002.md` + `docs/ietf/txt/rfc9002.txt` | ACK/loss/PTO 与尾部恢复。 |
| 参考实现：xquic | `docs/refs/xquic.md` | 调度原子、send queue、ACK/retransmit/new data 顺序。 |
| 参考实现：lsquic | `docs/refs/lsquic.md` | 重传重编码、读取驱动窗口更新。 |
| 参考实现：mvfst | `docs/refs/mvfst.md` | `pending events + scheduler` 与专项测试组织。 |
| 当前代码映射 | 当前仓库 transfer 相关代码 | 用现有实现锚点对齐设计，不脱离实际代码结构。 |
| 逻辑设计 | `docs/plans/step02-transfer-logic-design.md` | 统一的步骤02实现规格。 |

## 用户主入口：根目录 Makefile

步骤02沿用步骤01的入口约束，并新增 transfer 相关主入口：

- 仓库根目录必须维护一个 `Makefile`。
- 所有高频构建、运行、排障命令优先通过 `make` 暴露。
- `Makefile` 是用户主入口；`python3 run.py ...` 与底层 interop 脚本只作为实现细节。

步骤02至少应预留以下目标：

- `make build`
- `make test-unit`
- `make test-integration`
- `make test-interop-transfer`
- `make interop-transfer-server`
- `make interop-transfer-client`
- `make interop-logs CASE=transfer`

如果这些目标尚未落地，表示步骤02的用户入口仍未交付完整。

## 步骤02最小实现清单

- 复用步骤01连接骨架，在 AppData 空间上完成最小多流发送/接收路径。
- 支持 `stream manager` 的 lazy materialization、方向检查、Stream ID 分配与并发流管理。
- 为每条流维护独立的发送状态、接收重组状态、流级流控状态与最终大小状态。
- 独立维护连接级与流级流控：
  - `initial_max_data`
  - `initial_max_stream_data_*`
  - `MAX_DATA`
  - `MAX_STREAM_DATA`
  - `DATA_BLOCKED`
  - `STREAM_DATA_BLOCKED`
- 接收缓冲必须按 offset 组织，不得假设顺序到达。
- 应用层只能消费连续数据；消费后推进 `MAX_*` 更新，且不得把窗口更新错误绑定为“等对端先 blocked”。
- 发送调度至少满足：
  - ACK / 控制帧优先于应用新数据
  - 恢复态下优先考虑重传或探测
  - 多流新数据发送具备基本公平性，单流不能长期独占发送预算
- 客户端能把多个下载目标依次/并发映射到独立请求流，并把结果写入 `/downloads`。
- 服务端能从 `/www` 读取目标文件，并按请求流返回完整响应体。
- 失败时能从 runner 日志、端点日志、pcap、qlog 快速定位卡点。

## 关键约束速查

### 步骤02：流控与多流传输基础

- `transfer` testcase 使用单连接、多流并发下载，不是多连接拼接成功。
- 连接级与流级流控必须独立建模，不得混成单一“剩余额度”。
- `MAX_STREAM_DATA` 表示单流最大绝对偏移；`MAX_DATA` 表示所有流累计接收字节上限。
- 接收方不得等待 `*_BLOCKED` 才发送 `MAX_*`；窗口更新应主动发生。
- final size 一旦锁定，后续任何与之矛盾的数据都必须按协议错误处理。
- `RESET_STREAM`、`STOP_SENDING` 不得破坏既有 flow control 与 final size 状态。
- 重传必须使用新 packet number 重新编码，不得把旧包原样复用为“重传包”。
- 尾部恢复必须是可观测的：日志或 qlog 至少能判断当前是否只剩重传、是否还有流控卡点、最后一个未完成区间在哪里。

## 验收口径

### 与 runner 语义对齐的 `transfer`

与 `quic-interop-runner` 对齐后，成功至少要满足：

- 建立单个 QUIC 连接。
- 在该连接上并发创建多个请求流。
- 客户端使用较小初始流级/连接级窗口，并在传输期间触发窗口更新。
- 所有目标文件都下载成功，且客户端落盘文件与服务端源文件内容完全一致。
- 无流控死锁、无无限重传、无尾部收口卡死。

这意味着：

- “握手成功并进入 1-RTT” 还不够。
- “部分文件传完、最后一个文件尾部缺失” 不算通过。
- “日志里看见发了 `MAX_DATA`，但文件没完整落盘” 也不算通过。

### 步骤02完成判定

只有同时满足以下两项，才算步骤02完成：

- 步骤01的 `handshake` 能力仍保持通过。
- 步骤02的 `transfer` 能力可在单连接、多流、小窗口增长场景下验证通过。

## 结合审计的优先风险提示

`2026-04-16` 的 transfer 审计给出三条对步骤02设计直接相关的提醒：

- 尾部恢复必须可观测、可推进，不能长期停留在“只剩 retransmit 但没有明确前进证据”的状态。
- 只依赖“应用已消费”驱动窗口更新时，必须留出足够 margin / reserve，避免大文件与乱序下本地 `recv_limit exceeded`。
- qlog 与普通日志必须至少覆盖：
  - `recv progress`
  - `app consumed progress`
  - `MAX_DATA / MAX_STREAM_DATA` 调度与发送
  - `DATA_BLOCKED / STREAM_DATA_BLOCKED`
  - tail recovery / retransmit-only 相关状态

## 建议的排障顺序

固定按以下顺序排查：

1. `runner-output.txt`
   - 先区分是容器启动/退出问题，还是 testcase 语义检查失败。
2. `server/` 与 `client/` 日志
   - 再定位卡在流创建、流控增长、重传恢复、下载落盘还是完成判定阶段。
3. `sim/` 抓包
   - 如果日志不足，再判断真实发包、ACK、尾部重传与是否存在异常静默期。
4. `qlog` / `keylog`
   - 如果仍不足，再看 packet 层事件、密钥阶段与发送/接收时序。

## 建议的验证证据目录

- `artifacts/interop/<timestamp>-<server>-<client>-transfer/runner-output.txt`
- `artifacts/interop/<timestamp>-<server>-<client>-transfer/runner/server/`
- `artifacts/interop/<timestamp>-<server>-<client>-transfer/runner/client/`
- `artifacts/interop/<timestamp>-<server>-<client>-transfer/runner/sim/`
- `QLOGDIR` 输出目录

## 推荐操作方式

- 规划步骤02时，先读本文件，再按“建议阅读顺序”补齐原始文档。
- 需要决定代码落点时，先读 `docs/plans/repo-file-hierarchy.md`。
- 运行步骤02时，优先从仓库根目录执行 `make` 目标。
- 原始长命令只放在附录作为 `Makefile` 的底层实现说明，不作为主入口。
- 若后续重命名本文件或 `docs/plans/step02-transfer-logic-design.md`，再同步更新 `docs/menu.md`。

## 附录：Make 目标与底层命令的映射约束

以下长命令不应作为用户主入口，而应封装进根目录 `Makefile`：

- `make test-interop-transfer`
  - 底层实现可调用双方方向的 `interop-repeat` 或等价脚本。
- `make interop-transfer-server`
  - 底层实现可调用“我方 server + 对端 client”的 `transfer` 组合。
- `make interop-transfer-client`
  - 底层实现可调用“对端 server + 我方 client”的 `transfer` 组合。
- `make interop-logs CASE=transfer`
  - 底层实现可统一列出或打开对应 transfer 日志目录，并按 `runner-output -> server/client -> sim -> qlog` 顺序展示排障入口。

文档中优先写 `make` 目标；如果需要解释底层实现，再补原始命令。
