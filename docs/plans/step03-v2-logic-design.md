# 步骤03 逻辑设计：Compatible Version Negotiation + QUIC v2 边界收敛

更新于 `2026-04-16`

## 1. 范围与完成标准

本文档定义步骤03的实现规格。目标不是描述某次工作树状态，而是给后续实现者一份以行为正确性为主的设计蓝图；同时它吸收了当前仓库已完成步骤01/02、且 `transfer` 双向 10 轮真实 interop 全通过之后的边界要求。

### 必做能力

- 在保持同一套 `connection / recovery / stream / flow control` 主逻辑的前提下支持 QUIC v2。
- 实现基于 RFC 9368 / RFC 9369 的 compatible version negotiation。
- 支持客户端以 v1 发起、服务端协商到 v2，并在 v2 上完成小文件下载。
- 保持无状态 Version Negotiation（步骤00）能力不回退。
- 保持步骤01/02已有的 `handshake` / `transfer` 路径不回退。
- 提供最小可用的版本协商观测信息：Original Version、Negotiated Version、Version Information、Initial key 版本、版本切换时序。

### 不做能力

- HTTP/3 / QPACK
- DATAGRAM
- 0-RTT 的跨版本兼容策略细化
- Retry + compatible negotiation 的完整复杂矩阵
- 多版本部署编排自动化
- 为 v2 单独复制一套 stream/loss/flow control/path manager

### 本地完成判定

步骤03只有在以下四项都通过时，才算完成：

- 步骤01的 `handshake` 路径仍可验证通过。
- 步骤02的 `transfer` 路径仍可验证通过。
- 步骤03的 `v2` 路径可在 compatible negotiation 场景下验证通过。
- 未知版本仍能走无状态 VN 路径。

## 2. 总体架构

步骤03沿用步骤01/02的连接骨架，只在“版本边界”补齐以下模块：

- `version catalog / version ops`
  - 负责定义每个版本的 wire number、long header mapping、Initial salt、HKDF labels、Retry integrity 参数、版本能力位。
- `dispatcher / version negotiation`
  - 负责 `Acceptable Versions / Offered Versions`、无状态 VN 以及 compatible negotiation 的入口判定。
- `packet codec`
  - 负责按版本编码/解码 long header，并用正确版本的参数派生 Initial / Retry / header protection。
- `transport parameters + TLS adapter`
  - 负责 `version_information` 的编解码、鉴权后校验、版本协商错误上报，以及 `tls_init(version, odcid)` 风格入口。
- `connection version state`
  - 负责在同一逻辑连接内管理 `original_version -> negotiated_version` 的切换。

设计原则：

- `dispatcher` 是步骤03唯一允许做版本分流决策的代理入口；它负责把 datagram 路由到正确的 `version_ops`，然后继续进入同一套连接主逻辑。
- 版本差异只允许出现在 `dispatcher / codec / Initial key schedule / Retry / transport parameters / TLS adapter` 边界。
- `stream / flow control / loss / PTO / scheduler / receive reassembly` 不应因为 v2 新增高层分支。
- compatible negotiation 仍是同一连接；无状态 VN 仍是连接外路径。
- `Original Version`、`Chosen Version`、`Negotiated Version` 必须显式区分，不能只靠一个 `conn->version` 糊住。
- 绝对不允许复制粘贴出 `v1 path` 与 `v2 path` 两套连接主逻辑；正确做法是“统一连接主逻辑 + `dispatcher` 代理分流 + `version_ops` 注入版本差异”。

### 2.1 代理模式约束

步骤03应显式遵循“代理模式”：

- `dispatcher`
  - 负责解析不变量、判断版本、选择 `version_ops`、决定是否创建或路由连接。
- `version_ops`
  - 负责把版本相关细节以数据表或函数表形式提供给 `packet codec`、TLS 边界和连接入口。
- `connection`
  - 负责统一的握手推进、恢复、流控、发送调度、多流传输。

这意味着：

- `dispatcher` 像代理层，决定“这条连接后续按哪套版本参数继续”。
- `connection` 不应感知“我是一条 v1 连接还是 v2 连接，所以要走不同源码文件”。
- 如果实现中出现 `conn_v1.c` / `conn_v2.c`、`stream_v1.c` / `stream_v2.c` 一类平行文件，说明设计已经偏离本步目标。

### 2.2 当前仓库建议对照锚点

为了让步骤03设计能直接落到当前代码结构，建议优先对照以下文件：

- `ai-quic/include/ai_quic/version.h`
  - 当前已定义 `AI_QUIC_VERSION_V1` 与 `AI_QUIC_VERSION_V2`，但还没有版本表与版本 ops。
- `ai-quic/include/ai_quic/packet.h`
  - 当前 `packet header` 里已有 `version` 和 VN 版本列表承载位。
- `ai-quic/src/transport/dispatcher.c`
  - 当前支持版本集合只有 `v1`，且数组容量硬编码为 `1`。
- `ai-quic/src/transport/version_negotiation.c`
  - 当前只有无状态 VN 包构造，没有 compatible negotiation 状态。
- `ai-quic/src/transport/packet_codec.c`
  - 当前 long header first byte、long header type 解析、Initial labels 仍是 v1 常量。
- `ai-quic/src/transport/transport_params.c`
  - 当前尚未实现 `version_information`。
- `ai-quic/src/transport/conn.c`
  - 当前连接只保存一个 `version`，适合扩展为 `original_version + negotiated_version`。
- `ai-quic/src/transport/conn_io.c`
  - 当前握手入口在 server 侧以收到的 Initial 直接启动 TLS，适合补“先解析 version_information 再决定 negotiated version”的收口点。

## 3. 参考实现抽取的设计结论

步骤03建议吸收三份参考实现的共同结论，而不是抄其中某一份的全部细节。

### 3.1 xquic 给出的启发

- 版本依赖项应集中管理，而不是散落在 frame/stream/recovery 高层逻辑里。
- `tls_init(version, odcid)`、`tls_reset_initial(version, odcid)` 这一类入口应是版本感知的统一边界。
- v2 阶段真正要验证的是“版本层可切换，而 transport 主逻辑不变”。

### 3.2 lsquic 给出的启发

- 更值得借鉴的是“版本分发表”和 `parse/build vtable`，而不是照抄某个具体实现细节。
- 正确方向是“同一 connection/recovery/stream 逻辑 + 不同版本 codec/key schedule 表”。
- 版本协商回归至少要覆盖：
  - v1 正常握手不回退
  - v1 发起并协商到 v2
  - 未知版本仍走 stateless VN

### 3.3 mvfst 给出的启发

- 推荐使用 `quic_version_ops` 风格的运行时版本表，而不是编译期开关分裂代码路径。
- v2 的目标是把差异限制在 codec/crypto/dispatch 边界，不是重写一套新的 transport 状态机。
- 对步骤03而言，最有价值的不是“大而全功能”，而是“版本差异收敛得足够窄”。

## 4. 版本模型设计

### 4.1 需要显式建模的版本概念

实现层至少应显式区分以下概念：

- `Original Version`
  - 客户端发出的第一个 QUIC 包的版本。
- `Chosen Version`
  - 当前某一轮客户端首飞正在使用的版本。
- `Negotiated Version`
  - 本连接最终使用的版本。
- `Header Version In Use`
  - 当前发出 long header 时真正写到包头里的版本。

对步骤03首版：

- 客户端 `Original Version` 固定为 v1。
- compatible negotiation 成功时，`Negotiated Version` 变为 v2。
- 未发生 compatible negotiation 时，`Negotiated Version == Original Version`。

### 4.2 连接状态建议

建议在连接中新增或显式化下列状态：

- `original_version`
- `negotiated_version`
- `peer_chosen_version`
- `negotiated_version_learned`
- `compatible_negotiation_enabled`
- `sent_version_information`
- `validated_peer_version_information`

同时保留单一的连接相位：

- `PRE_VALIDATION`
- `HANDSHAKING`
- `ACTIVE`
- `CLOSING`

版本切换不是一个新的连接状态机；它只是握手前半段的边界事件。

### 4.3 部署集合概念

即使当前只有单机/单容器部署，接口上也建议区分：

- `Acceptable Versions`
  - 本实例真正能完成连接的版本。
- `Offered Versions`
  - 本实例在收到未知版本时会放进 VN 包的版本集合。
- `Fully Deployed Versions`
  - 当前部署中所有实例都稳定支持、适合进入 `server_available_versions` 的集合。

单机阶段三者可以取同值，但不要把接口设计成永久等价。

## 5. Version Ops 设计

### 5.1 最小版本表结构

建议引入 `ai_quic_version_ops_t` 或等价结构，至少包含：

- `wire_version`
- `name`
- `long_header_initial_type_bits`
- `long_header_handshake_type_bits`
- `long_header_retry_type_bits`
- `long_header_0rtt_type_bits`
- `initial_salt`
- `initial_secret_label_prefix`
- `hp_label`
- `key_label`
- `iv_label`
- `ku_label`
- `retry_integrity_secret`
- `retry_integrity_key`
- `retry_integrity_nonce`
- `supports_compatible_v1_v2`

### 5.2 当前仓库的 v1-only 锚点

当前代码里至少有三处明显需要被 `version_ops` 吸收：

- `ai_quic_long_header_first_byte()`
  - 直接把 v1 的 Initial / Handshake 首字节写死。
- `ai_quic_long_header_packet_type()`
  - 直接按 v1 type bits 解析 long header。
- `ai_quic_quic_expand_label(..., "quic key" / "quic iv" / "quic hp")`
  - 直接写死 v1 label，无法支持 v2 的 `quicv2 *` 标签。

这三处共同说明：当前仓库最需要的是“把版本常量从连接主逻辑里抽到 `dispatcher -> version_ops` 代理链路”，而不是围绕 v2 再复制一套包处理流程。

### 5.3 v2 必须覆盖的版本差异

根据 RFC 9369，步骤03最少必须覆盖这些差异：

- wire version 为 `0x6b3343cf`
- long header type bits：
  - `Initial=0b01`
  - `0-RTT=0b10`
  - `Handshake=0b11`
  - `Retry=0b00`
- Initial salt 变更
- HKDF labels 从 `quic *` 切换到 `quicv2 *`
- Retry integrity secret/key/nonce 变更

除这些差异外，步骤03不应把版本分支扩散到高层 transport 语义。

如果后续实现需要在 `conn_io.c`、`loss.c`、`stream.c` 中写出大量 `if (version == v2)`，应优先回退设计，检查是否把本该留在 `dispatcher/version_ops/codec` 的差异漏到了高层。

## 6. Dispatcher 与版本协商设计

### 6.1 无状态 Version Negotiation 保持不变

步骤00已有的 VN 路径继续保持以下性质：

- 在 `dispatcher` 层完成
- 不创建真实连接对象
- 不推进握手状态
- 仅在未知版本且满足最小首包条件时返回 VN

当前 `version_negotiation.c` 的包构造逻辑可继续复用，但版本列表来源应从“固定一个 v1”升级为 `Offered Versions`。

### 6.2 Compatible Negotiation 入口

compatible negotiation 与无状态 VN 的最大区别是：

- VN 是连接外的无状态响应。
- compatible negotiation 发生在同一逻辑连接内。

但两者的共同点是：版本判定都应从 `dispatcher` 这个统一入口发起，而不是让连接内部自己“半路再决定换一套逻辑”。

因此，server 侧建议流程是：

1. 用 `Original Version` 解析客户端首飞。
2. 读取并重组客户端 Initial `CRYPTO`，直到能拿到 ClientHello 和其 transport parameters。
3. 解析客户端 `version_information`。
4. 在客户端 Available Versions 与本端 Acceptable Versions 的交集中选择 Negotiated Version。
5. 若选择的是不同于 Original Version 的兼容版本，则切换后续 server long header / Initial key write path 到 Negotiated Version。
6. 后续 `Handshake` 与 `1-RTT` 全部使用 Negotiated Version。

这里的关键不是“server 改成发 v2 包”本身，而是：

- `dispatcher`/连接入口只负责选定 `version_ops`
- `connection` 继续沿用同一套握手、ACK、loss、flow control、stream 路径
- 版本差异只通过 `version_ops` 影响包头、Initial key、Retry 和 transport parameters

### 6.3 Original / Negotiated Version 约束

根据 RFC 9368 / RFC 9369，步骤03首版至少满足：

- compatible negotiation 是同一连接，不是新建第二个连接对象。
- 客户端通过观察第一个不同于 Original Version 的 server long header Version 字段学习到 Negotiated Version。
- 一旦学到 Negotiated Version，客户端应切换后续 Initial 发送到该版本。
- 双端发送 `Handshake` 与 `1-RTT` 时必须只使用 Negotiated Version。
- 对任意其它版本的 Handshake / 1-RTT 包都应丢弃。

### 6.4 Retry 边界

尽管步骤03不要求完整打通 Retry + compatible negotiation 的所有组合，但接口设计必须正确：

- 若 server 发送 Retry，Retry 必须使用 Original Version。
- 客户端带 Retry token 的后续 Initial 仍必须使用 Original Version。
- server 可以在 token 中编码版本，以防客户端错误切换版本。
- 真正协商到 Negotiated Version 后，后续 `Handshake` / `1-RTT` 仍使用 Negotiated Version。

## 7. Packet Codec 与 Initial/Retry Crypto 设计

### 7.1 Long Header 编解码

`packet_codec` 必须变成“按版本解释 long header type bits”，而不是“按 v1 全局固定解释”。

推荐做法：

- encode 时由 `version_ops` 提供 type bits -> first byte 映射。
- decode 时先读 version，再用对应版本的 `version_ops` 解释 long header type bits。
- 对未知版本，只做 invariant parsing；不进入 version-specific packet decode。

### 7.2 Initial Key Derivation

Initial protection 的输入必须明确带版本：

- `version`
- `original_destination_cid`
- `is_server`
- `is_write`

当前 `packet_codec.c` 里：

- Initial salt 仍通过 v1 常量派生；
- `quic key / quic iv / quic hp` 标签仍写死；

这些都必须升级为：

- 从 `version_ops` 读取 salt
- 从 `version_ops` 读取 label
- 让 `tls_reset_initial(version, odcid)` 或等价函数成为统一重置入口

### 7.3 Retry Integrity

如果步骤03要保持 Retry 设计可扩展，则 Retry integrity 也必须版本感知：

- v1 与 v2 使用不同 secret/key/nonce
- 版本切换后，Retry 的认证上下文仍以 Original Version 为准

即使步骤03首版不把 Retry 与 v2 组合完全跑透，也不能把 Retry integrity 常量继续全局写死。

## 8. Transport Parameters 与 TLS 适配层设计

### 8.1 `version_information` transport parameter

步骤03必须在 transport parameters 中新增 `version_information`：

- transport parameter codepoint 为 `0x11`
- 格式为：
  - `Chosen Version`
  - `Available Versions[]`

对 client 侧：

- `Chosen Version` 必须包含在 `Available Versions` 中
- `Available Versions` 按偏好降序排列
- 若客户端希望优先协商到 v2，首版推荐 `[v2, v1]`

对 server 侧：

- `Chosen Version` 表示 server 最终为该连接选定的版本
- `Available Versions` 表示当前部署 Fully Deployed Versions
- 可为空，但在部分版本协商路径上要谨慎使用

### 8.2 校验规则

步骤03最少要校验：

- 长度可被 4 整除
- `Chosen Version != 0`
- `Available Versions` 中不得出现 `0`
- server 接收 client 的 `version_information` 时：
  - client 的 `Chosen Version` 必须包含在其 `Available Versions` 中
  - `Chosen Version` 必须与当前连接正在解析的版本一致
- client 接收 server 的 `version_information` 时：
  - server 的 `Chosen Version` 必须是 client 曾宣告可接受的版本
  - 若 compatible negotiation 已通过包头版本学习到 Negotiated Version，则 server `Chosen Version` 必须等于该版本

校验失败时：

- 关闭连接
- 解析失败与协商语义失败分开处理：
- 若是 `version_information` 自身格式错误（例如长度非法、含 `0`、client 的 `Chosen Version` 未包含在其 `Available Versions` 中），至少在 v1 路径上必须按 RFC 9368 使用 `TRANSPORT_PARAMETER_ERROR`
- 若是已通过认证后的协商语义不一致（例如 `Chosen Version` 与当前连接版本不一致、server 选择了 client 未声明支持的版本、compatible negotiation 学到的版本与 server `Chosen Version` 不一致），应映射到 `VERSION_NEGOTIATION_ERROR`

### 8.3 TLS 适配层边界

TLS 适配层至少应提供：

- `ai_quic_tls_session_start(version, transport_params, odcid)`
- `ai_quic_tls_session_reset_initial(version, odcid)`
- `ai_quic_tls_session_set_peer_version_information(...)`

原则是：

- TLS 负责产出/消费认证后的 transport parameters。
- QUIC 负责决定 Original/Negotiated Version 与包头版本切换。
- TLS 不应自己决定何时切版本。

### 8.4 Ticket / Token 版本作用域

根据 RFC 9369：

- session ticket 不得跨版本复用
- token 不得跨版本误验

因此步骤03即使暂不开放 resumption / 0-RTT，也应在 ticket/token 结构中预留版本字段。

## 9. Connection、Recovery 与 AppData 的不变量

### 9.1 高层 transport 主逻辑保持不变

下列模块在步骤03不应新增大面积版本分支：

- `stream manager`
- connection / stream flow control
- loss detection
- PTO
- send scheduler
- receive reassembly
- 文件下载落盘路径

换句话说，步骤03是“改变边界”，不是“重做 transport”。

### 9.2 版本切换只影响少数边界

建议将版本切换的影响约束在：

- dispatcher 路由与版本集合
- long header 编解码
- Initial key 与 Retry integrity
- transport parameters 中的 `version_information`
- 连接内 `original_version / negotiated_version` 状态

步骤03完成后，如果 `flow control` 或 `loss.c` 中出现大量 `if (version == v2)`，通常说明边界拆分失败。

## 10. 观测与日志设计

步骤03至少新增这些日志/观测字段：

- `original_version`
- `negotiated_version`
- `peer_chosen_version`
- `local_available_versions`
- `peer_available_versions`
- `compatible_negotiation_enabled`
- `negotiated_version_learned_at`
- `initial_secret_version`
- `version_information parse/validate result`
- `version negotiation error reason`

qlog 或普通日志至少应能回答：

- 当前连接是用 Original Version 还是 Negotiated Version 在发 long header
- server 何时决定切到 v2
- client 何时学到并确认 v2
- 若失败，是包头版本、Initial key、transport parameters 还是 downgrade validation 出错

## 11. 测试与验收设计

### 11.1 单测建议

步骤03建议至少补这些单测：

- `version_ops` 表：
  - v1/v2 的 wire version、type bits、salt、labels 可正确取回
- `packet_codec`：
  - v1/v2 long header 编解码
  - v1/v2 Initial key derivation向量
  - Retry integrity 参数切换
- `transport_params`：
  - `version_information` 编解码
  - 长度非法、`Chosen Version=0`、client 缺失自身版本等失败分支
- `dispatcher`：
  - `Acceptable Versions / Offered Versions` 路由
  - 未知版本仍走 stateless VN

### 11.2 集成测试建议

步骤03建议至少补这些本地集成：

- `v1 -> v1` 握手下载保持通过
- `v1 -> v2` compatible negotiation 后下载小文件通过
- 缺失或畸形 `version_information` 触发连接失败
- v1 `transfer` 回归保持通过

### 11.3 Interop 建议

当步骤03进入真实 interop 时，至少覆盖：

- `ai-quic server -> xquic client` 的 `v2`
- `xquic server -> ai-quic client` 的 `v2`
- 回归 `handshake`
- 回归 `transfer`

建议以以下顺序推进：

1. 本地 unit
2. 本地 integration
3. 单方向 `v2`
4. 双方向 `v2`
5. 回归 `handshake`
6. 回归 `transfer`

## 12. 分阶段实现建议

### 阶段 A：版本表与编解码收敛

- 引入 `ai_quic_version_ops_t`
- 改造 `dispatcher` 支持多版本集合
- 改造 `packet_codec` 使用版本表解释 long header 与 Initial labels

### 阶段 B：`version_information` 与连接状态

- 在 transport parameters 中实现 `version_information`
- 为连接补 `original_version / negotiated_version`
- 在 TLS 适配层补版本感知入口

### 阶段 C：compatible negotiation 路径

- server 解析 client `version_information`
- server 选择 Negotiated Version
- client 从 server long header 学习 Negotiated Version
- 双端在同一连接内完成版本切换

### 阶段 D：回归与排障能力

- 补 unit / integration / interop
- 补版本协商日志与 qlog 观测
- 确认 `handshake / transfer / versionnegotiation` 都不回退

## 13. 当前代码下的直接设计结论

基于当前仓库，步骤03最直接的工程结论是：

- `version.h` 已经有 v2 常量，但这还只是“知道有 v2”，不是“支持 v2”。
- `dispatcher.c` 当前只支持一个 v1，说明版本集合与部署集合尚未建模。
- `packet_codec.c` 当前把 v1 的 long header type bits 和 `quic *` labels 写死，是步骤03最核心的改造入口。
- `transport_params.c` 当前没有 `version_information`，意味着 compatible negotiation 和 downgrade prevention 还没有认证载体。
- `conn.c` / `conn_io.c` 当前把连接看成“只有一个 version”，这正是步骤03要补成 `original -> negotiated` 双版本语义的地方。

因此，步骤03不应从 `stream` 或 `loss` 开始改，而应从 `version ops + packet codec + transport params + conn version state` 这条主线切入。
