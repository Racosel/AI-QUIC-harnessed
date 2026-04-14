# 步骤01 路径1代码逻辑设计

更新于 `2026-04-09 14:29:18 +0800`

## 设计目标

本设计用于支撑“路径1”：先在 `ai-quic/src/transport` 与 `ai-quic/src/tls` 落地最小握手链路，再接入 interop 验证。

步骤01只覆盖以下能力：

- 单连接握手成功（`Initial -> Handshake -> 1-RTT`）。
- 服务端不依赖 Retry。
- 握手后完成最小文件下载（与 `handshake` testcase 语义一致）。
- 失败时可通过日志、抓包、qlog 快速定位卡点。

本设计明确不在步骤01实现：

- `0-RTT`、会话恢复、`keyupdate`、迁移、多路径、`http3`、`v2`、大文件/多流优化。

## 约束来源

- 阶段目标：`docs/plans/plan-quic.md`
- 目录落点：`docs/plans/repo-file-hierarchy.md`
- 步骤01入口：`docs/plans/step01-handshake-doc-index.md`
- interop 契约：`docs/quic-interop-runner/how-to-run.md`、`docs/quic-interop-runner/implement-requirements.md`
- 协议约束：`docs/ietf/notes/9000.md`、`9001.md`、`9002.md`
- 参考实现：若后续重新引入 `xquic/` 或其他对照实现，可参考其 interop、demo、transport、tls 入口；当前工作树不应默认这些路径已存在

## 路径1模块落点设计

| 模块 | 拟新增文件（设计名） | 职责 | 参考 `xquic` |
|:--|:--|:--|:--|
| 公共类型层 | `ai-quic/include/ai_quic/ai_quic_types.h` | 连接角色、加密级别、错误码、基础结构声明 | `xqc_conn.h` / `xquic_typedef.h` |
| 可观测性层 | `ai-quic/src/common/aiq_log.h/.c` | 结构化事件日志与 qlog 占位输出（步骤01先做轻量格式） | `xqc_log*` |
| 传输核心 | `ai-quic/src/transport/aiq_conn.h/.c` | 连接对象、握手状态机、主事件循环 | `xqc_conn.*` |
| 包处理 | `ai-quic/src/transport/aiq_packet.h/.c` | Long Header / Short Header 编解码、包号空间分发 | `xqc_packet*` |
| 帧处理 | `ai-quic/src/transport/aiq_frame.h/.c` | `CRYPTO/ACK/STREAM/PADDING/PING/CONNECTION_CLOSE` 最小集 | `xqc_frame*` |
| 流管理 | `ai-quic/src/transport/aiq_stream.h/.c` | 步骤01单流收发、流状态、与 `STREAM` 帧映射 | `xqc_stream.*` |
| CRYPTO 缓冲 | `ai-quic/src/transport/aiq_crypto_stream.h/.c` | `CRYPTO` 重组、去重、重传调度 | `xqc_crypto*`（transport 侧） |
| 传输参数 | `ai-quic/src/transport/aiq_transport_params.h/.c` | 本地 TP 编码、对端 TP 解码、初始流控参数装配 | `xqc_transport_params.*` |
| 恢复与 PTO | `ai-quic/src/transport/aiq_recovery.h/.c` | 按包号空间独立 ACK/Loss/PTO | `xqc_send_ctl.*` / `xqc_timer.*` |
| TLS 适配层 | `ai-quic/src/tls/aiq_tls_if.h/.c` | TLS 生命周期、回调桥接、错误映射 | `xqc_tls.h` |
| 密钥生命周期 | `ai-quic/src/tls/aiq_key_phase.h/.c` | Initial/Handshake/1-RTT 密钥安装与丢弃 | `xqc_tls*` / `xqc_hkdf*` |
| demo 入口 | `ai-quic/demo/ai_quic_demo_client.c`、`ai-quic/demo/ai_quic_demo_server.c` | 最小 HTTP/0.9 下载闭环 | `demo_client.c` / `demo_server.c` |
| interop 包装 | `ai-quic/interop/run_endpoint.sh` | 映射 `ROLE`、`TESTCASE`、`REQUESTS`、日志与退出码 | 参考实现的 interop 入口脚本 |

说明：

- 上述是“设计落点”，当前尚未编码。
- 文件名最终可按你审查意见统一调整，但目录边界保持不变。

## 连接状态机设计（步骤01最小集）

### 客户端状态

1. `CLIENT_INIT`
2. `CLIENT_INITIAL_SENT`
3. `CLIENT_HANDSHAKE_RECV`
4. `CLIENT_HANDSHAKE_SENT`
5. `CLIENT_ESTABLISHED`
6. `CLIENT_CLOSING`
7. `CLIENT_CLOSED`

### 服务端状态

1. `SERVER_INIT`
2. `SERVER_INITIAL_RECV`
3. `SERVER_INITIAL_SENT`
4. `SERVER_HANDSHAKE_SENT`
5. `SERVER_HANDSHAKE_RECV`
6. `SERVER_ESTABLISHED`
7. `SERVER_CLOSING`
8. `SERVER_CLOSED`

### 关键迁移事件

- 客户端：
  - 创建连接后触发 TLS 初始化，产出 `ClientHello`，发送填充到 `>=1200` 的 Initial，进入 `CLIENT_INITIAL_SENT`。
  - 收到服务端 Initial/Handshake 并成功解密后，进入 `CLIENT_HANDSHAKE_RECV`。
  - 发送首个 Handshake 包后丢弃 Initial 密钥，进入 `CLIENT_HANDSHAKE_SENT`。
  - TLS 与传输层都确认握手完成后，进入 `CLIENT_ESTABLISHED`。
- 服务端：
  - 收到有效 Initial 并完成最小校验后，进入 `SERVER_INITIAL_RECV`。
  - 发送 Server Initial 后进入 `SERVER_INITIAL_SENT`。
  - 发送 Handshake 包后进入 `SERVER_HANDSHAKE_SENT`。
  - 首次成功处理客户端 Handshake 包后丢弃 Initial 密钥，进入 `SERVER_HANDSHAKE_RECV`。
  - 握手完成后发送 `HANDSHAKE_DONE`，进入 `SERVER_ESTABLISHED`。

## 包号空间与数据结构设计

按 RFC 语义拆分 3 个空间，互不混用 ACK/Loss：

- `INITIAL_SPACE`
- `HANDSHAKE_SPACE`
- `APP_SPACE`

每个空间维护最小状态：

- `next_pktno`：下一个发送包号
- `largest_acked`：已确认最大包号
- `sent_packets`：待确认发送记录（用于重传/丢包判断）
- `ack_needed`：是否需要立即发 ACK
- `crypto_tx_queue`：待发 `CRYPTO` 片段
- `crypto_rx_reassembly`：接收重组缓存
- `pto_deadline`：本空间 PTO 定时点

连接级关键状态（步骤01必须显式维护）：

- `local_scid`：本端连接 ID
- `peer_scid`：对端在包头声明的连接 ID
- `active_dcid`：本端发包使用的目标 CID（可在握手中切换）
- `odcid`：客户端初始目的 CID（用于 Initial 密钥派生）
- `stream_map`：当前已创建流对象（步骤01至少支持 1 条双向流）
- `conn_fc/stream_fc`：连接级与流级流控窗口（由 TP 初始化）

## 核心收发流程设计

### 客户端发送主线

1. `aiq_conn_client_start()` 先生成 `local_scid` 与 `odcid`，并把 `odcid` 写入 `active_dcid`。
2. 调用 `aiq_transport_params_encode_local()` 生成本地 TP 字节流，调用 `aiq_tls_set_local_transport_params()` 注入 TLS。
3. 调用 `aiq_tls_set_alpn_list()` 设置 ALPN（默认包含 `hq-interop`，可兼容 `hq-29`）。
4. 调用 `aiq_tls_init()`。
5. TLS 回调 `on_crypto_data(level=INITIAL)` 产出 `ClientHello` 字节。
6. 写入 `INITIAL_SPACE.crypto_tx_queue`。
7. `aiq_packet_build_initial()` 组包：`CRYPTO + PADDING`，确保 UDP 载荷 `>=1200`。
8. `aiq_conn_send_datagram()` 发包并登记发送记录。

### 服务端接收与回发主线

1. `aiq_conn_on_datagram()` 解析长包头并识别加密级别。
2. `aiq_tls_decrypt_header/payload()` 解密成功后进入帧分发。
3. `aiq_frame_handle_crypto()` 将数据写入 `INITIAL_SPACE.crypto_rx_reassembly`。
4. 达到连续偏移后调用 `aiq_tls_process_crypto_data()` 驱动 TLS。
5. TLS 完成对端 TP 解析后，通过 `on_peer_transport_params()` 回传传输层并初始化流控与连接参数。
6. TLS 完成 ALPN 协商后，服务端校验是否命中允许列表（步骤01允许 `hq-interop` / `hq-29`）。
7. TLS 回调产出服务端握手数据，分别写入 Initial/Handshake 发送队列。
8. 发送路径按空间取帧组包，必要时捎带 ACK。

### 客户端处理服务端握手主线

1. 客户端收到服务端 Initial，记录并校验服务端 `SCID`，将后续发包 `active_dcid` 切换为该值。
2. 推进 TLS，安装 Handshake 密钥。
3. 收到服务端 Handshake 后继续驱动 TLS，准备 1-RTT，并提取对端 TP 初始化流控窗口。
4. 发送首个客户端 Handshake 包后，触发 Initial 密钥丢弃。
5. 进入 `CLIENT_ESTABLISHED` 后允许最小应用数据收发。

## TLS 对接接口设计

`aiq_tls_if` 设计为“传输层可控、TLS 黑盒”接口，最小回调集如下：

- `on_crypto_data(level, data, len)`：TLS 产出待发送握手字节。
- `on_key_ready(level, key_type)`：某级别读/写密钥就绪。
- `on_peer_transport_params(tp_bytes, tp_len)`：TLS 解析到对端 TP 后回传传输层。
- `on_alpn_selected(alpn, alpn_len)`：回传协商后的 ALPN。
- `on_handshake_complete()`：TLS 视角握手完成。
- `on_alert(alert_code)`：TLS 告警转 QUIC 连接错误。

传输层对 TLS 的调用：

- `aiq_tls_init(conn, version, odcid)`
- `aiq_tls_set_local_transport_params(conn, tp_bytes, tp_len)`
- `aiq_tls_set_alpn_list(conn, alpn_list, alpn_count)`
- `aiq_tls_process_crypto_data(conn, level, data, len)`
- `aiq_tls_is_key_ready(conn, level, key_type)`
- `aiq_tls_discard_keys(conn, level)`

TP 装配规则（步骤01最小集）：

- 客户端在发首个 Initial 之前完成本地 TP 注入。
- 服务端在发首个握手响应前完成本地 TP 注入。
- 对端 TP 解析成功后，立即初始化连接级/流级流控窗口，避免 `STREAM` 数据路径无有效窗口。

ALPN 规则（步骤01最小集）：

- 客户端 `ClientHello` 必须携带 ALPN 扩展。
- 默认优先 `hq-interop`，并允许 `hq-29` 作为兼容项。
- 服务端只接受步骤01允许列表中的 ALPN；不匹配时走连接关闭路径并记录原因。

## ACK / Loss / PTO 设计

- Initial 与 Handshake 空间收到可确认包后立即置 `ack_needed=1`，发送时不延迟 ACK。
- `max_ack_delay` 在握手前按 0 处理。
- Loss/PTO 按包号空间独立计算，不跨空间借用样本。
- `CRYPTO` 发送记录进入可靠重传队列；丢包后必须在原加密级别重传。
- `APP_SPACE` 在握手未完成前不驱动应用层 PTO。

## STREAM 帧与最小应用数据设计

- 步骤01的最小帧集必须包含 `STREAM`，用于承载 HTTP/0.9 请求与响应数据。
- `aiq_frame` 需要支持 `STREAM` 的基础字段：`stream_id`、`offset`、`length`、`fin`。
- `aiq_stream` 负责把应用层读写请求切分为 `STREAM` 帧，并处理乱序重组。
- `STREAM` 数据进入发送记录后，丢包重传仍走原包号空间（1-RTT）。
- 流控检查顺序：先检查连接级窗口，再检查流级窗口，窗口不足时阻塞发送并打点。

## 反放大与地址验证设计

- 服务端维护 `bytes_recv` 与 `bytes_sent`。
- 在地址未验证前满足：`bytes_sent <= bytes_recv * 3`。
- 一旦地址验证完成（收到可验证握手进展包），解除放大量限制。
- 触及放大上限时，不丢状态，保留待发队列，等待后续来包解锁。

## CID 路由与切换设计

- 客户端建连时：
  - 生成本端 `SCID_C`。
  - 生成初始目标 `DCID_0`（同时作为 `odcid`）。
  - 首个 Initial 使用 `(SCID=SCID_C, DCID=DCID_0)`。
- 服务端收包时：
  - 从客户端 Initial 提取并保存 `DCID_0` 与 `SCID_C`。
  - 生成服务端 `SCID_S`，响应 Initial/Handshake 使用 `(SCID=SCID_S, DCID=SCID_C)`。
  - 建立 CID 路由映射：能根据接收包里的目的 CID 找到连接对象。
- 客户端收到服务端首个 Initial 后：
  - 提取 `SCID_S` 作为后续发包目标 CID。
  - 将 `active_dcid` 从 `DCID_0` 切换为 `SCID_S`。
  - 切换后客户端 Handshake/1-RTT 发包都使用新的 `active_dcid`。
- 校验与异常：
  - 对无法匹配连接的目标 CID，按无状态路径丢弃并记录日志。
  - 对明显异常的 CID 变更（与现有连接上下文冲突），关闭连接并记录原因。

## 最小下载链路设计（仅 handshake 所需）

- 应用协议限定为 HTTP/0.9 最小请求响应语义。
- 客户端在握手可发 1-RTT 后，通过单条双向流发送请求行（`STREAM` 帧承载，来自 `REQUESTS`）。
- 服务端从 `/www` 读取目标文件内容，通过同一请求流回写响应体（`STREAM` 帧承载）。
- 客户端写入 `/downloads`，由 runner 负责文件一致性校验。

## interop 接入设计

`ai-quic/interop/run_endpoint.sh` 与 `xquic` 的对齐原则：

- 仅放通步骤01需要的 testcase：`handshake`。
- 其余 testcase 返回 `127`（明确“不支持”而非“失败”）。
- 角色分支：
  - `ROLE=server`：启动 `ai_quic_demo_server`，监听 `443`，读取 `/www`。
  - `ROLE=client`：启动 `ai_quic_demo_client`，写入 `/downloads`。
- 保留 `SSLKEYLOGFILE`、日志目录输出，保证排障证据链完整。

兼容性注意点（来自近期互操作排障经验）：

- 收到 `version=0x57414954` 的 WAIT 探测包时，需要无状态响应（返回 `VERSION_NEGOTIATION`）并跳过连接状态推进，避免污染真实握手路径。

## 可观测性与 qlog 占位设计

- 步骤01暂不强制实现完整 qlog JSON 事件模型，但必须预留统一观测接口 `aiq_log_emit_event(...)`。
- `aiq_log` 输出采用结构化键值格式（可输出到标准错误流或日志文件），字段至少包含：
  - `ts`、`conn_id`、`role`、`pn_space`、`event`、`state_from`、`state_to`、`detail`
- 必打点事件：
  - 首包发送、首包接收、密钥就绪、密钥丢弃、ALPN 结果、TP 解析、`STREAM` 首字节收发、`HANDSHAKE_DONE` 发送/接收、放大限制阻塞/解锁。
- 后续阶段可基于该结构化日志脚本化转换为标准 qlog，避免步骤01因完整 qlog 实现阻塞。

## 实施切片（编码前设计）

1. 切片A：连接与包号空间骨架  
完成连接对象、状态机、3 个包号空间基础结构、CID 路由与主收发循环占位。
2. 切片B：Initial/Handshake 的 `CRYPTO + ACK` 闭环  
完成 TLS 回调桥接、TP 注入与提取、ALPN 协商、`CRYPTO` 重组与可靠重传、握手状态推进。
3. 切片C：密钥生命周期与握手完成语义  
补齐 Initial 密钥丢弃、1-RTT 切换、服务端 `HANDSHAKE_DONE`、结构化观测打点。
4. 切片D：demo + interop 封装  
完成 `STREAM` 最小下载路径与 `run_endpoint.sh`，跑 `handshake` 互操作验证。

## 风险与观测点

- 风险1：Initial 未填充到 1200 导致服务端静默丢包。  
观测点：客户端发包长度日志、pcap 首包长度。
- 风险2：包号空间混用导致 ACK/Loss 异常。  
观测点：按空间打印 `largest_acked` 与 PTO 触发原因。
- 风险3：密钥安装/丢弃时序错误导致“能收不能解”。  
观测点：`on_key_ready` 与 `discard_keys` 时序日志。
- 风险4：反放大限制触发后未正确解锁。  
观测点：服务端 `bytes_recv/bytes_sent` 与阻塞原因日志。
- 风险5：TP 注入/解析缺失导致握手无法建立或流控窗口为零。  
观测点：本地 TP 注入日志、对端 TP 回调日志、初始窗口值日志。
- 风险6：ALPN 未协商成功导致 interop 直接失败。  
观测点：`ClientHello` ALPN 列表、服务端选择结果、关闭原因码。
- 风险7：CID 切换时机错误导致后续包路由失败。  
观测点：`active_dcid` 切换日志、CID 路由命中率、无状态丢包日志。

## 决策结论

### 决策1：首个跨实现验证角色顺序

- 已选方案A：先做 `ai-quic` 作为 server，对接 `xquic` client。

### 决策2：步骤01导出的公共 API 粒度

- 已选方案A：步骤01仅导出最小稳定入口，调试接口留在内部头文件。

### 决策3：包解析实现策略

- 已选方案A：先做步骤01必需最小集（Long Header + Short Header 基本发送，帧仅 `CRYPTO/ACK/STREAM/PADDING/PING/CLOSE`）。

---

当前结论：以上内容仍作为步骤01的设计目标与切片划分；当前工作树未保留对应落地文件，不应把下文清单解读为“已完成实现”。

## 基于当前代码复核的仍缺失 Checklist

说明：

- 下列清单按“严格对照 step01 设计目标与模块落点”整理。
- 第一组可直接归入切片D。
- 第二组是当前代码相对 step01 原文仍存在的基础能力缺口，不建议简单并入切片D。

### 一、明确属于切片D的剩余项

切片D只承接“demo + interop 封装”，不新增协议能力目标；恢复实现时，应先确认 A/B/C 的真实代码状态，再继续 D。

- [ ] 交付物D1：实现 `ai-quic/demo/ai_quic_demo_server.c`。  
  验收口径：可监听 `443`，可从 `/www` 读取目标文件，可调用现有连接骨架完成最小收发循环。
- [ ] 交付物D2：实现 `ai-quic/demo/ai_quic_demo_client.c`。  
  验收口径：可读取 `REQUESTS`，可在握手可发后发出请求，并将下载结果写入 `/downloads`。
- [ ] 交付物D3：打通最小 `HTTP/0.9` 单流下载链路。  
  验收口径：客户端发送请求行后，服务端通过同一请求流返回文件内容；客户端落盘文件可用于 runner 校验。
- [ ] 交付物D4：实现 `ai-quic/interop/run_endpoint.sh` 的 runner 对接。  
  验收口径：正确映射 `ROLE`、`TESTCASE`、`REQUESTS`、日志目录、`SSLKEYLOGFILE`、退出码。
- [ ] 交付物D5：实现 testcase 选择门控。  
  验收口径：仅放通 `handshake`；其他 testcase 明确返回 `127`（不支持），不能误报失败。
- [ ] 交付物D6：补齐切片D最小验证脚本/命令。  
  验收口径：至少完成一次真实 `handshake` 烟雾运行，覆盖启动、下载、退出码三项检查，并记录执行命令与结果。
- [ ] 交付物D7：补齐失败证据链保全。  
  验收口径：失败时至少保留 `output.txt`、server/client 日志、`SSLKEYLOGFILE`，并在文档中固定排障读取顺序。

补充说明：

- 当前工作树未保留 `ai-quic/interop/smoke_handshake.sh`；如需切片D烟雾入口，应在恢复 `ai-quic/interop/` 时一并补齐。

切片D完成判定（本节统一口径）：

- [ ] D1~D7 全部打勾。
- [ ] `handshake` 互操作可重复通过（同机连续两次）且输出一致。
- [ ] 相关状态文档已同步更新，且引用的文件在当前工作树中真实存在。

### 二、严格按 step01 验收时仍缺失、且不应直接并入切片D的项

- [ ] 将当前基于 `aiq_conn_event_t` 的内存事件骨架补成真实包收发路径，而不只是 `RX_DATAGRAM/TX_WAKEUP/RX_ACK` 占位推进。
- [ ] 实现真实的包处理与帧处理最小集，至少覆盖设计中要求的 `CRYPTO/ACK/STREAM/PADDING/PING/CONNECTION_CLOSE`。
- [ ] 将当前 `stream_map` 占位补成真实单流收发能力，包括 `STREAM` 切分、乱序重组、1-RTT 重传与最小流控检查。
- [ ] 将当前 `aiq_tls_if` 的阶段模拟逻辑替换为真实 TLS 回调桥接，而不是仅靠 mock `stage` 推进握手。
- [ ] 补齐服务端反放大限制：维护 `bytes_recv` / `bytes_sent`，在地址未验证前满足 `bytes_sent <= bytes_recv * 3`，并在握手进展后解除限制。
- [ ] 补齐 `version=0x57414954` 的 WAIT 探测包无状态响应，确保可通过 QNS `WAITFORSERVER` 且不污染真实连接状态。
- [ ] 补齐设计中要求的关键观测点，至少包括 `STREAM` 首字节收发、放大限制阻塞/解锁。
- [ ] 若要求严格遵守模块落点，还需补齐 `aiq_packet.*`、`aiq_frame.*`、`aiq_stream.*`、`aiq_crypto_stream.*`、`aiq_recovery.*`、`aiq_key_phase.*` 的实现或完成等价拆分。

### 三、验收口径建议

- [ ] 若团队接受“当前 A/B/C 为可编译单测骨架，剩余主线进入 D”的宽松口径，则优先完成上面的切片D清单。
- [ ] 若团队要求“step01 达到设计目标中的真实最小握手 + 下载 + interop 能力”，则需要同时完成第一组与第二组，才能判定 step01 真正完成。
