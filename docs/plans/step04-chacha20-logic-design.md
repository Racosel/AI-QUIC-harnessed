# 步骤04 逻辑设计：TLS Cipher Policy + ChaCha20 Packet Protection

更新于 `2026-04-22`

## 1. 范围与完成标准

本文档定义步骤04的实现规格。目标不是描述某次工作树状态，而是给后续实现者一份以行为正确性为主的设计蓝图；同时它吸收了步骤01/02/03已经建立的连接、传输、版本协商与 packet codec 边界。

### 必做能力

- 支持 `TESTCASE="chacha20"`，让客户端和服务器只提供 `TLS_CHACHA20_POLY1305_SHA256`。
- TLS 最终协商到 ChaCha20 后，Handshake 与 1-RTT packet protection 使用 ChaCha20-Poly1305 AEAD。
- ChaCha20 header protection 使用 RFC 9001 定义的 16 字节 sample、4 字节 counter、12 字节 nonce 与 5 字节 mask。
- Initial packet protection 继续由 QUIC version ops 决定，使用版本 salt、Initial secret 与 AES-128-GCM。
- 保持 `handshake`、`transfer`、`v2` 既有路径不回退。
- 提供最小可用观测信息：cipher policy、negotiated cipher suite、encryption level、AEAD、header protection algorithm。

### 不做能力

- 不实现 Key Update 的完整密钥轮换状态机。
- 不引入 0-RTT / resumption cipher policy 兼容矩阵。
- 不实现 HTTP/3/QPACK 的 cipher policy。
- 不为 ChaCha20 复制新的 connection/recovery/stream/flow-control 路径。
- 不用 testcase 字符串绕过 TLS 协商结果。

### 本地完成判定

步骤04只有在以下五项都通过时，才算完成：

- 普通 `handshake` 路径仍可验证通过。
- 普通 `transfer` 路径仍可验证通过。
- `v2` 路径仍可验证通过。
- `chacha20` 路径可在 runner 中完成握手和文件下载。
- 单测覆盖 cipher suite ops、ChaCha20 header protection 与错误 key/nonce 失败路径。

## 2. 总体架构

步骤04沿用统一连接骨架，只在“TLS/crypto 边界”补齐以下模块：

- `cipher policy`
  - 负责描述端点允许或强制的 TLS 1.3 cipher suite 集合。
- `TLS adapter`
  - 负责把 cipher policy 应用到 BoringSSL QUIC session，并把 TLS callback 中的 cipher suite id 与 secret 传给 packet protection。
- `cipher suite ops`
  - 负责把 TLS cipher suite id 映射为 AEAD、HKDF hash、tag length、nonce length、header protection algorithm、hp key length 与 usage limit。
- `packet protection`
  - 负责按 encryption level 构造 read/write packet protection；Initial 使用 version ops，Handshake/1-RTT 使用 TLS negotiated cipher suite。
- `interop/testcase dispatch`
  - 负责把 `TESTCASE="chacha20"` 标记为已支持，并把 chacha20-only policy 传入 endpoint。

设计原则：

- cipher policy 是配置问题，不是 transport 状态机问题。
- `connection / recovery / stream / flow control / scheduler` 不应知道当前是 AES-GCM 还是 ChaCha20。
- packet protection 可以知道 AEAD/header protection algorithm，但必须通过集中 ops 获取，不要散落硬编码。
- Header protection 与 payload AEAD 是两个独立对象；两者使用不同派生标签和不同 key material。
- Initial 与 TLS negotiated packet protection 是两条不同路径；步骤04不得把 Initial 改成 ChaCha20。

## 3. 规范约束

### 3.1 AEAD usage

根据 RFC 9001：

- QUIC packet protection 使用 TLS 连接协商出的 AEAD。
- 如果 TLS 使用 `TLS_CHACHA20_POLY1305_SHA256`，则 QUIC Handshake/1-RTT 使用 `AEAD_CHACHA20_POLY1305`。
- QUIC 可以使用 TLS 1.3 中定义且具备 QUIC header protection scheme 的 cipher suite。
- 不得协商 `TLS_AES_128_CCM_8_SHA256`。
- 端点不得仅因为 ClientHello 提供未知或不支持的 cipher suite 就拒绝连接。
- TLS 1.3 cipher suite 的 authentication tag 长度为 16 字节，packet protection 输出比输入大 16 字节。

### 3.2 Initial protection

Initial packet protection 与 TLS cipher suite negotiation 分离：

- Initial secret 由 QUIC version salt 与客户端第一个 Initial 的 Destination Connection ID 派生。
- Initial packet protection 使用 AES-128-GCM 与 AES header protection。
- v1/v2 的 salt 与 label 差异属于 Step03 的 version ops；Step04 不改变 version ops 语义。
- 只有 Handshake、1-RTT 与后续应用数据使用 TLS callback 安装的 negotiated cipher suite。

### 3.3 Header protection

Header protection 的实现必须满足：

- packet protection 之后应用 header protection。
- 接收侧先移除 header protection，恢复 packet number，再打开 AEAD。
- sample 从 packet number 起始偏移 4 字节处开始。
- TLS 1.3 QUIC cipher suite 使用 16 字节 header protection sample。
- AES-GCM 使用 AES-ECB 生成 mask。
- ChaCha20-Poly1305 使用 raw ChaCha20 生成 mask：sample 前 4 字节是 little-endian counter，后 12 字节是 nonce，明文为 5 个零字节。
- long header 保护首字节低 4 位，short header 保护首字节低 5 位，packet number bytes 使用 mask 后续字节。

## 4. 配置与接口设计

### 4.1 Endpoint cipher policy

建议新增 `ai_quic_cipher_policy_t` 或等价枚举：

- `AI_QUIC_CIPHER_POLICY_DEFAULT`
- `AI_QUIC_CIPHER_POLICY_CHACHA20_ONLY`

配置入口建议放在 endpoint config：

- `ai_quic_endpoint_config_t.cipher_policy`
- 默认值为 `AI_QUIC_CIPHER_POLICY_DEFAULT`
- demo CLI 可增加 `--cipher-policy default|chacha20-only`
- interop `TESTCASE="chacha20"` 只负责选择 `chacha20-only` policy，不直接操控 packet codec

这样做的边界是：

- demo/interop 可以决定策略。
- TLS adapter 应用策略。
- packet protection 只消费 TLS 已协商出的 cipher suite id。
- connection 主逻辑完全不感知策略来源。

### 4.2 TLS adapter policy

TLS adapter 应提供 policy-aware session create/setup：

- BoringSSL 模式：
  - TLS version 仍固定 TLS 1.3。
  - default policy 保持默认 TLS 1.3 cipher suite 集合。
  - chacha20-only policy 只启用 `TLS_CHACHA20_POLY1305_SHA256`。
  - policy 设置失败时 session create 失败，并输出明确错误。
- fake TLS 模式：
  - default policy 继续模拟 `TLS_AES_128_GCM_SHA256`。
  - chacha20-only policy 模拟 `TLS_CHACHA20_POLY1305_SHA256`。
  - fake TLS 只能用于本地单测/集成测试，不能作为 runner 真实 TLS 互操作证据。

TLS callback 已经提供 `cipher` 与 `secret`，实现应继续通过 `SSL_CIPHER_get_protocol_id(cipher)` 保存 wire cipher suite id。packet protection 不应反查 TLS 对象。

### 4.3 Cipher suite ops

建议把当前 packet codec 中的 cipher 选择升级为集中结构：

- `cipher_suite_id`
- `name`
- `aead`
- `hkdf_md`
- `tag_len`
- `nonce_len`
- `hp_algorithm`
- `hp_key_len`
- `hp_sample_len`
- `confidentiality_limit`
- `integrity_limit`

首版至少支持：

- `TLS_AES_128_GCM_SHA256`
- `TLS_AES_256_GCM_SHA384`
- `TLS_CHACHA20_POLY1305_SHA256`

实现要求：

- 接受 BoringSSL TLS 1.3 protocol id 的 wire form，例如 `0x1301 / 0x1302 / 0x1303`。
- 如内部宏带历史前缀，应统一 mask 到低 16 位比较。
- `TLS_AES_128_CCM_8_SHA256` 不得进入可协商集合。
- unknown cipher suite 对 packet protection 是错误，但对 ClientHello offer 不能提前拒绝。

## 5. 数据流设计

### 5.1 Client chacha20-only flow

客户端流程：

1. demo/interop 解析 `TESTCASE="chacha20"`，选择 `AI_QUIC_CIPHER_POLICY_CHACHA20_ONLY`。
2. endpoint 创建 TLS session 时传入 cipher policy。
3. BoringSSL ClientHello 只提供 `TLS_CHACHA20_POLY1305_SHA256`。
4. Initial packet 使用 version ops 派生 AES-128-GCM protection。
5. TLS callback 安装 Handshake write/read secret，并记录 cipher suite id `0x1303`。
6. packet protection 通过 cipher suite ops 构造 ChaCha20-Poly1305 AEAD 与 ChaCha20 header protection。
7. TLS callback 安装 1-RTT secret 后，1-RTT 继续使用相同 negotiated cipher suite。
8. 应用下载路径复用既有 stream/flow control/scheduler。

### 5.2 Server chacha20-only flow

服务端流程：

1. demo/interop 解析 `TESTCASE="chacha20"`，选择 `AI_QUIC_CIPHER_POLICY_CHACHA20_ONLY`。
2. endpoint 创建 TLS session 时传入 cipher policy。
3. BoringSSL server 只从允许集合中选择 `TLS_CHACHA20_POLY1305_SHA256`。
4. Initial read/write protection 仍使用 version ops 和 AES-128-GCM。
5. TLS callback 安装 Handshake/1-RTT secret 时记录 cipher suite id。
6. packet protection 对 Handshake/1-RTT 使用 ChaCha20-Poly1305 和 ChaCha20 header protection。
7. 文件发送路径复用既有 stream/flow control/scheduler。

### 5.3 Default flow

default policy 必须保持现有行为：

- 不强制 ChaCha20。
- 不改变普通 `handshake`、`transfer`、`v2` 的 cipher negotiation。
- packet protection 仍按 TLS callback 提供的 cipher suite id 选择 AES 或 ChaCha ops。

## 6. 当前仓库建议对照锚点

当前代码已有以下基础：

- `ai-quic/src/transport/packet_codec.c`
  - 已能识别 `0x1301 / 0x1302 / 0x1303`，并选择 AES-GCM 或 ChaCha20-Poly1305。
  - 已有 AES 与 ChaCha20 header protection mask 生成雏形。
  - 仍需要把 cipher 选择整理成更明确的 ops，并补测试覆盖。
- `ai-quic/src/tls/tls_ctx.c`
  - 已通过 BoringSSL QUIC callbacks 保存 `cipher_suite` 与 secret。
  - 仍缺少 cipher policy 输入与 TLS 1.3 cipher suite 限制。
- `ai-quic/include/ai_quic/endpoint.h`
  - endpoint config 尚缺显式 cipher policy 字段。
- `ai-quic/interop/testcase_dispatch.sh`
  - 尚未开放 `chacha20`。
- `ai-quic/demo/common/demo_cli.c`
  - 尚未提供 `--cipher-policy` 或等价配置入口。

这些锚点说明：步骤04不需要重写 packet codec，而是要把“已有可用原语”收敛成配置明确、测试可证、日志可观察的 policy 驱动路径。

## 7. 错误处理与日志

### 7.1 错误处理

建议错误边界：

- cipher policy 字符串无法识别：demo CLI 返回参数错误。
- TLS adapter 无法应用 chacha20-only policy：endpoint/session create 失败。
- TLS 协商结果不符合 chacha20-only policy：握手失败并记录明确错误。
- packet protection 收到未知或禁用 cipher suite id：连接进入关闭或返回解包错误。
- packet 长度不足以提供 16 字节 hp sample：丢弃该包，不应读越界。
- AEAD open 失败：丢弃该包，并计入后续 AEAD integrity limit 统计的设计入口。

### 7.2 日志与观测

至少输出以下信息：

- endpoint role 与 cipher policy。
- TLS backend 与 configured cipher policy。
- negotiated cipher suite name/id。
- secret install event 的 encryption level、方向与 cipher suite。
- packet protection 构造时的 AEAD、hp algorithm、key/nonce/hp key length。
- `chacha20` testcase 是否已由 interop dispatch 放行。

日志应帮助排查：

- testcase 没放行导致 `127`。
- chacha20-only policy 没传入 TLS。
- TLS 没协商到 ChaCha20。
- packet protection 使用了错误 AEAD。
- Initial 被错误改成 ChaCha20。

## 8. 测试设计

### 8.1 Unit tests

新增或扩展单测覆盖：

- cipher suite ops：
  - `0x1301` -> AES-128-GCM / SHA-256 / AES hp / 16-byte hp key
  - `0x1302` -> AES-256-GCM / SHA-384 / AES hp / 32-byte hp key
  - `0x1303` -> ChaCha20-Poly1305 / SHA-256 / ChaCha20 hp / 32-byte hp key
  - unknown cipher suite 返回错误
- Header protection：
  - ChaCha20 hp 使用 sample 前 4 字节 little-endian counter。
  - ChaCha20 hp 使用 sample 后 12 字节 nonce。
  - AES 与 ChaCha20 的 mask 对同一 sample 不应混淆。
  - sample 不足时解包失败或丢弃。
- AEAD：
  - ChaCha20-Poly1305 正常 seal/open。
  - 错误 key、错误 nonce、错误 associated data 均 open 失败。
- Initial 边界：
  - chacha20-only policy 下 Initial 仍使用 AES-128-GCM。
  - Handshake/1-RTT 才使用 TLS negotiated cipher suite。

### 8.2 Integration tests

新增或扩展集成测试覆盖：

- default policy 下普通握手和小文件下载不回退。
- chacha20-only policy 下本地客户端/服务端完成握手和小文件下载。
- fake TLS 模式可显式模拟 chacha20-only negotiated cipher suite。
- v2 + default policy 不回退；是否组合 v2 + chacha20 可作为后续增强，不作为 Step04 首版阻塞。

### 8.3 Interop tests

实现完成后建议执行：

- `make test-unit`
- `make test-integration`
- `make test-interop-chacha20`
- `make interop-chacha20-server`
- `make interop-chacha20-client`
- `make test-interop-handshake`
- `make test-interop-transfer`
- `make test-interop-v2`

如果外部实现的 chacha20 方向不可用，先以 ai-quic 双端 runner smoke 作为本地证据，同时保留对端方向目标用于后续真实互操作验收。

## 9. 不变量

步骤04实现完成后必须保持以下不变量：

- Initial keys 由 QUIC version salt 与初始 DCID 派生，不由 TLS cipher suite 决定。
- TLS cipher policy 不改变 packet number space、ACK/loss、stream、flow control 或 scheduler 行为。
- Header protection key 与 AEAD key/IV 分开派生、分开保存、分开使用。
- Header protection sample 长度和算法来自 cipher suite ops，不能写死为 AES。
- packet protection 所需的 cipher suite id 只来自 TLS callback 或 fake TLS 测试配置。
- `chacha20` testcase 不得通过绕开 TLS 协商来伪造成功。
