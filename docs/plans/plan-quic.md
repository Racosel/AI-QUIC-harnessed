### 第一阶段：基础协议解析

建立协议栈的物理通信基础，确保能够合法地接收和初步解析 QUIC 数据包，为后续的状态机运转提供安全的数据输入。

* **目标测试用例**：`v2` (QUIC 版本 2 兼容), `ipv6` (IPv6 支持)
* **C 语言模块设计**：
    * **`quic_io_multiplexer`**：封装底层的 UDP Socket 通信。需实现对 `recvmsg`/`sendmsg` (或更高效的 `recvmmsg`/`sendmmsg`) 的支持，正确处理 IPv4/IPv6 双栈绑定，并提取出底层的 ECN 标记位（为后续 ECN 测试做准备）。
    * **`quic_varint`**：实现 QUIC 独有的变长整数 (Variable-Length Integer) 的编码与解码宏/内联函数，要求极高的执行效率。
    * **`quic_packet_parser`**：实现报文头解码器。能够区分 Long Header 和 Short Header，安全地提取出 Version、Source Connection ID (SCID) 和 Destination Connection ID (DCID)。
* **与 xquic 互通测试策略**：
    * 将本端协议栈作为服务端启动。强制 xquic 客户端向本端发送不同版本的 Initial 报文。
    * **验收标准**：协议栈的解码器不得发生内存越界或段错误 (Segmentation Fault)。能够正确提取出对端发来的 CID，并丢弃自身不支持的版本。

### 第二阶段：密码学引擎、安全上下文

实现与 TLS 1.3 的集成，完成密钥推导并建立加密通道。

* **目标测试用例**：`handshake` (基础握手), `chacha20` (限定密码套件)
* **C 语言模块设计**：
    * **`quic_tls_adapter`**：TLS 适配层。基于 OpenSSL 3.2+ 或 BoringSSL 的 QUIC API 提供实现。负责向 TLS 引擎提供握手数据，并从 TLS 引擎提取密钥材料 (Secret)。
    * **`quic_crypto_engine`**：报文加解密核心。实现 Initial 密钥的 HKDF 推导算法、报文首部保护 (Header Protection) 的掩码计算与去除、以及基于 AEAD (AES-GCM 或 ChaCha20-Poly1305) 的数据包载荷加解密。
    * **`quic_transport_param`**：传输参数的编码器与解码器。负责在 TLS 握手的扩展字段中注入和解析 `initial_max_data`、`initial_max_streams_bidi` 等核心参数。
* **与 xquic 互通测试策略**：
    * 进行标准的 1-RTT 握手互通。
    * **验收标准**：通过抓包或 qlog 确认双方成功交换 `HANDSHAKE_DONE` 帧。

### 第三阶段：报文传输、流多路复用

在安全的加密通道内，实现有序、可靠且受控的业务数据双向传输。

* **目标测试用例**：`transfer` (数据传输), `multiplexing` (并发流管控)
* **C 语言模块设计**：
    * **`quic_frame_parser`**：完整的 QUIC 帧编解码器。重点实现 `STREAM`, `ACK`, `MAX_DATA`, `MAX_STREAM_DATA` 等帧的序列化与反序列化。
    * **`quic_stream_manager`**：多路复用流状态机。实现对 `Ready -> Send -> Data Sent` 等状态的严格管控。实现处理网络乱序到达时流的隐式创建逻辑。
    * **`quic_flow_control`**：三维流控管理器。维护连接级别流控、流级别流控以及并发流数量的三重配额体系。
* **与 xquic 互通测试策略**：
    * 进行 1MB 至 10MB 的文件传输测试。
    * **难点排查**：xquic 的接收端流控非常严格。本端作为接收端时，必须实现智能的窗口更新算法（根据数据消化速度及时发送 `MAX_STREAM_DATA`），否则 xquic 会因为被限流而挂起发送，导致传输超时卡死。

### 第四阶段：丢包探测与网络异常恢复

实现 RFC 9002 的核心机制，确保协议栈在恶劣的网络环境下依然能够保证数据的完整交付。

* **目标测试用例**：`handshakeloss`, `transferloss`, `longrtt`, `blackhole`
* **C 语言模块设计**：
    * **`quic_packet_history`**：发送队列记录器。记录已发送但未确认的数据包及其包含的各个帧，用于在发生丢包时重新组装封包。需要为 Initial、Handshake 和 AppData 维护三个独立的包号空间 (Packet Number Space)。
    * **`quic_rtt_estimator`**：RTT 估算器。根据收到 ACK 帧的时间差，平滑计算 Latest RTT, Smoothed RTT (SRTT) 和 RTT Variance。
    * **`quic_loss_detector`**：丢包判定引擎。实现基于时间阈值 (Time Threshold) 和基于包号乱序阈值 (Packet Threshold, 固定为 3) 的双重丢包判定机制。
    * **`quic_timer_wheel`**：高精度时间轮或定时器管理器。负责调度 PTO (Probe Timeout) 探测超时事件和 Idle Timeout 空闲超时事件。
* **与 xquic 互通测试策略**：
    * 在模拟网络中注入 10% - 30% 的随机丢包以及突发的网络中断（黑洞）。
    * **验收标准**：在发生丢包时，本端能够准确触发 PTO 并重传丢失的 STREAM 数据，而不是盲目重传整个报文。在黑洞测试中，协议栈必须能够在其通告的空闲超时时间内准确触发连接断开，避免资源泄漏。

### 第五阶段：边界防护与健壮性验证

提升服务端防御恶意网络攻击（如 DDoS、放大攻击、数据篡改）的能力。

* **目标测试用例**：`retry`, `amplificationlimit`, `handshakecorruption`, `transfercorruption`
* **C 语言模块设计**：
    * **`quic_token_manager`**：地址验证令牌生成器。使用 AEAD 算法生成绑定客户端 IP、端口及时间戳的 Retry Token。
    * **`quic_anti_amplification`**：放大攻击防御阀门。在服务端完成客户端地址验证（即收到合法的 Handshake 数据）之前，强制限制服务端的下行发送总字节数不得超过从客户端接收总字节数的 3 倍。
    * **`quic_error_handler`**：全局异常分发器。
* **与 xquic 互通测试策略**：
    * 强制开启 Retry 机制进行对抗。
    * **难点排查**：当 xquic 服务端下发 Retry 报文时，本端客户端必须能够正确提取 Token，并将自己后续发出报文的 Destination CID 替换为 xquic 在 Retry 报文中指定的新 CID。若状态机处理不当，将无法完成二次握手。在篡改测试中，要求 AEAD 解密失败时静默丢弃，协议栈不得发生崩溃。

### 第六阶段：高级加密特性与0-RTT重连

引入高级加密特性，优化二次建连的时延，并支持长期运行连接的安全密钥更替。

* **目标测试用例**：`keyupdate`, `resumption`, `zerortt`
* **C 语言模块设计**：
    * **`quic_key_updater`**：密钥轮转引擎。监控 Key Phase 位的翻转，实现新老两代密钥的平滑过渡与解密容错窗口。
    * **`quic_session_ticket`**：会话票据存储。持久化保存 TLS 提供的 Session Ticket 及 0-RTT 传输参数。
    * **`quic_0rtt_buffer`**：早期数据缓存区。对于服务端，在完成 1-RTT 校验前，隔离并缓存收到的 0-RTT 应用层数据，以防范重放攻击 (Replay Attack)。
* **与 xquic 互通测试策略**：
    * 验证 0-RTT 数据的收发逻辑。
    * **验收标准**：本端客户端在收到 Ticket 后，二次重连必须能够在 0-RTT 时期发送业务数据。xquic 服务端必须能够正确解密并接受该数据。要求代码层面严格区分 0-RTT 数据流与 1-RTT 数据流的合法作用域。

### 第七阶段：连接迁移

实现 QUIC 最核心的网络层解耦特性，保证移动端网络切换时的会话连续性。

* **目标测试用例**：`portrebinding`, `addressrebinding`, `connectionmigration`
* **C 语言模块设计**：
    * **`quic_cid_manager`**：CID 动态生命周期管理。维护双方下发的 CID 备用池，严格落实带有 `Retire Prior To` 字段的 `NEW_CONNECTION_ID` 控制帧的“先清理后添加”逻辑。
    * **`quic_path_validator`**：路径验证状态机。当底层检测到对端的四元组 (IP/Port) 发生变化时，暂停常规数据发送，触发发送携带随机负载的 `PATH_CHALLENGE` 帧，并在收到对应的 `PATH_RESPONSE` 帧后恢复发送，同时重置 RTT 估算值与拥塞窗口。
* **与 xquic 互通测试策略**：
    * 在传输过程中由测试框架强制改变本端客户端的 IP 地址。
    * **验收标准**：本端客户端必须立即使用备用 CID 组装报文。xquic 服务端在感知到 IP 变化后会下发 `PATH_CHALLENGE`，本端必须在规定的时间内精确回传 `PATH_RESPONSE`。整个迁移过程中应用层数据流不断开。

### 第八阶段：拥塞控制与HTTP3应用

将传输层映射到实际的应用层协议（如 HTTP/3）。

* **目标测试用例**：`goodput`, `crosstraffic`, `ecn`, `http3`
* **C 语言模块设计**：
    * **`quic_cc_engine`**：拥塞控制器抽象层。实现标准的 NewReno 或 CUBIC 算法，管理拥塞窗口 (CWND) 在慢启动 (Slow Start)、拥塞避免 (Congestion Avoidance) 及快速恢复 (Fast Recovery) 状态间的跃迁。
    * **`quic_pacer`**：发送平滑起搏器。基于当前 CWND 和 RTT 计算发送速率，将突发的发送请求在时间轴上打散，防止瞬时流量打爆网络设备缓存。
    * **`h3_framer` & `qpack_engine`**：HTTP/3 语义映射。处理控制流与数据流的分配，实现 QPACK 静态字典压缩机制。
* **与 xquic 互通测试策略**：
    * 使用 `goodput` 和 `crosstraffic` 进行极限带宽与公平性评估。
    * **验收标准**：通过分析 xquic 与本端交互的 `qlog` 日志，确认本端协议栈的拥塞窗口曲线能够合理地应对网络拥塞，且发送速率平滑。最终实现利用 HTTP/3 语义进行大规模并发文件的无误传输。