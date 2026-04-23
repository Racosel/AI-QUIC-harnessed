# 步骤04 文档目录：密码套件约束能力（ChaCha20）

更新于 `2026-04-22`

## 目标

本文档是步骤04的专项入口，用于指导后续实现与排障围绕以下最小闭环推进：

- 对齐 `TESTCASE="chacha20"` 的 runner 语义：客户端和服务器只提供 ChaCha20 cipher suite，然后完成小文件下载。
- 验证 TLS cipher suite policy 可以独立切换，而 `connection / recovery / stream / flow control` 主逻辑保持不变。
- 将 cipher suite 选择、AEAD、header protection、HKDF hash 与 secret 安装收敛到 TLS/crypto/packet protection 边界。
- 保持步骤01/02/03已经通过的 `handshake`、`transfer` 与 `v2` 路径不回退。

本地完成标准按更严格口径执行：

- `chacha20` 不是新的 transport 行为；它只改变 TLS 协商出的 Handshake/1-RTT packet protection 套件。
- Initial packet protection 仍由 QUIC version salt 与客户端首个 DCID 派生，并使用 AES-128-GCM，不随 TLS 协商结果改变。
- Header protection 与 payload AEAD 必须作为两个独立对象建模，不能把 ChaCha20 写成散落的 `if (chacha20)`。
- 步骤04能力完成后，应能单独验证 ChaCha20-only 握手与下载，并回归普通 AES 路径。

说明：

- `docs/plans/plan-quic.md` 已将步骤04定义为“密码套件约束能力”。
- 本文档只定义步骤04的专项阅读顺序、完成标准与排障入口，不改写主计划的阶段排序。
- 当前代码中 `packet_codec` 已有 AES/ChaCha cipher suite 选择雏形，但 endpoint/TLS config 尚缺显式 cipher policy，interop dispatch 也尚未开放 `chacha20`。

## 建议阅读顺序

1. `docs/plans/plan-quic.md`
   - 先对齐步骤04的阶段边界、前置依赖与验收重点。
2. `docs/plans/repo-file-hierarchy.md`
   - 先明确 TLS adapter、packet codec、interop、test 应落在哪些目录。
3. `docs/quic-interop-runner/quic-test-cases.md`
   - 先锁定 `chacha20` 的 testcase 语义：双方仅提供 ChaCha20，随后客户端下载文件。
4. `docs/quic-interop-runner/how-to-run.md`
   - 先锁定 `/www`、`/downloads`、`SSLKEYLOGFILE`、`QLOGDIR`、日志目录契约。
5. `docs/quic-interop-runner/implement-requirements.md`
   - 先锁定退出码 `0 / 1 / 127` 的边界。
6. `docs/ietf/notes/9001.md`
   - 快速核对 packet protection、AEAD usage、header protection、Initial protection 与 AEAD usage limits。
7. `docs/ietf/txt/rfc9001.txt`
   - 对 AEAD usage、ChaCha20 header protection、Initial packet protection 与 forbidden cipher suite 做权威核对。
8. `docs/ietf/notes/9000.md`
   - 补充核对 QUIC packet/header 处理边界，避免把 cipher policy 扩散到 transport 语义。
9. `docs/ietf/txt/rfc9000.txt`
   - 需要权威确认 packet/header 或错误码边界时再读。
10. `docs/refs/xquic.md`
    - 对照 engine 级与 connection 级 TLS 配置的职责拆分。
11. `docs/refs/lsquic.md`
    - 对照 `enc_sess` 风格的 TLS/crypto 边界，不让 transport 感知具体 cipher。
12. `docs/refs/mvfst.md`
    - 对照 `quic_cipher_suite_ops`、AEAD 与 packet number/header protection cipher 的拆分思路。
13. 当前仓库 Step04 相关代码
    - `ai-quic/include/ai_quic/endpoint.h`
    - `ai-quic/include/ai_quic/tls.h`
    - `ai-quic/src/tls/tls_ctx.c`
    - `ai-quic/src/tls/tls_internal.h`
    - `ai-quic/src/transport/packet_codec.c`
    - `ai-quic/interop/testcase_dispatch.sh`
    - `ai-quic/interop/run_endpoint.sh`
    - `ai-quic/demo/common/demo_cli.c`
    - `ai-quic/tests/unittest/test_unit.c`
14. `docs/plans/step04-chacha20-logic-design.md`
    - 在编码前审查步骤04的行为级实现规格。

## 主题目录

| 主题 | 首选文档 | 本步要点 |
|:--|:--|:--|
| 阶段目标 | `docs/plans/plan-quic.md` | 步骤04是 `chacha20`，目标是 TLS cipher policy 独立切换。 |
| 目录与代码落点 | `docs/plans/repo-file-hierarchy.md` | 明确 tls/transport codec/interop/test 的目录边界。 |
| testcase 语义 | `docs/quic-interop-runner/quic-test-cases.md` | `chacha20` 要求双端仅提供 ChaCha20 后完成下载。 |
| runner 契约 | `docs/quic-interop-runner/how-to-run.md` | 服务端读 `/www`，客户端写 `/downloads`，保留 keylog/qlog。 |
| 退出码 | `docs/quic-interop-runner/implement-requirements.md` | 成功 `0`，错误 `1`，未支持 `127`。 |
| Packet protection | `docs/ietf/notes/9001.md` + `docs/ietf/txt/rfc9001.txt` | Handshake/1-RTT 使用 TLS 协商出的 AEAD。 |
| Initial protection | `docs/ietf/notes/9001.md` + `docs/ietf/txt/rfc9001.txt` | Initial 仍使用版本 salt 与 AES-128-GCM，不随 ChaCha20 改变。 |
| Header protection | `docs/ietf/txt/rfc9001.txt` | AES 与 ChaCha20 的 hp algorithm、sample 与 mask 生成不同。 |
| 参考实现：xquic | `docs/refs/xquic.md` | TLS 配置职责集中，不引入新的 transport 逻辑。 |
| 参考实现：lsquic | `docs/refs/lsquic.md` | cipher 选择属于 TLS/crypto session 边界。 |
| 参考实现：mvfst | `docs/refs/mvfst.md` | `quic_cipher_suite_ops` 与独立 header cipher。 |
| 当前代码映射 | 当前仓库 Step04 相关代码 | 现有 codec 已有雏形，TLS/endpoint/interop 入口需补齐。 |
| 逻辑设计 | `docs/plans/step04-chacha20-logic-design.md` | 统一的步骤04实现规格。 |

## 用户主入口：根目录 Makefile

步骤04沿用前序步骤的入口约束：

- 仓库根目录必须维护一个 `Makefile`。
- 所有高频构建、运行、排障命令优先通过 `make` 暴露。
- `Makefile` 是用户主入口；`python3 run.py ...` 与底层 interop 脚本只作为实现细节。

步骤04实现完成后至少应预留以下目标：

- `make build`
- `make test-unit`
- `make test-integration`
- `make test-interop-handshake`
- `make test-interop-transfer`
- `make test-interop-v2`
- `make test-interop-chacha20`
- `make interop-chacha20-server`
- `make interop-chacha20-client`
- `make interop-logs CASE=chacha20`

如果这些目标尚未落地，表示步骤04的用户入口仍未交付完整。

## 步骤04最小实现清单

- 在 endpoint/demo config 中新增显式 cipher policy，而不是直接让 `testcase` 分支控制 crypto 细节。
- TLS adapter 至少支持两种策略：
  - `default`：保持普通 TLS 1.3 cipher suite 集合。
  - `chacha20-only`：仅提供 `TLS_CHACHA20_POLY1305_SHA256`。
- BoringSSL QUIC setup 时应在 TLS 1.3 cipher suites 层约束策略；TLS 版本仍固定 TLS 1.3。
- fake TLS 路径若继续用于本地测试，也必须能显式模拟 negotiated cipher suite。
- packet protection 应使用集中式 cipher suite ops 或等价表驱动结构，描述：
  - TLS cipher suite wire id
  - AEAD
  - HKDF hash
  - authentication tag length
  - nonce length
  - header protection algorithm
  - header protection key length
  - AEAD usage limits
- `Initial` packet protection 继续走 QUIC version ops：version salt、Initial labels、AES-128-GCM 与 AES header protection。
- `Handshake` 与 `1-RTT` packet protection 使用 TLS callback 安装的 secret 与 cipher suite。
- `ai-quic/interop/testcase_dispatch.sh` 必须开放 `chacha20`，未实现前继续按 runner 契约返回 `127`。
- 日志与 qlog 至少能输出：
  - cipher policy
  - negotiated cipher suite
  - 每个 encryption level 安装的 packet protection suite
  - header protection algorithm
  - chacha20-only testcase 是否生效

## 关键约束速查

### 步骤04：ChaCha20 cipher policy

- `chacha20` 不是第二套 transport 状态机；它应复用步骤01/02/03已有的连接、恢复、流控、多流与版本逻辑。
- cipher suite policy 只属于 endpoint config、TLS adapter 与 packet protection 边界。
- 不要在 `stream / loss / flow control / scheduler / dispatcher` 高层逻辑中判断 AES 或 ChaCha20。
- QUIC 可以使用 TLS 1.3 定义的 cipher suite，但不得协商 `TLS_AES_128_CCM_8_SHA256`。
- 端点不得仅因为 ClientHello 提供未知或不支持的 cipher suite 就拒绝连接。
- Handshake/1-RTT packet protection 使用 TLS 协商出的 AEAD。
- Initial packet protection 不使用 TLS 协商出的 AEAD；它由 QUIC version salt 与客户端首个 DCID 派生，步骤04不得改写这一点。
- Header protection 必须在 packet protection 之后应用；接收侧必须先移除 header protection，再恢复 packet number 并打开 AEAD。
- ChaCha20 header protection 使用 16 字节 sample，前 4 字节作为 little-endian counter，后 12 字节作为 nonce，并用 ChaCha20 生成 5 字节 mask。
- Header protection key 与 packet protection key/IV 分别派生并分别保存；后续 Key Update 不更新 header protection key。

## 验收口径

### 与 runner 语义对齐的 `chacha20`

与 `quic-interop-runner` 对齐后，成功至少要满足：

- 服务端与客户端在 `TESTCASE="chacha20"` 下都只提供 ChaCha20 cipher suite。
- TLS 最终协商到 `TLS_CHACHA20_POLY1305_SHA256`。
- Handshake 和 1-RTT packet protection 使用 ChaCha20-Poly1305 AEAD 与 ChaCha20 header protection。
- Initial packet protection 仍保持版本对应的 Initial key derivation 和 AES-128-GCM。
- 客户端最终成功下载文件，落盘内容与服务端源文件完全一致。

这意味着：

- “packet codec 里有 `EVP_aead_chacha20_poly1305()`” 还不够。
- “通过 testcase 字符串绕过 TLS 协商” 不算通过。
- “ChaCha20 通过了，但普通 handshake/transfer/v2 回退” 也不算通过。

### 步骤04完成判定

只有同时满足以下五项，才算步骤04完成：

- 步骤01的 `handshake` 路径仍可验证通过。
- 步骤02的 `transfer` 路径仍可验证通过。
- 步骤03的 `v2` 路径仍可验证通过。
- 步骤04的 `chacha20` 能力可在 runner 中验证通过。
- 单测能覆盖 cipher suite ops、ChaCha20 header protection、AEAD 错误密钥/nonce 失败路径。

## 建议的排障顺序

固定按以下顺序排查：

1. `runner-output.txt`
   - 先区分是 testcase 未开放、环境/容器问题、TLS cipher negotiation 失败，还是下载校验失败。
2. `server/` 与 `client/` 日志
   - 再定位 cipher policy 是否生效、TLS 是否协商到 ChaCha20、哪个 encryption level 安装了错误 suite。
3. `SSLKEYLOGFILE`
   - 确认 TLS secret 已导出，必要时结合 pcap 解密 Handshake/1-RTT。
4. `sim/` 抓包
   - 判断 Initial 是否仍按版本正常保护，Handshake/1-RTT 是否进入 ChaCha20 packet protection。
5. `qlog`
   - 如果日志不足，再看 packet_sent/packet_received、encryption level 与错误关闭原因。

## 建议的验证证据目录

- `artifacts/interop/<timestamp>-<server>-<client>-chacha20/runner-output.txt`
- `artifacts/interop/<timestamp>-<server>-<client>-chacha20/runner/server/`
- `artifacts/interop/<timestamp>-<server>-<client>-chacha20/runner/client/`
- `artifacts/interop/<timestamp>-<server>-<client>-chacha20/runner/sim/`
- `QLOGDIR` 输出目录
- `SSLKEYLOGFILE` 输出文件

## 推荐操作方式

- 规划步骤04时，先读本文件，再按“建议阅读顺序”补齐原始文档。
- 需要决定代码落点时，先读 `docs/plans/repo-file-hierarchy.md`。
- 运行步骤04时，优先从仓库根目录执行 `make` 目标。
- 原始长命令只放在附录作为 `Makefile` 的底层实现说明，不作为主入口。
- 若后续重命名本文件或 `docs/plans/step04-chacha20-logic-design.md`，再同步更新 `docs/menu.md`。

## 附录：Make 目标与底层命令的映射约束

以下长命令不应作为用户主入口，而应封装进根目录 `Makefile`：

- `make test-interop-chacha20`
  - 底层实现可优先跑 ai-quic 双端 smoke，再扩展为真实 runner 对端方向。
- `make interop-chacha20-server`
  - 底层实现可调用“我方 server + 对端 client”的 `chacha20` 组合。
- `make interop-chacha20-client`
  - 底层实现可调用“对端 server + 我方 client”的 `chacha20` 组合。
- `make interop-logs CASE=chacha20`
  - 底层实现可统一列出对应 chacha20 日志目录，并按 `runner-output -> server/client -> keylog -> sim -> qlog` 顺序展示排障入口。

文档中优先写 `make` 目标；如果需要解释底层实现，再补原始命令。
