# 步骤00+01 文档目录：Version Negotiation + Handshake

更新于 `2026-04-14`

## 目标

本文档是步骤00与步骤01的联合入口，用于指导后续实现与排障围绕以下最小闭环推进：

- 步骤00：未知版本首包触发无状态 Version Negotiation（VN）回复。
- 步骤01：单连接完成最小 QUIC 握手，并在 1-RTT 后成功下载小文件。

本地完成标准按更严格口径执行：

- `step1` 完成，意味着 **步骤00和步骤01同时完成**。
- `versionnegotiation` 与 `handshake` 共用同一套 `dispatcher / invariant parsing / 连接入口` 骨架。
- `handshake` testcase 仍要求“无 Retry、单连接、小文件下载”；但本地阶段完成判定额外要求同时具备步骤00的无状态 VN 路径。

说明：

- `docs/plans/plan-quic.md` 仍保留“步骤00禁用占位”的主线表述。
- 本文档只定义步骤00+01的更严格本地完成标准，不改写主计划的阶段排序。

## 建议阅读顺序

1. `docs/plans/plan-quic.md`
   - 先对齐步骤00与步骤01在主线中的位置、边界和验收重点。
2. `docs/plans/repo-file-hierarchy.md`
   - 先明确代码、demo、interop 包装与测试应落在哪些目录。
3. `docs/quic-interop-runner/quic-test-cases.md`
   - 先锁定 `versionnegotiation` 与 `handshake` 的 testcase 语义。
4. `docs/quic-interop-runner/how-to-run.md`
   - 先锁定 `/www`、`/downloads`、`/certs`、`REQUESTS`、`SSLKEYLOGFILE`、`QLOGDIR` 与日志目录契约。
5. `docs/quic-interop-runner/implement-requirements.md`
   - 先锁定退出码 `0 / 1 / 127` 的边界。
6. `docs/ietf/notes/9000.md`
   - 核对 VN、Initial 最小 1200 字节、CID 认证、ACK 规则、`HANDSHAKE_DONE`、反放大限制。
7. `docs/ietf/notes/9001.md`
   - 核对 `CRYPTO`、TLS 集成、密钥安装/丢弃、握手完成时序。
8. `docs/ietf/notes/9002.md`
   - 核对握手阶段 ACK、PTO、丢弃密钥后的状态清理、地址验证配合。
9. `docs/plans/step01-handshake-logic-design.md`
   - 在编码前审查步骤00+01的行为级实现规格。

## 主题目录

| 主题 | 首选文档 | 本步要点 |
|:--|:--|:--|
| 阶段目标 | `docs/plans/plan-quic.md` | 本地完成标准按 `step0 + step1` 联合执行。 |
| 目录与代码落点 | `docs/plans/repo-file-hierarchy.md` | 明确新代码、demo、interop 入口和测试目录边界。 |
| testcase 语义 | `docs/quic-interop-runner/quic-test-cases.md` | `versionnegotiation` 看无状态 VN；`handshake` 看单连接、小文件、无 Retry。 |
| runner 契约 | `docs/quic-interop-runner/how-to-run.md` | 服务端读 `/www`，客户端写 `/downloads`，证书来自 `/certs`。 |
| 退出码 | `docs/quic-interop-runner/implement-requirements.md` | 成功 `0`，错误 `1`，未支持 `127`。 |
| 传输层约束 | `docs/ietf/notes/9000.md` | VN 无状态、Initial 至少 1200 字节、CID/TP 校验。 |
| TLS 集成 | `docs/ietf/notes/9001.md` | `CRYPTO` 可靠交付、同级别重传、密钥安装与丢弃。 |
| 恢复与 PTO | `docs/ietf/notes/9002.md` | Initial/Handshake 立即 ACK，`max_ack_delay=0`，握手前不启用 AppData PTO。 |
| 逻辑设计 | `docs/plans/step01-handshake-logic-design.md` | 统一的步骤00+01实现规格。 |

## 用户主入口：根目录 Makefile

步骤00+01 从这次开始统一要求：

- 仓库根目录必须维护一个 `Makefile`。
- 所有高频构建、运行、排障命令优先通过 `make` 暴露。
- `Makefile` 是用户主入口；`ai-quic/interop/*.sh`、`python3 run.py ...` 等只作为底层实现细节。

步骤00+01至少应预留以下目标：

- `make build`
- `make interop-versionnegotiation`
- `make interop-handshake-smoke`
- `make interop-handshake-server`
- `make interop-handshake-client`
- `make interop-logs CASE=<testcase>`

如果这些目标尚未落地，表示步骤00+01的用户入口仍未交付完整。

## 步骤00+01最小实现清单

- 服务端能对未知版本首包执行无状态 VN 回复，不创建真实连接对象。
- 客户端能发送填充后的 Initial，且首个 Initial 的 UDP 有效载荷至少 1200 字节。
- 服务端能接收 Initial，启动 TLS，回发 Initial/Handshake，并允许最小 coalesced datagram 路径。
- 双端按 `Initial / Handshake / AppData` 三个 packet number space 独立维护 ACK、丢包恢复与密钥生命周期。
- `CRYPTO` 数据能按原始加密级别可靠重传，且乱序缓冲不阻塞 TLS 正常推进。
- 双端能在正确时机安装新密钥并丢弃旧密钥。
- 服务端握手完成后发送 `HANDSHAKE_DONE`。
- 双端能在单条双向流上完成最小 HTTP/0.9 文件下载。
- 失败时能从 runner 日志、端点日志、pcap、qlog、keylog 快速定位卡点。

## 关键约束速查

### 步骤00：Version Negotiation

- VN 必须走 `dispatcher` 层无状态路径。
- 不支持的版本若满足最小首包长度要求，应返回 VN；长度不足可直接丢弃。
- VN 不得创建真实连接状态。
- Short Header 报文不触发 VN。
- VN 包不得包含客户端已提议的版本。

### 步骤01：最小握手闭环

- `handshake` testcase 成功路径中，服务端不允许发送 Retry。
- Initial 与 Handshake 包必须立即 ACK。
- `CRYPTO` 数据必须可靠重传，且必须使用原始加密级别重传。
- 地址验证完成前，服务端受 3 倍反放大限制。
- 客户端首次发送 Handshake 包后丢弃 Initial 密钥。
- 服务端首次成功处理 Handshake 包后丢弃 Initial 密钥。
- `HANDSHAKE_DONE` 只能由服务端在握手完成后发送。
- 握手确认前不得为 AppData 空间设置 PTO。

## 联合验收口径

### 步骤00：`versionnegotiation`

本地完成时至少要满足：

- 客户端使用不支持的版本发起连接。
- 服务端返回 Version Negotiation 包。
- VN 包满足基本线缆约束：
  - `Version = 0x00000000`
  - 回显并交换收到的 DCID / SCID
  - 版本列表不包含客户端当前已提议版本
- 客户端在收到 VN 后中止当前连接尝试。
- 无真实连接对象被创建或推进到握手状态。

与 runner 语义对齐时，`versionnegotiation` 的成功关键证据是：

- 客户端 trace 中存在与其首个 Initial 的 DCID 匹配的 VN 包 `SCID`。

说明：

- `quic-interop-runner` 当前主线仍将该 case 标记为 disabled。
- 因此，步骤00的完成可通过本地自检、pcap 验证、单测或等价 runner 语义验证来判定，但能力本身必须实现。

### 步骤01：`handshake`

与 `quic-interop-runner/testcases_quic.py` 对齐后，成功至少要满足：

- 版本检查通过，且当前仅使用 QUIC v1。
- 下载文件成功，且客户端落盘文件与服务端源文件内容完全一致。
- 服务端没有发送 Retry。
- 抓包中恰好只有 1 次握手。

这意味着：

- “客户端和服务端都打印了 handshake finished” 还不够。
- “握手成功但文件没写到 `/downloads`” 不算通过。
- “握手成功但出现 Retry” 在步骤01里仍算失败。

### 联合完成判定

只有同时满足以下两项，才算步骤00+01完成：

- 步骤00的无状态 VN 路径可验证通过。
- 步骤01的最小握手与单流下载路径可验证通过。

## 建议的排障顺序

固定按以下顺序排查：

1. `output.txt`
   - 先区分是容器启动/退出问题，还是 testcase 语义检查失败。
2. `server/` 与 `client/` 日志
   - 再定位卡在 Initial、Handshake、1-RTT 还是下载阶段。
3. `sim/` 抓包
   - 如果日志不足，再判断真实发包、回包、VN、握手次数与是否出现 Retry。
4. `qlog` / `keylog`
   - 如果仍不足，再看加密级别、密钥安装、旧 key 丢弃和握手完成时序。

## 建议的验证证据目录

- `logs/<server>_<client>/versionnegotiation/`
- `logs/<server>_<client>/handshake/output.txt`
- `logs/<server>_<client>/handshake/server/`
- `logs/<server>_<client>/handshake/client/`
- `logs/<server>_<client>/handshake/sim/`
- `QLOGDIR` 输出目录

## 推荐操作方式

- 规划步骤00+01时，先读本文件，再按“建议阅读顺序”补齐原始文档。
- 需要决定代码落点时，先读 `docs/plans/repo-file-hierarchy.md`。
- 运行步骤00+01时，优先从仓库根目录执行 `make` 目标。
- 原始长命令只放在附录作为 `Makefile` 的底层实现说明，不作为主入口。
- 若后续重命名本文件或 `docs/plans/step01-handshake-logic-design.md`，再同步更新 `docs/menu.md`。

## 附录：Make 目标与底层命令的映射约束

以下长命令不应作为用户主入口，而应封装进根目录 `Makefile`：

- `make interop-versionnegotiation`
  - 底层实现可调用 `quic-interop-runner` 的 `run.py` 或本地自检脚本，对未知版本首包与 VN 报文进行验证。
- `make interop-handshake-smoke`
  - 底层实现可调用 `python3 run.py -t handshake ...` 的同实现烟雾测试。
- `make interop-handshake-server`
  - 底层实现可调用“我方 server + 对端 client”的跨实现组合。
- `make interop-handshake-client`
  - 底层实现可调用“对端 server + 我方 client”的跨实现组合。
- `make interop-logs CASE=handshake`
  - 底层实现可统一打开对应日志目录，并按 `output.txt -> server/client -> sim -> qlog` 顺序展示排障入口。

文档中优先写 `make` 目标；如果需要解释底层实现，再补原始命令。
