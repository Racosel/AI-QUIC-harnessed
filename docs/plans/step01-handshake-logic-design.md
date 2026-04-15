# 步骤00+01 逻辑设计：无状态 VN + 最小握手 + 单流下载闭环

更新于 `2026-04-14`

## 1. 范围与完成标准

本文档定义步骤00与步骤01的联合实现规格。目标不是描述当前代码状态，而是给后续实现者一份以功能正确性为主的行为级蓝图。

### 必做能力

- 步骤00：对未知版本首包执行无状态 Version Negotiation（VN）回复。
- 步骤01：完成最小 QUIC 握手闭环，即 `Initial -> Handshake -> 1-RTT`。
- 在 1-RTT 后，基于单条双向流完成最小 HTTP/0.9 文件下载。
- 与 `quic-interop-runner` 的环境变量、挂载目录、退出码、日志目录契约兼容。
- 提供最小可用的 `SSLKEYLOGFILE` 与 `QLOGDIR` 可观测性。

### 不做能力

- Retry 成功路径
- 0-RTT
- 会话恢复
- QUIC v2
- HTTP/3 / QPACK
- 路径迁移
- 多流优化
- 复杂拥塞控制算法

### 本地完成判定

步骤00+01只有在以下两项都通过时，才算完成：

- 步骤00通过 `versionnegotiation` 能力验证。
- 步骤01通过 `handshake` 能力验证。

说明：

- `docs/plans/plan-quic.md` 仍保留“步骤00禁用占位”的主线表述。
- 本文档只定义更严格的本地完成标准：**step1 done = step0 + step1 done**。

## 2. 总体架构

步骤00与步骤01共用同一套最小骨架：

- `dispatcher / endpoint`
  - 负责 UDP datagram 入口、最小不变量解析、VN 与连接分发。
- `quic_conn`
  - 负责真实连接状态机、握手推进、帧处理、应用下载路径。
- `packet number spaces`
  - 独立维护 `Initial`、`Handshake`、`AppData` 三个空间的 ACK、发送记录、PTO 与密钥生命周期。
- `tls adapter`
  - 负责 QUIC 与 TLS 的桥接，只产出/消费 `CRYPTO` 数据与密钥事件。
- `crypto reassembly`
  - 负责按加密级别重组、去重、缓存乱序 `CRYPTO` 数据。
- `minimal stream/download path`
  - 负责 1-RTT 后的单条双向流请求与文件返回。

设计原则：

- Version Negotiation 必须发生在 `dispatcher` 层，不进入连接对象。
- 握手期间的 `CRYPTO` 数据流与 1-RTT 后的应用 `STREAM` 数据流必须分离建模。
- 只保留 `engine -> connection -> stream` 的最小清晰边界，不引入多余对象分裂。
- 从步骤00+01开始，公共函数、内部函数、类型、宏与脚本函数统一使用 `ai_quic_*` 前缀。

## 3. 步骤00：Version Negotiation 设计

### 3.1 最小不变量解析

`dispatcher` 在连接创建之前，至少要解析以下字段：

- Header Form
- Version
- DCID
- SCID
- 报文是否满足最小首包长度要求

这一步只为回答三个问题：

- 这是未知版本首包吗？
- 这是应当丢弃的无效首包吗？
- 这是应进入真实连接处理的可支持版本报文吗？

### 3.2 触发条件

当服务端收到：

- Long Header 报文
- `Version` 不在当前支持集合内
- 且该首包长度满足最小可响应要求

则应生成并返回 Version Negotiation 包。

如果首包长度不足以满足最小可响应要求，可直接丢弃而不回复。

### 3.3 VN 构造规则

VN 包至少满足以下规则：

- `Version` 字段固定为 `0x00000000`
- 收到的 `SCID` 原样回显到发送报文的 `DCID`
- 收到的 `DCID` 原样回显到发送报文的 `SCID`
- 版本列表中不得包含客户端当前已经提议的版本
- 单个 UDP 数据报最多发送一个 VN 包

### 3.4 对象与状态边界

- VN 必须是 `dispatcher` 层的无状态回复。
- VN 不得创建真实连接对象。
- VN 不得推进任何握手状态。
- Short Header 报文不触发 VN。

### 3.5 客户端处理规则

客户端在本阶段需要具备以下最小行为：

- 使用不支持的版本发起首个连接尝试。
- 收到 VN 包后中止当前连接尝试。
- 不对 VN 包再发送 VN 响应。

## 4. 步骤01：最小握手设计

### 4.1 客户端首包生成

客户端发起连接时，至少满足：

- 为首个 Initial 选择不可预测的 DCID，长度至少 8 字节。
- 在收到服务器 Initial 或 Retry 之前，所有已发送数据包使用同一个目标 DCID。
- 生成本端 SCID，并在首个 Initial 中携带。
- 使用该首个 Initial 的 DCID 派生 Initial keys。
- 将首个包含 Initial 的 UDP 数据报填充到至少 1200 字节。

### 4.2 服务端最小接收与回发

服务端处理有效 Initial 时，至少满足：

- 使用客户端首个 Initial 的 DCID 作为 `original_destination_connection_id` 的认证来源。
- 启动 TLS 处理，读取并缓存 `CRYPTO` 数据。
- 在未验证地址前遵守 3 倍反放大限制。
- 回发 Initial / Handshake 数据，必要时允许放入同一个 coalesced datagram。
- 响应路径不进入 Retry 成功路径。

### 4.3 客户端处理服务端握手数据

客户端收到服务端 Initial / Handshake 后，至少满足：

- 正确切换后续发送使用的目标 DCID。
- 安装 Handshake keys。
- 继续处理服务端 Handshake 数据并推进 TLS。
- 在 1-RTT keys 就绪后允许最小应用数据路径。

### 4.4 连接状态

实现层至少应显式区分以下概念，而不是混成一个布尔值：

- `handshake completed`
- `can_send_1rtt`
- `handshake confirmed`

建议连接相位至少包含：

- `PRE_VALIDATION`
- `HANDSHAKING`
- `ACTIVE`
- `CLOSING`

### 4.5 服务端握手收尾

服务端在握手完成时必须：

- 立即发送 `HANDSHAKE_DONE`
- 进入可发送 1-RTT 数据状态
- 允许最小应用下载路径开始

## 5. 包号空间、帧与包约束

### 5.1 Packet Number Space

必须独立维护三个 packet number space：

- `Initial`
- `Handshake`
- `AppData`

每个空间至少维护：

- `next_packet_number`
- `largest_acked`
- `sent_packets`
- `ack_needed`
- `pto_deadline`
- 当前空间的 `CRYPTO` 发送与接收重组状态

### 5.2 最小包与帧能力

步骤00只需要：

- 最小不变量解析
- VN 报文构造与发送

步骤01握手期允许的最小帧集：

- `PADDING`
- `PING`
- `ACK`
- `CRYPTO`
- `CONNECTION_CLOSE`

步骤01应用数据期新增的最小帧：

- `STREAM`

### 5.3 非法帧处理

根据 RFC 9000 的握手期约束：

- Initial 和 Handshake 包中如果出现非允许帧，必须视为协议错误。
- `STREAM` 帧仅在 1-RTT 应用数据期进入成功路径。

### 5.4 Coalesced Datagram

实现必须支持最小 coalesced datagram 处理顺序：

- 同一 UDP datagram 中允许出现多个 QUIC 包。
- 握手阶段应正确处理服务端 Initial + Handshake 共存的常见路径。

## 6. CRYPTO、TLS 与密钥生命周期

### 6.1 CRYPTO 数据路径

TLS 集成遵循以下边界：

- QUIC 只通过 `CRYPTO` 帧承载 TLS 握手数据。
- TLS 不直接发送 UDP，不直接控制 packet scheduler。
- QUIC 负责：
  - 乱序缓存 `CRYPTO`
  - 按偏移重组
  - 用原始加密级别重传 `CRYPTO`
- TLS 负责：
  - 处理有序握手字节
  - 产出待发送握手字节
  - 安装各级别密钥
  - 导出 ALPN 与 transport parameters

### 6.2 乱序缓冲与重传

实现至少满足：

- `CRYPTO` 偏移在每个 packet number space 中从 0 开始。
- 支持最小可用的乱序 `CRYPTO` 缓冲，不得因轻微乱序直接卡死握手。
- 如果 QUIC 重传握手数据，必须使用该数据原始所属的加密级别重传。

### 6.3 密钥安装与丢弃时机

关键时序必须满足：

- 客户端在首次发送 Handshake 包时丢弃 Initial keys。
- 服务端在首次成功处理客户端 Handshake 包时丢弃 Initial keys。
- 握手确认后丢弃 Handshake keys。
- 一旦丢弃某级别密钥，必须同时丢弃该级别未处理旧包与恢复状态。

### 6.4 1-RTT 处理约束

- 握手完成前，任何一端都不得处理来自对端的 1-RTT 数据作为成功路径。
- 新发送数据必须使用当前可用的最高加密级别。

## 7. 传输参数与 CID 校验

### 7.1 必须校验的连接 ID 参数

步骤00+01必须把 CID 认证链建对：

- `initial_source_connection_id`
  - 必须存在并用于认证对端首个 Initial 中的 SCID。
- `original_destination_connection_id`
  - 服务端必须发送。
  - 客户端必须校验其与客户端首个 Initial 的 DCID 一致。

### 7.2 Retry 相关字段的预留

步骤01正常路径不走 Retry，但结构上必须为后续保留：

- `retry_source_connection_id`

本阶段要求：

- 正常握手路径中不使用 Retry。
- `retry_source_connection_id` 在正常路径中应保持未设置。
- 解析、状态结构和校验链上应预留该字段位置，避免后续引入 Retry 时重做 TP/CID 认证框架。

### 7.3 DCID 切换规则

客户端必须只响应**第一个** Initial 或 Retry 来切换发送用 DCID：

- 首个客户端 Initial 使用初始目标 DCID。
- 客户端在收到服务器首个有效 Initial 后，将后续发送包的 DCID 切换为服务器当时提供的 SCID。
- 之后不得因后续乱序或异常首包再次切换该发送目标 CID。

## 8. ACK、Loss、PTO 与反放大

### 8.1 ACK 规则

- Initial 与 Handshake 包必须立即 ACK。
- Initial / Handshake 空间的 `max_ack_delay = 0`。

### 8.2 PTO 规则

- 握手确认前，不为 AppData 空间设置 PTO。
- 客户端在服务器受反放大限制时，必须通过继续发送数据包帮助服务器解锁。
- 如果客户端还没有 Handshake keys，则 PTO 探测优先发送 1200 字节 Initial；否则发送 Handshake 包。

### 8.3 密钥丢弃后的状态清理

当某加密级别被丢弃时，必须一起清理：

- 该空间的 in-flight 计数
- 该空间的发送记录
- 该空间的 loss / PTO timer
- 该空间未处理的旧数据包

### 8.4 反放大限制

服务端在地址未验证前，必须满足：

- `bytes_sent <= bytes_received * 3`

并且：

- 所有唯一归属于该连接的发送有效载荷都要计入预算。
- 达到限制时保留状态和待发数据，不得直接把连接推进为失败。
- 成功处理来自对端的 Handshake 数据后，地址可视为通过验证。

## 9. 最小下载链路

### 9.1 应用协议范围

步骤01的应用成功路径限定为：

- HTTP/0.9 最小请求/响应语义
- 单条双向流
- 单连接

ALPN 约束写法采用可配置口径：

- 实现必须显式协商一个非 H3 的应用协议。
- 实现应允许 HQ ALPN 可配置。
- 若后续采用 `hq-interop`、`hq-29` 等字面值，只应作为互操作经验性默认值，不写成本文的硬编码协议要求。

### 9.2 客户端行为

客户端在 1-RTT 可发后：

- 从 `REQUESTS` 读取目标 URL
- 通过单条双向流发送最小请求
- 将收到的响应体写入 `/downloads`

### 9.3 服务端行为

服务端：

- 从 `/www` 读取目标文件
- 通过同一请求流返回文件内容
- 不在 `handshake` 成功路径中发送 Retry

### 9.4 Runner 契约

步骤00+01实现至少兼容以下 runner 契约：

- `/www`
- `/downloads`
- `/certs`
- `ROLE`
- `TESTCASE`
- `REQUESTS`
- `SSLKEYLOGFILE`
- `QLOGDIR`

退出码必须满足：

- `0`：成功
- `1`：错误
- `127`：当前 testcase 不支持

步骤00+01主线至少应放通：

- `versionnegotiation`
- `handshake`

其余 testcase 可返回 `127`。

## 10. 可观测性与日志

### 10.1 必须提供的观测入口

步骤00+01必须支持：

- `SSLKEYLOGFILE`
- `QLOGDIR`

并保证失败时能从以下层次逐级定位：

- runner `output.txt`
- client/server 端点日志
- `sim` 抓包
- qlog / keylog

### 10.2 qlog 组织建议

qlog 的最小组织建议如下：

- 使用 ODCID 作为连接级 `group_id`
- 使用 ODCID 作为文件名主标识，并追加观察点后缀
- 对无法归属到单一连接的无状态事件，可记录在端点级追踪文件中，或在能分配 ODCID 前暂存

### 10.3 应记录的关键事件

最小推荐事件集：

- `connection_started`
- `packet_sent`
- `packet_received`
- `key_updated`
- `key_discarded`
- `connection_state_updated`
- `connection_closed`

如果暂未完整实现标准 qlog 事件，也必须先保留等价的结构化事件接口，便于后续收敛。

### 10.4 敏感数据约束

结构化日志正文中不应记录：

- Retry token
- 地址验证 token
- TLS 会话票据
- TLS 解密密钥
- 其他可直接恢复明文或凭据的敏感值

## 11. 用户入口与 Makefile 约束

步骤00+01从这次开始统一要求：

- 仓库根目录维护一个 `Makefile`
- 所有高频命令优先通过 `make` 暴露
- `Makefile` 是用户主入口

至少预留这些目标：

- `make build`
- `make interop-versionnegotiation`
- `make interop-handshake-smoke`
- `make interop-handshake-server`
- `make interop-handshake-client`
- `make interop-logs CASE=<testcase>`

底层实现可以调用：

- `ai-quic/interop/*.sh`
- `python3 run.py ...`
- 本地自检脚本

但这些都属于实现细节，不应作为文档主入口。

## 12. 非目标与删除项

本文档刻意不再承载以下内容：

- “路径1”之类的旧命名
- “切片A-D”之类的分批历史记录
- “决策结论”式的临时拍板痕迹
- “基于当前代码复核的仍缺失 checklist”
- 对当前工作树实现状态的推测性判断
- 未由当前 `docs/`、`quic-interop-runner/`、RFC 笔记直接支撑的强绑定结论

若需要记录实现进度、代码现状或阶段性缺口，应放在单独的实现日志或交接文档中，而不是污染步骤00+01的行为规格。

## 13. 实现完成判定

### 13.1 步骤00完成条件

以下条件同时满足，才算步骤00完成：

- 未知版本首包可触发无状态 VN 回复
- VN 不创建真实连接状态
- VN 包满足线缆格式与版本列表约束
- 客户端能在收到 VN 后中止当前连接尝试

### 13.2 步骤01完成条件

以下条件同时满足，才算步骤01完成：

- `handshake` testcase 成功
- 使用单一 QUIC v1
- 没有 Retry
- 恰好 1 次握手
- 文件下载成功且内容匹配
- `HANDSHAKE_DONE`、密钥安装与旧 key 丢弃时序正确

### 13.3 联合完成条件

只有以下两项都满足，才算步骤00+01完成：

- 步骤00通过 `versionnegotiation`
- 步骤01通过 `handshake`

换言之，**step1 完成必须同时意味着 step0 与 step1 都完成**。
