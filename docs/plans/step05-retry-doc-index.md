# 步骤05 文档目录：Token 与地址验证链路（Retry）

更新于 `2026-04-23`

## 目标

本文档是步骤05的专项入口，用于指导后续实现与排障围绕以下最小闭环推进：

- 对齐 `TESTCASE="retry"` 的 runner 语义：服务端生成 Retry，客户端在后续 Initial 中携带 Retry token 继续握手并完成小文件下载。
- 把 Retry、token 校验、地址验证、anti-amplification 与 CID 一致性串成一条完整链路，而不是分散在连接内部零散补丁。
- 明确区分 `Retry token`、`NEW_TOKEN`、`resumption/application token` 三类对象的语义和作用域。
- 保持步骤01/02/03/04已经通过的 `handshake`、`transfer`、`v2` 与 `chacha20` 路径不回退。

本地完成标准按更严格口径执行：

- `retry` 不是新的 transport 状态机；它应复用既有 `connection / recovery / stream / flow control` 主逻辑。
- Retry 必须在 `dispatcher / endpoint` 的连接创建之前完成决策与响应，保持无状态路径。
- 服务端在地址未验证前，发送预算必须严格受 `bytes_received * 3` 约束，重传与 coalescing 也必须计入同一预算。
- 步骤05能力完成后，应能单独验证 Retry 成功、token 篡改失败、client 地址变化导致 token 失效、ODCID 不匹配等行为，并持续跟踪 `amplificationlimit` 影子回归。

说明：

- `docs/plans/plan-quic.md` 已将步骤05定义为“Token 与地址验证链路”。
- 本文档只定义步骤05的专项阅读顺序、完成标准与排障入口，不改写主计划的阶段排序。
- 当前仓库里 `retry` testcase 仍未开放，`conn_io.c` 只有基础反放大检查，`transport_params.c` 已有 `original_destination_connection_id / retry_source_connection_id` 字段但校验链未完整，`dispatcher / endpoint` 仍缺无状态 Retry 分支。

## 建议阅读顺序

1. `docs/plans/plan-quic.md`
   - 先对齐步骤05的阶段边界、前置依赖、注意事项与验收重点。
2. `docs/plans/repo-file-hierarchy.md`
   - 先明确 token service、dispatcher、packet codec、interop、test 应落在哪些目录。
3. `docs/quic-interop-runner/quic-test-cases.md`
   - 先锁定 `retry` 的 testcase 语义：服务端发送 Retry，客户端在 Initial 中使用 Retry token。
4. `docs/quic-interop-runner/how-to-run.md`
   - 先锁定 `/www`、`/downloads`、`SSLKEYLOGFILE`、`QLOGDIR`、日志目录契约。
5. `docs/quic-interop-runner/implement-requirements.md`
   - 先锁定退出码 `0 / 1 / 127` 的边界。
6. `docs/ietf/notes/9000.md`
   - 快速核对 Retry、token、地址验证、transport parameters、反放大限制与 CID 一致性边界。
7. `docs/ietf/txt/rfc9000.txt`
   - 对 Retry 包格式、token 语义、`NEW_TOKEN`、反放大限制、`original_destination_connection_id / retry_source_connection_id` 做权威核对。
8. `docs/ietf/notes/9001.md`
   - 快速核对 Retry integrity tag、Initial key 复派生与 Retry 后 0-RTT/Initial 行为边界。
9. `docs/ietf/txt/rfc9001.txt`
   - 对 Retry integrity tag、Retry pseudo-packet 与 Initial key 复派生做权威核对。
10. `docs/ietf/notes/9002.md`
    - 快速核对地址验证与 PTO/重传边界，避免后续把恢复计账做散。
11. `docs/ietf/txt/rfc9002.txt`
    - 需要权威确认 PTO、probe packet 与反放大协同时再读。
12. `docs/refs/xquic.md`
    - 对照 Retry / token / 地址验证 / ODCID / Retry SCID 的对象模型与状态拆分。
13. `docs/refs/lsquic.md`
    - 对照“先校验 token，再决定是否创建连接”的 dispatcher 级前置筛选思路。
14. `docs/refs/mvfst.md`
    - 对照 server worker 层处理 Retry、client Retry 后重建 Initial 路径的设计。
15. 当前仓库 Step05 相关代码
    - `ai-quic/include/ai_quic/packet.h`
    - `ai-quic/include/ai_quic/dispatcher.h`
    - `ai-quic/src/transport/version.c`
    - `ai-quic/src/transport/packet_codec.c`
    - `ai-quic/src/transport/dispatcher.c`
    - `ai-quic/src/transport/endpoint.c`
    - `ai-quic/src/transport/conn_io.c`
    - `ai-quic/src/transport/transport_params.c`
    - `ai-quic/interop/testcase_dispatch.sh`
    - `ai-quic/interop/run_endpoint.sh`
    - `ai-quic/tests/interop/test_run_endpoint.sh`
16. `docs/plans/step05-retry-logic-design.md`
    - 在编码前审查步骤05的行为级实现规格。

## 主题目录

| 主题 | 首选文档 | 本步要点 |
|:--|:--|:--|
| 阶段目标 | `docs/plans/plan-quic.md` | 步骤05是 `retry`，目标是把 Retry、token、地址验证与反放大串成一条链。 |
| 目录与代码落点 | `docs/plans/repo-file-hierarchy.md` | 明确 dispatcher/token/codec/interop/test 的目录边界。 |
| testcase 语义 | `docs/quic-interop-runner/quic-test-cases.md` | `retry` 关注服务端是否发送 Retry、客户端是否在第二个 Initial 携带 token。 |
| runner 契约 | `docs/quic-interop-runner/how-to-run.md` | 服务端读 `/www`，客户端写 `/downloads`，保留 keylog/qlog。 |
| 退出码 | `docs/quic-interop-runner/implement-requirements.md` | 成功 `0`，错误 `1`，未支持 `127`。 |
| Retry 与 token | `docs/ietf/notes/9000.md` + `docs/ietf/txt/rfc9000.txt` | Retry、token、`NEW_TOKEN`、地址绑定与 transport parameters 校验链。 |
| Retry integrity | `docs/ietf/notes/9001.md` + `docs/ietf/txt/rfc9001.txt` | Retry integrity tag 与 Retry 后 Initial key 复派生。 |
| 反放大与恢复 | `docs/ietf/notes/9002.md` + `docs/ietf/txt/rfc9002.txt` | 地址未验证前的发送预算、PTO probe 与计账边界。 |
| 参考实现：xquic | `docs/refs/xquic.md` | Retry、token、地址验证与 CID 三元组是一条链。 |
| 参考实现：lsquic | `docs/refs/lsquic.md` | token 校验尽量在创建连接之前完成。 |
| 参考实现：mvfst | `docs/refs/mvfst.md` | server worker 层处理 Retry，client 收到 Retry 后重建 Initial 状态。 |
| 当前代码映射 | 当前仓库 Step05 相关代码 | 当前已有 token 字段与 Retry 常量雏形，但尚未具备完整 Retry 链路。 |
| 逻辑设计 | `docs/plans/step05-retry-logic-design.md` | 统一的步骤05实现规格。 |

## 用户主入口：根目录 Makefile

步骤05沿用前序步骤的入口约束：

- 仓库根目录必须维护一个 `Makefile`。
- 所有高频构建、运行、排障命令优先通过 `make` 暴露。
- `Makefile` 是用户主入口；`python3 run.py ...` 与底层 interop 脚本只作为实现细节。

步骤05实现完成后至少应预留以下目标：

- `make build`
- `make test-unit`
- `make test-integration`
- `make test-interop-handshake`
- `make test-interop-transfer`
- `make test-interop-v2`
- `make test-interop-chacha20`
- `make test-interop-retry`
- `make interop-retry-server`
- `make interop-retry-client`
- `make interop-logs CASE=retry`

当前仓库现状需要明确写清：

- `Makefile` 目前还没有 `retry` 相关目标。
- `ai-quic/interop/testcase_dispatch.sh` 目前不会放行 `retry`。
- 因此在步骤05真正实现完成之前，`retry` 仍应按 runner 契约返回 `127`，而不是伪装成失败或部分支持。

如果这些目标和入口约束尚未落地，表示步骤05的用户入口仍未交付完整。

## 步骤05最小实现清单

- 为 QUIC v1/v2 补齐 Retry 包类型、Retry pseudo-packet 与 Retry integrity tag 编解码。
- 把 token 服务做成独立模块，至少建模：
  - `Retry token`
  - `NEW_TOKEN`
  - `resumption / application token`
- Retry token 负载至少绑定：
  - client 地址
  - original destination connection ID
  - 时间戳/有效期
  - 防伪 MAC
- `dispatcher` 必须在创建连接之前完成：
  - token 解析
  - token 校验
  - 是否发送 Retry 的决策
  - 无状态 Retry 响应
- 客户端收到合法 Retry 后，必须：
  - 验证 Retry integrity tag
  - 保存 Retry token
  - 将后续 Initial 的 DCID 切到 Retry SCID
  - 重建 Initial 发送路径并复派生 Initial keys
- transport parameters 检查链必须覆盖：
  - 无 Retry 时校验 `original_destination_connection_id`
  - 有 Retry 时额外校验 `retry_source_connection_id`
  - 对齐 `ODCID / Retry SCID / current DCID` 的一致性
- 服务端地址未验证前必须严格执行 3 倍反放大限制，并确保重传与 coalescing 共用同一记账口径。
- interop、测试与日志入口必须补齐，使 `retry` 能作为独立 testcase 验证，同时从本步开始跟踪 `amplificationlimit`。

## 关键约束速查

### 步骤05：Retry / Token / Address Validation

- `retry` 不是第二套 transport 状态机；它应复用步骤01/02/03/04已有的连接、恢复、流控、多流与版本逻辑。
- Retry 必须走连接创建之前的无状态路径；不要在 `conn_io.c` 内部“半路补一个 Retry 分支”。
- `Retry token`、`NEW_TOKEN`、`resumption/application token` 不是一类东西，不能共用一套含义不清的结构和密钥空间。
- 客户端必须独立验证 Retry integrity tag，不能只相信“这是一个长首部 Retry 包”。
- 客户端收到 Retry 后，后续 Initial 的 DCID 必须改成 Retry 包中的 SCID，并据此复派生 Initial keys。
- 无 Retry 路径中，`retry_source_connection_id` 必须保持缺省；不要为了图省事把它写成“和 current SCID 一样”。
- 服务端在地址未验证前，发送预算必须受 `bytes_received * 3` 约束；重传、ACK-only、coalescing 与 probe packet 也要共用这个账本。
- 地址绑定意味着 client 源地址变化时 token 可能失效，这不是 bug，而是设计结果。
- 从本步开始应把 `amplificationlimit` 当作影子回归入口，但不另外拆第三份规划文档。

## 验收口径

### 与 runner 语义对齐的 `retry`

与 `quic-interop-runner` 对齐后，成功至少要满足：

- 服务端在 `TESTCASE="retry"` 下发送 Retry。
- 客户端正确验证 Retry integrity tag，并在第二个 Initial 中携带 Retry token。
- 客户端后续 Initial 的 DCID 正确切到服务端指定的 Retry SCID。
- 双端完成后续握手和小文件下载，客户端下载结果与服务端源文件完全一致。
- transport parameters 验证链能区分“无 Retry”和“有 Retry”两种路径。

这意味着：

- “Initial 里能塞一个 token 字段” 还不够。
- “服务端发了一个长首部包，看起来像 Retry” 不算通过。
- “Retry 通过了，但 `handshake/transfer/v2/chacha20` 回退” 也不算通过。

### 步骤05完成判定

只有同时满足以下六项，才算步骤05完成：

- 步骤01的 `handshake` 路径仍可验证通过。
- 步骤02的 `transfer` 路径仍可验证通过。
- 步骤03的 `v2` 路径仍可验证通过。
- 步骤04的 `chacha20` 路径仍可验证通过。
- 步骤05的 `retry` 能力可在 runner 中验证通过。
- 单测/集成测试能覆盖 Retry integrity、token 篡改失败、地址变化 token 失效、TP 一致性校验与反放大计账。

### 推荐的负例验收

步骤05至少应补齐以下负例证据：

- token 篡改失败
- client 地址变化导致 token 失效
- original DCID 不匹配
- `retry_source_connection_id` 缺失或错误
- 第二次收到 Retry 时客户端正确丢弃
- 地址未验证前服务端不会超出 3 倍发送预算

## 建议的排障顺序

固定按以下顺序排查：

1. `runner-output.txt`
   - 先区分是 testcase 未开放、环境/容器问题、Retry/下载语义失败，还是纯运行错误。
2. `server/` 与 `client/` 日志
   - 再定位 Retry 是否发出、token 是否通过校验、第二个 Initial 是否带 token、地址验证是否生效。
3. `sim/` 抓包
   - 判断服务端是否真的发出 Retry、客户端第二个 Initial 的 DCID 是否切到了 Retry SCID、是否存在超出预算的发送。
4. `qlog`
   - 如果日志不足，再看 Retry 决策、packet_dropped、address validation 与发送预算变化。
5. `SSLKEYLOGFILE`
   - Retry 本身不加密；若问题进入 Handshake/1-RTT，再结合 keylog 分析后续密钥阶段。

## 建议的验证证据目录

- `artifacts/interop/<timestamp>-<server>-<client>-retry/runner-output.txt`
- `artifacts/interop/<timestamp>-<server>-<client>-retry/runner/server/`
- `artifacts/interop/<timestamp>-<server>-<client>-retry/runner/client/`
- `artifacts/interop/<timestamp>-<server>-<client>-retry/runner/sim/`
- `QLOGDIR` 输出目录
- `SSLKEYLOGFILE` 输出文件

## 推荐操作方式

- 规划步骤05时，先读本文件，再按“建议阅读顺序”补齐原始文档。
- 需要决定代码落点时，先读 `docs/plans/repo-file-hierarchy.md`。
- 运行步骤05时，优先从仓库根目录执行 `make` 目标。
- 原始长命令只放在附录作为 `Makefile` 的底层实现说明，不作为主入口。
- 若后续重命名本文件或 `docs/plans/step05-retry-logic-design.md`，再同步更新 `docs/menu.md`。

## 附录：Make 目标与底层命令的映射约束

以下长命令不应作为用户主入口，而应封装进根目录 `Makefile`：

- `make test-interop-retry`
  - 底层实现可先跑 ai-quic 双端 Retry smoke，再扩展为真实 runner 对端方向。
- `make interop-retry-server`
  - 底层实现可调用“我方 server + 对端 client”的 `retry` 组合。
- `make interop-retry-client`
  - 底层实现可调用“对端 server + 我方 client”的 `retry` 组合。
- `make interop-logs CASE=retry`
  - 底层实现可统一列出对应 retry 日志目录，并按 `runner-output -> server/client -> sim -> qlog -> keylog` 顺序展示排障入口。

文档中优先写 `make` 目标；如果需要解释底层实现，再补原始命令。
