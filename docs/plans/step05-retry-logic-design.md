# 步骤05 逻辑设计：Stateless Retry + Token Service + Address Validation

更新于 `2026-04-23`

## 1. 范围与完成标准

本文档定义步骤05的实现规格。目标不是描述某次工作树状态，而是给后续实现者一份以行为正确性为主的设计蓝图；同时它吸收了步骤01/02/03/04已经建立的连接、传输、版本协商、cipher policy 与 packet codec 边界。

### 必做能力

- 支持 `TESTCASE="retry"`，让服务端发送 Retry，客户端在第二个 Initial 中携带 Retry token 继续握手并下载小文件。
- 服务端在连接创建前完成 Retry/token 校验决策，保持无状态 Retry 路径。
- 客户端独立验证 Retry integrity tag，并在收到合法 Retry 后重建 Initial 发送路径。
- Token 服务至少显式区分：
  - `Retry token`
  - `NEW_TOKEN`
  - `resumption / application token`
- Retry token 至少绑定：
  - client 地址
  - original destination connection ID
  - 时间戳/有效期
  - 防伪 MAC
- transport parameters 校验链覆盖：
  - 无 Retry 时的 `original_destination_connection_id`
  - 有 Retry 时额外的 `retry_source_connection_id`
  - `ODCID / Retry SCID / current DCID` 的一致性
- 服务端地址未验证前，发送预算严格服从 `bytes_received * 3`，重传与 coalescing 使用同一账本。
- 保持 `handshake`、`transfer`、`v2`、`chacha20` 既有路径不回退。
- 提供最小可用观测信息：Retry 决策、token 校验结果、地址验证来源、Retry SCID、ODCID、发送预算变化。

### 不做能力

- 不在步骤05实现完整的 `NEW_TOKEN` 缓存/复用策略。
- 不实现 resumption、0-RTT 与 Retry 的完整交互矩阵。
- 不实现 post-handshake migration、rebind 或 preferred address。
- 不实现分布式/多实例 token 密钥轮换系统。
- 不把 Retry 扩散成第二套 `connection / recovery / stream / flow control` 源码路径。

### 本地完成判定

步骤05只有在以下六项都通过时，才算完成：

- 普通 `handshake` 路径仍可验证通过。
- 普通 `transfer` 路径仍可验证通过。
- `v2` 路径仍可验证通过。
- `chacha20` 路径仍可验证通过。
- `retry` 路径可在 runner 中完成 Retry、握手和文件下载。
- 单测/集成测试覆盖 Retry integrity、token MAC/过期/地址绑定、TP 一致性校验与反放大计账。

## 2. 总体架构

步骤05沿用统一连接骨架，只在“无状态握手入口 + 地址验证边界”补齐以下模块：

- `token service`
  - 负责生成与校验 Retry token，并显式区分 Retry / NEW_TOKEN / resumption token。
- `dispatcher retry gate`
  - 负责在创建连接之前解析 token、决定是否发送 Retry、是否丢弃、是否允许创建连接。
- `retry packet codec / integrity`
  - 负责 Retry 包类型识别、Retry pseudo-packet 构造与 Retry integrity tag 生成/验证。
- `client retry restart path`
  - 负责客户端收到 Retry 后撤销旧 Initial 状态、保存 token、切换 DCID 并复派生 Initial keys。
- `address validation + anti-amplification accounting`
  - 负责地址验证标志、验证来源、发送预算与重传/coalescing 共用账本。

设计原则：

- Retry 是 `dispatcher / endpoint` 的早期岔路，不是连接内部的新状态机。
- `stream / recovery / flow control / scheduler` 不应因为 Retry 分裂出单独实现。
- Token 校验应尽量发生在创建连接之前；连接只消费“地址已验证”或“仍未验证”的结果。
- Retry token、`NEW_TOKEN`、resumption/application token 必须在类型、密钥空间与有效期上显式分离。
- `ODCID / Retry SCID / current DCID` 与 transport parameters 校验链必须一起设计，不要拆成互不知情的局部判断。

## 3. 规范约束

### 3.1 Retry 包处理

根据 RFC 9000 / RFC 9001，步骤05至少满足：

- Retry 是长首部包，但不携带 packet number，不能被 ACK。
- 客户端必须验证 Retry integrity tag。
- 客户端必须丢弃：
  - integrity tag 错误的 Retry
  - zero-length token 的 Retry
  - 同一连接尝试中的第二个 Retry
- 客户端收到合法 Retry 后，后续 Initial 的 DCID 必须改成 Retry 包中的 SCID，并用新的 DCID 复派生 Initial keys。
- 客户端的 token 字段必须设置为 Retry 包提供的 token。

步骤05首版的客户端侧策略建议固定为：

- 保持客户端自己的 SCID 不变；
- 只强制修改后续 Initial 的 DCID 与 token；
- 这样可以把变量收敛到“Retry SCID + token + Initial keys”三件事上。

### 3.2 Token 作用域

三类 token 必须显式区分：

- `Retry token`
  - 用于当前握手继续推进，寿命短，必须绑定 client 地址。
- `NEW_TOKEN`
  - 用于未来连接的地址验证优化，可以更长寿命，但不是 Retry 当前握手 token。
- `resumption / application token`
  - 属于 TLS 或应用层语义，不应由 transport token 服务按 Retry 规则解析。

步骤05首版建议：

- 用显式 `token_kind` 区分三类 token。
- Retry token 的编码至少包含：
  - token kind
  - original version
  - original destination connection ID
  - client 地址或其稳定表示
  - issue time
  - expiry time 或 ttl
  - MAC
- 如实现需要直接恢复 Retry SCID，也可以把 `retry_scid` 纳入 Retry token 负载。

### 3.3 地址验证与反放大限制

步骤05的地址验证模型必须满足：

- 服务端在地址未验证前，发送预算为 `bytes_received * 3`。
- `bytes_received` 与 `bytes_sent` 是同一路径的统一账本，不允许为重传、coalescing 或 probe packet 开平行计数器绕过限制。
- Retry 本身走无状态路径，不提前占用真实连接资源。
- 地址验证成功的来源至少区分：
  - `NONE`
  - `RETRY_TOKEN`
  - `NEW_TOKEN`
  - `HANDSHAKE`

步骤05首版中：

- 通过有效 Retry token 创建连接时，可以立即把地址视为已验证。
- 无 token 正常握手路径，仍可在后续握手完成后把地址视为已验证。

### 3.4 Transport Parameters 一致性

transport parameters 校验链必须覆盖以下规则：

- 无 Retry 路径：
  - 客户端必须校验服务端 `original_destination_connection_id == client 原始 ODCID`
  - 客户端不应要求 `retry_source_connection_id`
- 有 Retry 路径：
  - 客户端必须继续校验 `original_destination_connection_id == client 原始 ODCID`
  - 客户端还必须校验 `retry_source_connection_id == 收到的 Retry SCID`
- 服务端路径：
  - 始终校验客户端 `initial_source_connection_id`
  - 如当前连接由 Retry token 恢复，还必须把本端写入的 `retry_source_connection_id` 固定为实际发送 Retry 时使用的 SCID

## 4. 接口与数据模型设计

### 4.1 Packet 与 dispatcher 类型

建议新增或扩展以下类型：

- `AI_QUIC_PACKET_TYPE_RETRY`
- `AI_QUIC_DISPATCH_SEND_RETRY`

对应影响：

- `ai_quic_packet_type_t`
  - 需要能显式表示 Retry，而不是把 Retry 混入 `VERSION_NEGOTIATION` 或 `INITIAL` 的模糊分支。
- `ai_quic_dispatch_action_t`
  - 需要从当前的 `DROP / VERSION_NEGOTIATION / ROUTE_EXISTING / CREATE_CONN` 扩展出 Retry 动作。
- `ai_quic_dispatch_decision_t`
  - 需要能承载：
    - token 是否存在
    - token 校验结果
    - 是否应发送 Retry
    - Retry SCID
    - 是否允许创建连接

### 4.2 Token service 接口

建议把 token 服务独立成一个 transport 子模块，至少提供：

- `generate_retry_token(peer_addr, original_version, odcid, retry_scid, now_ms)`
- `validate_retry_token(peer_addr, original_version, odcid, token, now_ms, out_context)`

`out_context` 至少能恢复：

- `original_version`
- `odcid`
- `retry_scid`（如果 token 编码了它）
- `issued_at_ms`
- `expires_at_ms`
- `token_kind`

步骤05首版建议的校验结果分三类：

- `TOKEN_VALID`
  - 允许创建连接，并把地址视为已验证。
- `TOKEN_MISSING`
  - 若当前策略要求 Retry，则发送 Retry。
- `TOKEN_INVALID`
  - 包括 MAC 错误、过期、地址不匹配、ODCID 不匹配、kind 错误。
  - 首版直接丢弃，不再为无效 token 再次发送 Retry。

这样做的收益是：

- Happy path 只需要一次 Retry。
- token 篡改、地址变化与 ODCID 不匹配都天然落到“无连接、无放大”的失败路径。

### 4.3 连接与 TP 状态

连接侧至少应显式保存：

- `original_destination_cid`
- `retry_source_cid`
- `current_dcid`
- `retry_accepted`
- `retry_token_present`
- `address_validation_method`
- `address_validated`

transport parameters 侧至少显式保存：

- `original_destination_connection_id`
- `initial_source_connection_id`
- `retry_source_connection_id`
- `has_retry_source_connection_id`

步骤05首版建议：

- `no-retry` 路径中，`retry_source_cid` 与 `has_retry_source_connection_id` 都保持未设置。
- `retry` 路径中，连接创建时就把 `retry_source_cid` 固定下来，避免后续“当前 CID”变化后反查失败。

### 4.4 路径与地址元信息

Retry token 正确实现必须能看到 client 地址。因此步骤05实现时必须补齐“源地址从 UDP 收包路径传到 dispatcher/token service”的链路。

当前 `ai_quic_endpoint_receive_datagram()` 只接收 datagram 与 `now_ms`，没有携带 peer address，这意味着：

- 当前 API 还不足以完成真正的地址绑定 token 校验；
- 步骤05实现时需要为 endpoint/dispatcher 增加 path/address 元信息输入；
- 首版可以只支持单活动路径，但不能继续把“地址验证”写成没有地址输入的伪逻辑。

## 5. 数据流设计

### 5.1 Server 首个 Initial 流程

服务端收到未知连接的 Initial 后，步骤05建议流程如下：

1. `dispatcher` 解析 invariant header，确认这是支持版本的 Initial。
2. 从包头读取 token 字段。
3. 根据当前 Retry 策略处理：
   - token 缺失：发送无状态 Retry，不创建连接。
   - token 存在但无效：丢弃，不创建连接。
   - token 有效：允许创建连接，并把地址视为已验证。
4. 只有在 token 有效或策略允许直通时，`endpoint` 才创建连接对象并进入 `conn_io` 路径。
5. 若当前连接由 Retry token 恢复：
   - `original_destination_cid` 使用客户端第一次 Initial 的 ODCID
   - `retry_source_cid` 使用服务端先前发送 Retry 的 SCID
   - `local_transport_params.retry_source_connection_id` 必须设置

这条流程的关键点是：

- Retry 决策不进入真实连接。
- 连接只处理“已经被 dispatcher 筛过”的握手输入。

### 5.2 Client 处理 Retry 流程

客户端收到服务端 Retry 后，步骤05建议流程如下：

1. 确认当前连接尝试尚未接受过 Retry。
2. 验证 Retry integrity tag。
3. 检查 Retry token 非空。
4. 保存：
   - 原始 ODCID
   - Retry token
   - Retry SCID
5. 撤销旧的 Initial 发送状态：
   - 旧 Initial sent map / ACK 状态 / PTO 计时都不能继续沿用
6. 将下一次 Initial 的 DCID 切换为 Retry SCID。
7. 基于新的 DCID 复派生 Initial keys。
8. 重发 Initial，token 字段带上 Retry token。

步骤05首版明确要求：

- 客户端只接受一次合法 Retry。
- 第二次收到 Retry 必须静默丢弃。
- Retry 后继续保留同一逻辑连接，不新建“第二个客户端连接对象”。

### 5.3 No-Retry 流程

不走 Retry 时，路径必须保持现有行为：

- 服务端可直接创建连接推进握手。
- `retry_source_connection_id` 保持缺省。
- 地址验证仍可在握手完成后达成。
- `handshake / transfer / v2 / chacha20` 行为不应因步骤05被改写。

### 5.4 TP 校验流程

客户端在校验服务端 transport parameters 时：

1. 总是校验 `original_destination_connection_id == original ODCID`
2. 总是校验 `initial_source_connection_id == peer 当前握手 SCID`
3. 若 `retry_accepted == 1`：
   - 额外要求存在 `retry_source_connection_id`
   - 且其值等于保存的 Retry SCID
4. 任一失败都触发 transport error

服务端在校验客户端 transport parameters 时：

1. 总是校验 `initial_source_connection_id`
2. Retry 路径不额外要求客户端携带 `retry_source_connection_id`
3. token 恢复出的 ODCID / 地址 / 版本信息与当前握手上下文不一致时，不创建连接

## 6. 当前仓库建议对照锚点

当前代码已有以下基础：

- `ai-quic/include/ai_quic/packet.h`
  - Initial 头部已经有 token 字段；
  - 但 `ai_quic_packet_type_t` 还没有 Retry 包类型。
- `ai-quic/src/transport/version.c`
  - 已保存 v1/v2 的 `retry_type_bits` 与 Retry integrity 常量；
  - 但 long header 编解码目前仍只显式处理 Initial/Handshake。
- `ai-quic/src/transport/packet_codec.c`
  - 已能编解码 Initial token 字段；
  - 但还没有 Retry 包编解码、Retry pseudo-packet 与 integrity tag 逻辑。
- `ai-quic/src/transport/dispatcher.c`
  - 当前只有 `DROP / VERSION_NEGOTIATION / ROUTE_EXISTING / CREATE_CONN`；
  - 对受支持版本的 Initial 会直接走 `CREATE_CONN`。
- `ai-quic/src/transport/endpoint.c`
  - 服务端在首个支持版本 Initial 上会直接创建连接；
  - 当前没有无状态 Retry 分支；
  - 当前 `receive_datagram` API 也没有 peer address 输入。
- `ai-quic/src/transport/transport_params.c`
  - 已有 `original_destination_connection_id / retry_source_connection_id` 编解码；
  - 但 `validate_client()` 只校验 ODCID 与 `initial_source_connection_id`，还没把 Retry SCID 纳入链路；
  - `validate_server()` 目前只校验 `initial_source_connection_id`。
- `ai-quic/src/transport/conn_io.c`
  - 已有基础反放大检查；
  - 但预算只在发送末端统一拦截，尚未接入 token-based address validation、Retry restart 或 TP 三元组校验。
- `ai-quic/interop/testcase_dispatch.sh`
  - 目前不放行 `retry`。
- `ai-quic/tests/interop/test_run_endpoint.sh`
  - 当前把 `retry` 视为“应返回 127”的未实现 testcase。

这些锚点说明：步骤05不是“只加一个 token 校验 if 语句”，而是要把 version 常量、packet codec、dispatcher、endpoint、TP 校验与发送预算收敛成同一条设计链路。

## 7. 错误处理与日志

### 7.1 错误处理

建议错误边界：

- token 缺失且策略要求 Retry：
  - 发送 Retry，不创建连接。
- token MAC 错误 / 过期 / 地址不匹配 / ODCID 不匹配 / kind 错误：
  - 丢弃，不创建连接。
- Retry integrity tag 错误：
  - 客户端丢弃 Retry，不重建 Initial。
- zero-length Retry token：
  - 客户端丢弃 Retry。
- 第二次 Retry：
  - 客户端丢弃。
- Retry 路径上 `retry_source_connection_id` 缺失或错误：
  - 连接以 transport error 关闭。
- 地址未验证前预算不足：
  - 不发送，并记录预算错误。

### 7.2 日志与观测

至少输出以下信息：

- Retry 决策结果：
  - `retry_policy`
  - `token_present`
  - `token_validation_result`
  - `retry_sent`
- Retry 与 CID：
  - `original_dcid`
  - `retry_scid`
  - `current_dcid`
- 地址验证：
  - `address_validated`
  - `address_validation_method`
  - `peer_address`
- 预算：
  - `bytes_received_before_validation`
  - `bytes_sent_before_validation`
  - `amplification_budget`
  - `budget_remaining`

日志或 qlog 至少应能回答：

- 服务端为什么发送或不发送 Retry
- token 是缺失、过期、篡改还是地址不匹配
- 客户端第二个 Initial 是否正确携带 token
- Retry 后 Initial 的 DCID 是否切换到 Retry SCID
- 哪个时刻地址被判定为已验证
- 是否出现发送预算超限

## 8. 测试设计

### 8.1 Unit tests

步骤05建议至少补这些单测：

- Retry packet codec：
  - v1/v2 Retry 类型识别
  - Retry pseudo-packet 构造
  - Retry integrity tag 生成/验证
  - 错误 version constants 不混用
- token service：
  - 正常生成/校验成功
  - token MAC 篡改失败
  - token 过期失败
  - client 地址变化失败
  - ODCID 不匹配失败
  - `token_kind` 错误失败
- dispatcher：
  - 无 token -> `SEND_RETRY`
  - 无效 token -> `DROP`
  - 有效 token -> `CREATE_CONN`
  - 不支持版本仍走 `VERSION_NEGOTIATION`
- transport params：
  - 无 Retry 路径只要求 ODCID
  - Retry 路径要求 `retry_source_connection_id`
- anti-amplification：
  - 重传计入预算
  - coalescing 计入预算
  - 预算不足时发送失败

### 8.2 Integration tests

步骤05建议至少补这些本地集成：

- ai-quic client/server 在本地完成 Retry、握手和小文件下载。
- 客户端 token 被篡改后无法完成连接。
- 模拟 client 地址变化后，token 校验失败。
- 无 Retry 的普通 handshake 路径不回退。
- v2/chacha20 路径不应因为步骤05引入的 dispatcher 逻辑退化。

### 8.3 Interop tests

实现完成后建议执行：

- `make test-unit`
- `make test-integration`
- `make test-interop-retry`
- `make interop-retry-server`
- `make interop-retry-client`
- `make test-interop-handshake`
- `make test-interop-transfer`
- `make test-interop-v2`
- `make test-interop-chacha20`

`amplificationlimit` 建议从步骤05开始作为影子回归跟进：

- 首版可以先通过日志和本地预算断言建立证据；
- 后续若 Makefile 加入独立入口，再把它升级为显式回归目标。

## 9. 不变量

步骤05实现完成后必须保持以下不变量：

- Retry 是 dispatcher 级无状态路径，不创建真实连接对象。
- Retry token、`NEW_TOKEN`、resumption/application token 在类型和密钥空间上显式分离。
- 客户端接受合法 Retry 后，后续 Initial 的 DCID 必须等于 Retry SCID。
- Retry 后 Initial keys 必须基于新的 DCID 复派生。
- 无 Retry 路径中，`retry_source_connection_id` 必须保持未设置。
- 地址未验证前的发送预算只允许有一套账本；重传、coalescing 与 probe packet 都不能绕过它。
- 步骤05不能改变既有 `connection / recovery / stream / flow control / scheduler` 的主逻辑形状。
