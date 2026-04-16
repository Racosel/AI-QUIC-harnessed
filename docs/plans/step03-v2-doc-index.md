# 步骤03 文档目录：QUIC v2 协商与兼容

更新于 `2026-04-16`

## 目标

本文档是步骤03的专项入口，用于指导后续实现与排障围绕以下最小闭环推进：

- 在不复制整套 transport 状态机的前提下，为当前仓库补齐 QUIC v2 支持。
- 按 RFC 9368 / RFC 9369 建立 `v1 -> v2` 的 compatible version negotiation 路径。
- 保持步骤01/02已经通过的 `handshake` / `transfer` 路径不回退。
- 把版本差异收敛到 `dispatcher / version negotiation / packet codec / Initial key schedule / transport parameters` 等少数边界模块，并由统一 `dispatcher` 代理入口决定连接后续按 v1 还是 v2 推进。

本地完成标准按更严格口径执行：

- `step3` 完成，意味着 **步骤01与步骤02仍保持通过，且步骤03能力可独立验证通过**。
- `v2`、`handshake`、`transfer` 共用同一套 `connection / recovery / stream / flow control` 主逻辑，不允许为了 v2 再复制一套连接状态机。
- 版本选择必须遵循代理模式：由 `dispatcher` 统一完成版本分流与 `version_ops` 选择，连接内部绝不复制粘贴出 `v1_conn/v2_conn` 两套平行主逻辑。
- 步骤03只引入版本协商与版本相关编解码/密钥差异，但验收必须覆盖“compatible negotiation + v2 小文件下载 + v1 不回退 + 未知版本仍走无状态 VN”。

说明：

- `docs/plans/plan-quic.md` 已将步骤03定义为“QUIC v2 协商与兼容”。
- 本文档只定义步骤03的专项阅读顺序、完成标准与排障入口，不改写主计划的阶段排序。
- 进入步骤03前，本地已完成 `transfer` 双向各 10 轮真实 interop 并全部通过，因此步骤03默认以“保护既有稳定性”为前提推进。

## 建议阅读顺序

1. `docs/plans/plan-quic.md`
   - 先对齐步骤03的阶段边界、前置依赖与验收重点。
2. `docs/plans/repo-file-hierarchy.md`
   - 先明确 version / codec / tls / interop / test 应落在哪些目录。
3. `docs/quic-interop-runner/quic-test-cases.md`
   - 先锁定 `v2` 的 testcase 语义：客户端以 v1 发起，携带 v2，服务端兼容协商到 v2，再下载小文件。
4. `docs/quic-interop-runner/how-to-run.md`
   - 先锁定 `/www`、`/downloads`、`SSLKEYLOGFILE`、`QLOGDIR`、日志目录契约。
5. `docs/quic-interop-runner/implement-requirements.md`
   - 先锁定退出码 `0 / 1 / 127` 的边界。
6. `docs/ietf/notes/9368.md`
   - 先锁定 `version_information`、Original/Chosen/Negotiated Version、compatible negotiation 与 downgrade prevention。
7. `docs/ietf/txt/rfc9368.txt`
   - 对 `version_information`、Original/Chosen/Negotiated Version、compatible negotiation 与 downgrade prevention 做权威核对。
8. `docs/ietf/notes/9369.md`
   - 再锁定 v2 相对 v1 的最小差异：wire version、long header type bits、Initial salt、HKDF labels、Retry integrity。
9. `docs/ietf/txt/rfc9369.txt`
   - 对 v2 相对 v1 的最小差异：wire version、long header type bits、Initial salt、HKDF labels、Retry integrity 做权威核对。
10. `docs/ietf/notes/9000.md`
   - 核对 VN、transport parameters、Retry/ODCID/SCID 认证边界。
11. `docs/ietf/notes/9001.md`
   - 核对 Initial key derivation、label/salt 与 TLS/QUIC 边界。
12. `docs/ietf/txt/rfc9000.txt`
    - 对 transport parameters、Version Negotiation、Retry、connection ID 认证做权威核对。
13. `docs/ietf/txt/rfc9001.txt`
    - 对 Initial keys、header protection、Retry integrity、HKDF labels 做权威核对。
14. `docs/refs/xquic.md`
    - 对照“版本依赖项集中管理”的边界设计。
15. `docs/refs/lsquic.md`
    - 对照 version dispatch table / parse-build vtable 的拆分方式。
16. `docs/refs/mvfst.md`
    - 对照 `quic_version_ops` 风格的版本表驱动思路。
17. 当前仓库 v2 相关代码
    - `ai-quic/include/ai_quic/version.h`
    - `ai-quic/include/ai_quic/packet.h`
    - `ai-quic/src/transport/dispatcher.c`
    - `ai-quic/src/transport/version_negotiation.c`
    - `ai-quic/src/transport/packet_codec.c`
    - `ai-quic/src/transport/transport_params.c`
    - `ai-quic/src/transport/conn.c`
    - `ai-quic/src/transport/conn_io.c`
    - `ai-quic/tests/interop/test_versionnegotiation.c`
    - `ai-quic/tests/unittest/test_unit.c`
18. `docs/plans/step03-v2-logic-design.md`
    - 在编码前审查步骤03的行为级实现规格。

## 主题目录

| 主题 | 首选文档 | 本步要点 |
|:--|:--|:--|
| 阶段目标 | `docs/plans/plan-quic.md` | 步骤03是 `v2`，目标是 compatible negotiation 与 v2 兼容。 |
| 目录与代码落点 | `docs/plans/repo-file-hierarchy.md` | 明确 version/codec/tls/test 的目录边界。 |
| testcase 语义 | `docs/quic-interop-runner/quic-test-cases.md` | `v2` 关注 v1 首飞、v2 协商、小文件下载。 |
| runner 契约 | `docs/quic-interop-runner/how-to-run.md` | 服务端读 `/www`，客户端写 `/downloads`。 |
| 退出码 | `docs/quic-interop-runner/implement-requirements.md` | 成功 `0`，错误 `1`，未支持 `127`。 |
| 兼容协商 | `RFC 9368` | `version_information`、compatible negotiation、downgrade prevention。 |
| v2 差异 | `RFC 9369` | 版本号、long header type bits、Initial salt、HKDF labels、Retry integrity。 |
| 版本相关 TLS/crypto | `docs/ietf/notes/9001.md` + `docs/ietf/txt/rfc9001.txt` | Initial key derivation、header protection、Retry tag。 |
| 参考实现：xquic | `docs/refs/xquic.md` | 版本依赖项集中管理，不让高层 transport 感知版本分支。 |
| 参考实现：lsquic | `docs/refs/lsquic.md` | version dispatch table 与 codec vtable。 |
| 参考实现：mvfst | `docs/refs/mvfst.md` | `quic_version_ops` 风格的运行时版本表。 |
| 当前代码映射 | 当前仓库 v2 相关代码 | 现有 `v1-only` 锚点与后续抽象边界。 |
| 逻辑设计 | `docs/plans/step03-v2-logic-design.md` | 统一的步骤03实现规格。 |

## 用户主入口：根目录 Makefile

步骤03沿用步骤01/02的入口约束，并新增 v2 相关主入口：

- 仓库根目录必须维护一个 `Makefile`。
- 所有高频构建、运行、排障命令优先通过 `make` 暴露。
- `Makefile` 是用户主入口；`python3 run.py ...` 与底层 interop 脚本只作为实现细节。

步骤03至少应预留以下目标：

- `make build`
- `make test-unit`
- `make test-integration`
- `make test-interop-handshake`
- `make test-interop-transfer`
- `make test-interop-v2`
- `make interop-v2-server`
- `make interop-v2-client`
- `make interop-versionnegotiation`
- `make interop-logs CASE=v2`

如果这些目标尚未落地，表示步骤03的用户入口仍未交付完整。

## 步骤03最小实现清单

- 在 `dispatcher` 层区分：
  - `Acceptable Versions`
  - `Offered Versions`
  - 单机阶段可等价，但接口不得写死成“永远只有一个支持版本”。
- `dispatcher` 必须作为版本代理入口：
  - 负责决定 datagram 走 v1 还是 v2 的 `version_ops`
  - 负责把版本差异收敛在连接外或连接入口
  - 负责把连接继续路由到同一套 `connection/recovery/stream` 主逻辑
- 为连接显式建模：
  - `original_version`
  - `negotiated_version`
  - 当前 long header 使用版本
  - 本端/对端 `version_information`
- 客户端能以 v1 发起，并在 `version_information` 中携带 `[v1, v2]` 风格的可用版本列表。
- 服务端能在解析客户端首飞后：
  - 选择 v2 作为 Negotiated Version
  - 在同一连接内切换到 v2 路径
  - 不复制新的 `connection/recovery/stream` 主逻辑
- `version_information` transport parameter 必须能：
  - 编码
  - 解码
  - 鉴权后校验
  - 在缺失/格式错误/降级场景下触发版本协商错误
- 版本相关差异必须集中管理：
  - wire version
  - long header type mapping
  - Initial salt
  - HKDF labels
  - Retry integrity secret/key/nonce
- `packet codec` 与 `Initial packet protection` 必须基于版本表，而不是写死 v1 常量。
- Retry 与 token/ticket 必须带版本边界：
  - Retry 使用 Original Version
  - ticket/token 不得跨版本误用
- `v1` 的 `handshake` / `transfer` 路径必须继续稳定通过。
- 未知版本仍走步骤00的无状态 Version Negotiation。
- 日志与 qlog 至少能输出：
  - Original/Negotiated Version
  - 本端/对端 Available Versions
  - 版本切换时点
  - Initial secret 所属版本
  - `version_information` 的收发与校验结果

## 关键约束速查

### 步骤03：v2 兼容与协商

- v2 不是第二套 transport 状态机；它应复用步骤01/02已有的连接、恢复、流控与多流逻辑。
- 版本分流必须只发生在 `dispatcher` 代理入口和 `version_ops` 边界；进入连接主逻辑后，不应再按“这是 v1 还是 v2 连接”分裂出两套流程。
- 支持 v2 的端点必须发送、处理并校验 `version_information` transport parameter。
- `version_information` 的 transport parameter codepoint 是 `0x11`；版本协商错误码也是 `0x11`。
- QUIC v2 的 wire version 固定为 `0x6b3343cf`。
- QUIC v2 的 long header type bits 与 v1 不同：
  - `Initial=0b01`
  - `0-RTT=0b10`
  - `Handshake=0b11`
  - `Retry=0b00`
- QUIC v2 的 Initial salt、HKDF labels、Retry integrity secret/key/nonce 都不同于 v1。
- 不要把版本判断散落到 `stream / loss / flow control / scheduler` 高层逻辑中。
- 绝对不要通过复制 `conn_io.c`、`loss.c`、`stream.c` 等高层文件来重写一套 v2 路径。
- compatible negotiation 期间：
  - Retry 必须使用 Original Version
  - 客户端收到不同版本的首个 server long header 后，学习到 Negotiated Version
  - 双端后续 `Handshake` 与 `1-RTT` 必须只使用 Negotiated Version
- 票据和 token 必须带版本边界，不得跨版本直接复用。

## 验收口径

### 与 runner 语义对齐的 `v2`

与 `quic-interop-runner` 对齐后，成功至少要满足：

- 客户端使用 QUIC v1 发起连接。
- 客户端在其认证后的版本信息中携带 QUIC v2。
- 服务端正确选择 v2 作为 Negotiated Version。
- 连接在同一逻辑连接内切换并完成 v2 握手。
- 客户端最终在 QUIC v2 上成功下载小文件，且落盘文件与服务端源文件内容完全一致。

这意味着：

- “代码里定义了 `AI_QUIC_VERSION_V2`” 还不够。
- “收到 VN 包或切换了版本号，但没有完成小文件下载” 不算通过。
- “v2 通过了，但 v1 的 `handshake/transfer` 回退” 也不算通过。

### 步骤03完成判定

只有同时满足以下四项，才算步骤03完成：

- 步骤01的 `handshake` 能力仍保持通过。
- 步骤02的 `transfer` 能力仍保持通过。
- 步骤03的 `v2` 能力可在 compatible negotiation 路径上验证通过。
- 未知版本的无状态 VN 路径未回退。

## 建议的排障顺序

固定按以下顺序排查：

1. `runner-output.txt`
   - 先区分是环境/容器问题，还是协商语义检查失败。
2. `server/` 与 `client/` 日志
   - 再定位卡在 `version_information`、Initial key、版本切换、Handshake 还是下载阶段。
3. `sim/` 抓包
   - 如果日志不足，再判断 Original Version、Negotiated Version、Retry 与 long header type 是否符合预期。
4. `qlog` / `keylog`
   - 如果仍不足，再看 key schedule、packet 版本切换与加密级别时序。

## 建议的验证证据目录

- `artifacts/interop/<timestamp>-<server>-<client>-v2/runner-output.txt`
- `artifacts/interop/<timestamp>-<server>-<client>-v2/runner/server/`
- `artifacts/interop/<timestamp>-<server>-<client>-v2/runner/client/`
- `artifacts/interop/<timestamp>-<server>-<client>-v2/runner/sim/`
- `QLOGDIR` 输出目录

## 推荐操作方式

- 规划步骤03时，先读本文件，再按“建议阅读顺序”补齐原始文档。
- 需要决定代码落点时，先读 `docs/plans/repo-file-hierarchy.md`。
- 运行步骤03时，优先从仓库根目录执行 `make` 目标。
- 原始长命令只放在附录作为 `Makefile` 的底层实现说明，不作为主入口。
- 若后续重命名本文件或 `docs/plans/step03-v2-logic-design.md`，再同步更新 `docs/menu.md`。

## 附录：Make 目标与底层命令的映射约束

以下长命令不应作为用户主入口，而应封装进根目录 `Makefile`：

- `make test-interop-v2`
  - 底层实现可调用双方方向的 `v2` testcase。
- `make interop-v2-server`
  - 底层实现可调用“我方 server + 对端 client”的 `v2` 组合。
- `make interop-v2-client`
  - 底层实现可调用“对端 server + 我方 client”的 `v2` 组合。
- `make interop-logs CASE=v2`
  - 底层实现可统一列出或打开对应 v2 日志目录，并按 `runner-output -> server/client -> sim -> qlog` 顺序展示排障入口。

文档中优先写 `make` 目标；如果需要解释底层实现，再补原始命令。
