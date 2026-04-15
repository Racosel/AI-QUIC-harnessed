### QUIC Interop 主线梯度计划（易 -> 难）

本文以 `docs/quic-interop-runner/quic-test-cases.md` 的 testcase 为交付牵引，并综合 `docs/refs/lsquic.md`、`docs/refs/mvfst.md`、`docs/refs/xquic.md` 的阶段建议。它不是协议模块设计图，而是“每一阶段该开放什么、暂不做什么、如何判定完成”的执行计划。

#### 计划使用原则

- 先做 `IETF QUIC v1 transport core`，再做 `v2`、`HTTP/3`、完整迁移与其他扩展。
- 先单路径，再做路径验证与完整连接迁移；不要把多路径作为主线前置依赖。
- 先把状态机、定时器、可观测性和测试做对，再谈吞吐、零拷贝、pacing 优化与自适应窗口。
- 每个阶段只开放当前 testcase 必需的协议面；未支持的 testcase 必须明确返回 `127`，不能误报为协议失败。
- TLS 集成必须集中在适配层；版本差异必须尽量收敛到 `dispatcher/codec/key schedule` 边界，而不是散落到 `stream/loss/flow control`。
- 从基础骨架阶段开始，公共函数、内部函数、类型、宏、脚本函数与可执行入口统一使用 `ai_quic_*` 前缀，避免后续批量重命名。
- 单个 testcase 通过不等于阶段完成；每一步还需要满足本文写明的“阶段实现建议”“注意事项”和“验收重点”。

#### 模块映射规则

- `Dispatcher/包头/握手基础` -> 步骤00-01
- `Stream/Flow Control/基础调度` -> 步骤02
- `Version Negotiation/版本兼容/TLS 套件约束` -> 步骤03-04
- `Retry/Token/地址验证/反放大` -> 步骤05
- `ACK/Loss/PTO/Congestion` -> 步骤06
- `Ticket/Resumption/0-RTT/Key Update` -> 步骤07-09
- `Path Validation/NAT Rebinding` -> 步骤10-11
- `HTTP/3/QPACK` -> 步骤12
- `CID 池/Preferred Address/Connection Migration` -> 步骤13

#### 跨阶段硬约束

- 统一以 `engine -> connection -> stream` 为主骨架；连接对象是协议聚合根，但不能把所有逻辑都写进一个无边界巨物。
- `Initial / Handshake / AppData` 三个 packet number space 必须独立维护 ACK、sent map、loss/PTO 与 key 生命周期。
- `CRYPTO` 数据流与应用 `STREAM` 数据流必须分离建模；0-RTT 与 1-RTT 也必须有显式状态区分。
- 从步骤01开始就输出最小可用的 `SSLKEYLOGFILE` 与 `QLOGDIR`；排障时优先依赖日志、qlog、pcap，而不是猜测状态机。
- 每完成一步，都要在 `quic-interop-runner` 环境中做真实互操作验证，不以 AI 生成测试例作为唯一验收依据。

#### 默认阶段退出条件

- 目标 `TESTCASE` 在 runner 中稳定通过，且重复运行不会随机飘忽。
- 该阶段新增的协议语义至少有对应单测、集成测试或可重复故障注入验证。
- 文档、日志与退出码能明确区分“实现错误”和“当前未支持”。
- 未计划在本阶段开放的能力仍保持关闭，不通过跨边界打补丁“顺带实现”。

### 步骤00：Version Negotiation（禁用占位）

- **目标测试例(TESTCASE)**：`TESTCASE="versionnegotiation"`
- **阶段目标**：保留 `unsupported version -> stateless VN reply` 的完整入口，但默认不作为当前发布阻塞项。
- **阶段实现建议**：
  - 在 `dispatcher` 层完成最小不变量解析，不为未知版本创建正式连接对象。
  - 把 Version Negotiation 明确建模成无状态回复路径，和 Retry 一样不污染真实连接状态。
  - 至少保留 `parse invariant`、`is supported version`、`build VN packet` 三类基础接口。
- **注意事项**：
  - VN 包不得回送客户端已提议过的版本。
  - Short Header 报文不应触发 VN。
  - 日志中要能明确区分“未知版本”“普通解包失败”“当前禁用占位”。
- **验收重点**：
  - 代码入口存在、默认关闭、文档明确记录“保留但未验收”。
  - 单测覆盖：未知版本 Initial 触发 VN、畸形 VN 包解析失败、VN 不进入连接对象。
- **前置依赖**：无（占位步骤）。

### 步骤01：握手最小闭环

- **目标测试例(TESTCASE)**：`TESTCASE="handshake"`
- **阶段目标**：完成 `Initial -> Handshake -> 1-RTT` 的最小协议闭环，并在握手后成功下载小文件。
- **阶段实现建议**：
  - 使用统一 `quic_conn` 对象和 `phase` 子状态，不要为首版实现复制 `mini_conn/full_conn` 双体系。
  - 从一开始就按 RFC 边界拆开：三个 packet number space、`CRYPTO` 与 `STREAM` 分离、Initial/Handshake/1-RTT keys 独立安装与丢弃。
  - 内部先用最小帧集打通握手：`PADDING`、`PING`、`ACK`、`CRYPTO`、`CONNECTION_CLOSE`；随后只接入“足够完成小文件下载”的最小 `STREAM` 路径，不提前引入多流、H3、Retry、0-RTT。
  - TLS 适配层只负责产出/消费 `CRYPTO` bytes、安装密钥、回传 TP/ALPN/握手事件；不得直接控制 UDP 发送。
  - 从第一天开始接入最小 `qlog` 与 `SSLKEYLOGFILE`，避免把密钥问题误判成状态机问题。
- **注意事项**：
  - 必须支持 coalesced datagram 的基本处理顺序。
  - 客户端 Initial 最小 `1200` 字节约束必须严格执行。
  - 服务端地址未验证前要受 anti-amplification limit 约束。
  - `handshake completed`、`can_send_1rtt`、`handshake confirmed` 必须分开建模。
- **验收重点**：
  - 单连接完成握手并成功下载小文件。
  - 日志中能看见 `Initial -> Handshake -> 1-RTT` 的明确推进和旧 key 丢弃时机。
  - 至少覆盖反向测试：Initial 太短、缺失 `CRYPTO`、错误 encryption level 的 `CRYPTO` frame。
- **影子回归建议**：从本步开始尽早引入 `handshakecorruption` 的定向排障能力。
- **前置依赖**：步骤00。

### 步骤02：流控与多流传输基础

- **目标测试例(TESTCASE)**：`TESTCASE="transfer"`
- **阶段目标**：建立 `stream manager + flow control + send scheduler` 的最小稳定组合，支撑 1MB 级数据传输。
- **阶段实现建议**：
  - 先做完整的单流发送/接收状态机，再做 stream manager 的 lazy materialization、方向检查、流数限制与流集合管理。
  - 连接级与流级流控要独立建模；接收缓冲按 offset 区间组织，不能假设顺序到达。
  - `MAX_DATA` / `MAX_STREAM_DATA` 应由“应用已消费”驱动，而不是“收到数据立即放大窗口”。
  - 调度策略先追求公平和可解释：控制帧优先于数据帧，流按轮询或简单带权轮询发送，单个流不能长期独占 cwnd。
- **注意事项**：
  - final size 违规必须按协议错误处理。
  - `RESET_STREAM`、`STOP_SENDING` 不能打乱流控状态。
  - 不建议本阶段引入智能接收窗口扩展，先使用固定窗口和清晰接口。
- **验收重点**：
  - 约 1MB 传输期间窗口可稳定增长，传输完成且无死锁。
  - 多流并发时无饿死，ACK 与流控不会互相卡死。
  - 覆盖乱序接收、FIN/final size、`RESET_STREAM`、流数超限、窗口更新时机。
- **影子回归建议**：步骤02之后尽早挂上 `ipv6`；`multiplexing` 可作为步骤02到步骤12之间的桥梁测试。
- **前置依赖**：步骤01。

### 步骤03：QUIC v2 协商与兼容

- **目标测试例(TESTCASE)**：`TESTCASE="v2"`
- **阶段目标**：在不复制整套连接状态机的前提下，把版本差异收敛到少数边界模块，并完成 v2 路径兼容。
- **阶段实现建议**：
  - 引入 `quic_version_ops` 或等价表驱动结构，把版本差异压缩到 Initial salt、长首部细节、版本支持列表、版本相关 codec/key schedule。
  - 保持同一套 `connection/recovery/stream/flow control` 主逻辑，避免为了 v2 再复制一套 transport 状态机。
  - 将 `tls_init(version, odcid)`、`tls_reset_initial(version, odcid)` 一类接口做成版本感知的集中入口。
- **注意事项**：
  - 不要把版本判断散落到 frame 语义、流控和 recovery 高层逻辑中。
  - 如果当前阶段还不准备做完整 v2，也应先搭好“版本表驱动”的骨架。
- **验收重点**：
  - v1 正常握手与下载不回退。
  - 从支持版本集合中正确协商到 v2 并完成小文件下载。
  - 未知版本仍走无状态 VN 路径。
- **前置依赖**：步骤02。

### 步骤04：密码套件约束能力

- **目标测试例(TESTCASE)**：`TESTCASE="chacha20"`
- **阶段目标**：验证 TLS 套件策略可以独立变化，而 transport 状态机保持不变。
- **阶段实现建议**：
  - 把密码套件约束集中在 TLS 适配层或等价配置层，不要让 `connection/recovery/stream` 感知“当前是 AES 还是 ChaCha20”。
  - 用表驱动或独立 ops 描述 AEAD、header protection、HKDF 派生参数，避免散落的 `if (chacha20)`。
  - Header protection 与 payload AEAD 必须是两个独立对象。
- **注意事项**：
  - Initial secret 由 QUIC version salt 决定，不随 TLS 协商套件变化。
  - tag 长度、nonce 长度、header protection 采样规则必须来自当前 cipher 配置，不能写死。
  - 本阶段不应引入新的 transport 行为，只改 TLS 配置和兼容性。
- **验收重点**：
  - 强制 ChaCha20 后握手与下载仍成功。
  - 单测覆盖 AEAD 加解密、header protection/unprotection、错误 key/nonce 的失败路径。
- **前置依赖**：步骤03。

### 步骤05：Token 与地址验证链路

- **目标测试例(TESTCASE)**：`TESTCASE="retry"`
- **阶段目标**：把 Retry、token 校验、地址验证、anti-amplification 与 CID 一致性串成一条完整链路。
- **阶段实现建议**：
  - 把 token 服务做成独立模块，输入至少绑定 client 地址、ODCID、时间戳/有效期和防伪 MAC。
  - Retry 尽量在 dispatcher 层、创建连接之前完成校验与决策。
  - 明确区分 `Retry token`、`NEW_TOKEN`、`resumption/application token`，不要共用一套语义含糊的结构。
  - 把 `original_destination_connection_id`、`retry_source_connection_id` 与当前 CID 一致性校验纳入 transport parameters 检查链。
- **注意事项**：
  - 地址未验证前，发送预算必须和收包字节数联动，重传与 coalescing 也要正确计账。
  - Retry 本身应是无状态路径，不应提前占用真实连接资源。
  - Retry 不是孤立特性，它和地址验证、Initial key 复派生、CID 切换是一条链。
- **验收重点**：
  - 服务端触发 Retry 后，客户端能携带 token 完成后续握手。
  - 覆盖 token 篡改失败、client 地址变化导致 token 失效、ODCID 不匹配。
  - 明确验证 `ODCID / Retry SCID / current DCID` 的一致性。
- **影子回归建议**：从本步开始跟进 `amplificationlimit`。
- **前置依赖**：步骤04。

### 步骤06：握手丢包恢复能力

- **目标测试例(TESTCASE)**：`TESTCASE="multiconnect"`
- **阶段目标**：建立握手阶段独立的 ACK/Loss/PTO 体系，并在多连接、丢包环境下保持握手收敛与资源可控。
- **阶段实现建议**：
  - 以 RFC 9002 为准，先把 Initial/Handshake 两个空间的 sent map、ACK state、PTO 与 `CRYPTO` 重传单独做扎实。
  - 统一 outstanding packet 列表，至少记录 `packet number`、`packet number space`、`sent time`、`ack-eliciting`、`in-flight`、frame 元信息。
  - ACK 驱动下更新 RTT、bytes-in-flight、loss marking；再接上 PTO timer 与 probe packet。
  - 定时器模型建议采用“连接内多逻辑 timer，event loop 只注册最近唤醒点”的结构。
- **注意事项**：
  - 不要把握手阶段 ACK/Loss 合并偷懒；三个 packet number space 必须独立。
  - `CRYPTO` 重传不能复用普通 `STREAM` 的 final size 语义。
  - PTO 探测包也要服从 anti-amplification 限制。
  - 本阶段先追求 recovery 正确，不要抢做激进拥塞控制。
- **验收重点**：
  - 多连接在丢包环境下仍可稳定完成握手并下载文件。
  - `Initial ACK`、`Handshake PTO`、半开连接回收、CID 表解绑都能正确收敛。
  - 推荐使用固定随机种子、指定丢包模式和重复跑 N 次来验证稳定性。
- **影子回归建议**：把 `handshakeloss`、`longrtt`、`blackhole` 作为步骤06/07的长期回归。
- **前置依赖**：步骤05。

### 步骤07：会话恢复路径

- **目标测试例(TESTCASE)**：`TESTCASE="resumption"`
- **阶段目标**：先实现恢复握手本身，不把 0-RTT 混进同一阶段。
- **阶段实现建议**：
  - ticket store 挂在 endpoint 或应用会话缓存中，而不是临时连接对象里。
  - TLS 适配层负责 ticket 的存储、加载、过期与绑定策略；连接对象只接收 `resumption accepted/rejected` 事件。
  - 明确保存 ticket identity、PSK/resumption secret、ALPN、可复用 TP 子集、应用参数与过期时间。
- **注意事项**：
  - 不要复用旧连接的 stream/cwnd/flow state 污染新连接。
  - 恢复成功不等于 0-RTT 成功；两者必须分阶段实现。
  - 即使恢复成功，也必须重新建立本次连接的 1-RTT 密钥并重新校验对端 TP。
- **验收重点**：
  - 首次连接获取 ticket，第二次连接确实走恢复路径而不是普通全握手。
  - 覆盖过期 ticket、ALPN 不匹配、应用参数不匹配等拒绝分支。
- **前置依赖**：步骤06。

### 步骤08：0-RTT 与重放防护

- **目标测试例(TESTCASE)**：`TESTCASE="zerortt"`
- **阶段目标**：在恢复握手基础上引入 early data 通路，并为接受/拒绝/回退建立显式状态机。
- **阶段实现建议**：
  - 0-RTT 发送队列必须与普通 1-RTT 应用发送队列分开建模。
  - transport 层显式暴露：`can_send_early_data`、`early_data_accepted`、`early_data_rejected`、`can_resend_as_1rtt`。
  - 0-RTT 被拒绝后，能够清理未确认 0-RTT frame，把对应 outstanding packet 标记为 lost，并决定是否按 1-RTT 重发。
  - 应用层只有显式标记为“幂等/可重放”的请求才能走 0-RTT。
- **注意事项**：
  - 0-RTT 与 1-RTT 共享 application data packet number space，但 key 生命周期和接受语义不同，不能偷懒混成同一条发送状态。
  - transport parameters 或应用参数不兼容时，可能出现“恢复成功但 0-RTT 被拒绝”的情况。
  - 0-RTT key 不能过早丢弃。
- **验收重点**：
  - 二次连接可在 0-RTT 阶段请求剩余文件且行为符合预期。
  - 至少覆盖三条路径：0-RTT accepted、0-RTT rejected but resendable、0-RTT rejected and not resendable。
- **影子回归建议**：从本步开始，逐步把 `transferloss`、`transfercorruption`、`goodput`、`crosstraffic`、`ecn` 挂到长期回归中。
- **前置依赖**：步骤07。

### 步骤09：密钥轮换

- **目标测试例(TESTCASE)**：`TESTCASE="keyupdate"`
- **阶段目标**：在应用数据空间内完成 key phase 切换，而不是“再做一次握手”。
- **阶段实现建议**：
  - 把 key phase 状态抽成独立子结构，至少记录当前发送 key phase、下一个预期接收 key phase、首个使用新 phase 的包号、old/new key 并存窗口。
  - read path 要能同时理解当前 phase 与 next phase；write path 要能按当前 phase 构造 short header。
  - 密钥更新应表现为 recovery 与 TLS/crypto 模块协作的显式事件，而不是“见到包头位变化就顺手切换”。
- **注意事项**：
  - `keyupdate` 发生在同一 `AppData` packet number space 中，ACK/loss accounting 不能因为换 key 丢上下文。
  - 本端发起的 key update 在确认前不得再次发起新的 key update。
  - 必须区分：新 phase 的乱序包、旧 phase 的滞后包、next key 尚未安装的包。
- **验收重点**：
  - 连接早期触发密钥更新后传输不中断。
  - 覆盖本端发起、对端发起、旧新 key 乱序并存、更新中发生 PTO/重传。
- **前置依赖**：步骤08。

### 步骤10：端口重绑定路径验证

- **目标测试例(TESTCASE)**：`TESTCASE="rebind-port"`
- **阶段目标**：把 NAT rebinding 的最小子集实现为“路径检测 + 路径验证 + 状态连续性”，而不是重建连接。
- **阶段实现建议**：
  - 在连接中维护显式 path manager，至少包含 `active path`、`validating path`、`PATH_CHALLENGE` 数据和 path validation timer。
  - 新路径检测应由四元组变化触发，而不是只看 CID 变化。
  - 新路径首次出现先发 `PATH_CHALLENGE`，验证通过前限制发送；切换主路径的条件写成可测试的纯函数。
- **注意事项**：
  - 即使当前只支持单路径，path 抽象也必须先做对。
  - 路径切换不应重置 packet number、stream state、loss state 与流控状态。
  - 路径挑战超时建议使用独立退避，不直接复用旧路径 RTT。
- **验收重点**：
  - 握手后端口变化不会新建连接对象，也不会直接导致连接断裂。
  - `PATH_RESPONSE` 到达后可切换到新路径；超时后可回退或失败。
  - 日志/qlog 能明确标出 path ID 与四元组变化。
- **前置依赖**：步骤09。

### 步骤11：地址重绑定路径验证

- **目标测试例(TESTCASE)**：`TESTCASE="rebind-addr"`
- **阶段目标**：在更严格的安全约束下复用步骤10的 path validation 基础设施，支撑 IP 地址变化后的连接延续。
- **阶段实现建议**：
  - 把地址变化与端口变化统一抽象成 `peer_address_changed(old, new)` 事件，让 path manager 统一处理。
  - 进入迁移判断前，显式检查 `handshake confirmed`、`disable_active_migration`、未知 server 地址包处理等 gate。
  - 仍采用“一个 active path + 一个 validating path”的简化模型，不急于做复杂多候选路径并发。
- **注意事项**：
  - 地址变化不能只靠“更新 sockaddr”糊过去。
  - client 对来自未知 server 地址的普通包应默认丢弃。
  - 若 preferred address 主动迁移尚未实现，也至少要把“未知地址丢弃”和“新地址 path validation”两条被动路径做对。
- **验收重点**：
  - 地址变化后仍可维持连接并继续传输。
  - 验证 `anti-amplification`、`disable_active_migration`、path validation 失败回滚等分支。
- **前置依赖**：步骤10。

### 步骤12：HTTP/3 收敛验证

- **目标测试例(TESTCASE)**：`TESTCASE="http3"`
- **阶段目标**：把 H3 作为 transport 基本稳定后的上层收敛验证，而不是 transport 未稳时的主要调试入口。
- **阶段实现建议**：
  - 明确拆层：`quic transport`、`http3 session`、`qpack encoder/decoder`。
  - 开发顺序建议先建立控制流、SETTINGS、QPACK encoder/decoder stream，再做 request stream 的 `HEADERS/DATA`，最后做并发请求和错误映射。
  - QUIC 内核只通过 stream 接口向上暴露可靠字节流、流创建、`RESET_STREAM`、`STOP_SENDING` 等能力。
- **注意事项**：
  - 不要让 H3 反向污染 transport 内核。
  - 这一步最容易暴露的是 transport 的流状态 bug，而不是 H3 语义本身。
  - 在 transport 尚未稳定前，不要过早优化 QPACK 或优先级。
- **验收重点**：
  - 并行 HTTP/3 请求与文件传输正确完成。
  - 控制流不会阻塞请求流，QPACK 状态在多请求间保持一致。
  - 能明确区分 transport error、application error、QPACK state error。
- **前置依赖**：步骤11。

### 步骤13：完整连接迁移

- **目标测试例(TESTCASE)**：`TESTCASE="connectionmigration"`
- **阶段目标**：在已有 path validation 基础上，把 CID 池、preferred address、旧路径回收和迁移期间的数据面连续性一起收口。
- **阶段实现建议**：
  - 明确拆成四条子能力：`NEW_CONNECTION_ID / RETIRE_CONNECTION_ID` 生命周期、`active_connection_id_limit`、preferred address 触发条件、旧路径晚到包处理。
  - 迁移建立在 path manager 之上，但不等于简单更换 `sockaddr`；必须伴随新的路径对象、CID 选择、PATH_CHALLENGE 与旧 CID 生命周期变化。
  - 即使第一版还不做 multipath，也至少实现“单活动路径迁移”，并把失败计数、回退和资源回收做完整。
- **注意事项**：
  - 没有足够 peer CID 时，不应允许完整迁移。
  - 迁移期间 outstanding packet、ACK/loss accounting、RTT 采样与 pacing 归属要定义清楚。
  - 若前面的 Retry、CID 生命周期、路径验证没有打牢，这一步很容易出现“看似迁移问题、实际是 CID bug”的假象。
- **验收重点**：
  - 客户端迁移到新路径后连接持续可用，应用层数据不中断。
  - 旧路径资源最终可回收，新路径 RTT/loss 能独立收敛。
  - 覆盖成功迁移、path validation 超时、连续多次迁移失败等场景。
- **前置依赖**：步骤12。

### 扩展验证挂靠建议

以下条目不在 `docs/quic-interop-runner/quic-test-cases.md` 当前主线清单内，但建议作为“影子回归”逐步挂靠，不影响步骤00-13 的主线验收：

- `handshakecorruption`、`amplificationlimit`：从步骤01/05 开始跟进。
- `handshakeloss`、`longrtt`、`blackhole`：挂在步骤06/07。
- `transferloss`、`transfercorruption`、`goodput`、`crosstraffic`、`ecn`：建议从步骤08 之后持续跟进。
- `ipv6`：步骤02 之后尽早加入，避免后期把地址族兼容性和迁移问题叠在一起。
- `multiplexing`：可作为步骤02 与步骤12 之间的桥梁测试，用于提前发现“多流调度没问题，但 H3 控制流优先级不对”的问题。

### 附录：开发执行要求

#### 1. Interop 运行与端点契约（来源：`docs/quic-interop-runner/how-to-run.md`）

- Interop Runner 通过环境变量和挂载目录与容器交互。
- 服务端容器必须在 `443` 端口提供服务，并处理挂载到 `/www` 的文件目录。
- 客户端容器需将下载结果写入 `/downloads`，并通过 `REQUESTS` 环境变量接收下载 URL 列表。
- 测试用例通过 `TESTCASE` 环境变量传递。
- 证书与密钥由运行器挂载到 `/certs`，服务端需从 `priv.key` 和 `cert.pem` 加载。
- 实现必须以 Docker 镜像封装；在线互操作环境要求支持 `linux/amd64`。
- 若在非 `amd64` 主机构建镜像，需使用 `docker build --platform linux/amd64` 或 `docker buildx` 多平台构建。
- 在 Linux 上执行 IPv6 相关测试前，应先加载 `ip6table_filter` 模块：`sudo modprobe ip6table_filter`。

#### 2. 容器退出码硬约束（来源：`docs/quic-interop-runner/implement-requirements.md`）

- 退出码 `0`：测试任务成功完成。
- 退出码 `1`：执行出错（运行异常、传输中止、协议验证失败）。
- 退出码 `127`：当前实现不支持该 `TESTCASE`。
- 退出码契约用于区分“失败”和“未支持”，必须严格遵守。

#### 3. 测试与观测要求（来源：`docs/quic-interop-runner/how-to-run.md`、`docs/plans/plan-general.md`）

- 互操作测试可使用 `python3 run.py`，或用 `-s`、`-c`、`-t` 指定 `server/client/testcase` 组合。
- 日志目录固定为 `logs/<server>_<client>/<testcase>/`，关键排查入口：
  - `output.txt`：运行器控制台输出与失败原因。
  - `server/`、`client/`：端点日志。
  - `sim/`：模拟器 pcap 抓包。
- TLS 密钥日志应通过 `SSLKEYLOGFILE` 导出，qlog 文件应输出到 `QLOGDIR` 指定目录。
- 开发过程需从早期接入 qlog，保障失败时可追踪。
- 每完成一步实现，都应在 `quic-interop-runner` 环境中验证，不以 AI 生成测试结果作为唯一验收依据。

#### 4. 质量门禁要求（来源：`docs/mega-linter/config.md`、`docs/mega-linter/run-locally.md`、`docs/plans/plan-general.md`）

- MegaLinter 配置优先级：环境变量（ENV）高于 `.mega-linter.yml`。
- 本地运行前置依赖：NodeJS + Docker/Podman。
- 本地运行方式：`mega-linter-runner` 或 `npx mega-linter-runner [OPTIONS]`。
- 需要自动修复时可启用 `--fix`；启用后仍需执行互操作测试与核心用例复验，避免仅通过格式化掩盖行为问题。
- 建议将代码规范检查与运行时检查结合（如 MegaLinter + Sanitizer）作为质量门禁。

#### 5. 开发流程纪律（来源：`docs/plans/plan-general.md`）

- 每轮改动优先聚焦单一模块，降低问题定位复杂度。
- 定期更新项目日志与完成状态标记，并记录“如何判定已完成”。
- 定期清理无效文件，并检查是否存在违反既定流程的操作。
- 对于资料缺失或工具不可用场景，必须先提出缺失项与补充请求，再继续推进实现。
