### QUIC的qlog事件定义基础约定

#### 双方均应遵守的设计
* **Group ID 的使用规范**
  * 推荐使用QUIC的“原始目的连接ID”（Original Destination Connection ID, 简称 ODCID，即客户端首次联系服务器时选择的CID）作为 qlog 的 `group_id` 字段值。因为这是在整个连接过程中唯一不会改变的值，能够将更复杂的QUIC数据包（如重试 Retry、版本协商 Version Negotiation）与特定的连接关联起来。
  * 建议使用 ODCID 作为 qlog 的文件名或文件标识符，并可根据需要加上观察点（vantagepoint）类型作为后缀（例如：`abcd1234_server.qlog` 代表 ODCID 为 abcd1234 的连接在服务器端的追踪记录）。
* **原始数据包与帧信息的长度计算**
  * QUIC 数据包始终在末尾包含一个 AEAD 认证标签。虽然该标签的长度取决于 TLS 密码套件，但在 QUIC v1 中使用的所有密码套件均采用 16 字节的标签。
  * 在计算与 QUIC 数据包相关的 `RawInfo` 类型字段的长度时，应统一将 AEAD 标签视为大小固定为 16 字节的尾部数据（trailer）。
* **不属于单一连接的事件处理**
  * 某些类型的事件（例如触发值为 `connection_unknown` 的事件）可能无法绑定到特定的 QUIC 连接。
  * 记录系统在处理这些事件时，需在文件大小开销、灵活性、易用性和实现难度之间进行权衡，可选的处理方案包括：
    1. 将它们记录在一个独立的、端点全局（endpoint-wide）的追踪文件中（或使用特殊的 `group_id` 值），不与单一连接关联。
    2. 将它们记录在最近使用的追踪文件中。
    3. 使用额外的启发式方法来进行连接识别（例如，除了连接ID外，同时使用四元组信息）。
    4. 缓存这些事件，直到它们能够被明确分配给某个连接（例如版本协商和重试事件）。
* **数据结构与模式约定**
  * 事件和数据结构必须使用简明数据定义语言（CDDL）及其在 qlog 主规范中的扩展来进行定义。
  * 必须导入并使用主规范中的以下字段：`name`、`namespace`、`type`、`data`、`tuple`、`group_id`、`RawInfo` 以及与时间相关的字段。
  * qlog 模式定义有意保持对序列化格式的不可知性（agnostic），选择何种具体格式纯粹是实现层面的决定。

#### 发送端应遵守的设计
* 客户端（作为初始发送端）在首次联系服务器时负责生成并选择“原始目的连接ID”（ODCID）。发送端应确保该 ODCID 被正确用于其自身 qlog 的 `group_id` 及日志文件的命名中。
* 本章节未对发送端提出其他额外的专属要求，发送端应默认遵守上述“双方均应遵守的设计”中的各项日志记录和计算规范。

#### 接收端应遵守的设计
* 服务器（作为接收端）在处理“不属于单一连接的事件”（如收到未知连接数据）时面临更高的复杂性。接收端应特别关注并实现上述提及的“孤立事件处理策略”（如利用四元组启发式识别、缓存等待分配或使用全局独立日志），以防丢失无法立即识别 CID 的重要网络事件。


### 事件模式定义

#### 双方均应遵守的设计
* **命名空间与模式注册**
  * 必须使用新定义的事件模式在 qlog 中表达核心 QUIC 协议以及选定的扩展。
  * 根据主规范（[QLOG-MAIN]）第8章的要求，正式注册 `quic` 命名空间。
  * 最终正式版的事件模式 URI 为 `urn:ietf:params:qlog:events:quic`。
* **草案版本的模式标识约束**（注：此规则在正式 RFC 发布前有效）
  * 只有最终发布的正式 RFC 的实现才能使用基础 URI (`urn:ietf:params:qlog:events:quic`)。在正式 RFC 存在之前，任何实现**严禁（MUST NOT）**使用此基础 URI 标识自身。
  * 任何事件模式的草案版本实现，**必须（MUST）**在基础 URI 后追加字符串“-”以及对应的草案版本号。例如，第07版草案的标识必须使用 URI：`urn:ietf:params:qlog:events:quic-07`。
  * 命名空间标识符本身（即 `quic`）不受此草案版本号追加规则的影响。

#### 发送端应遵守的设计
* 本章节未对发送端提出独立于接收端的专属要求。发送端在生成并输出 qlog 时，必须严格遵守上述“双方均应遵守的设计”中关于命名空间注册、URI 声明以及草案版本号后缀的标识规范，确保日志元数据的准确性。

#### 接收端应遵守的设计
* 本章节未对接收端提出独立于发送端的专属要求。接收端（如日志分析工具或服务端监控系统）在摄取和解析 qlog 时，应期望并支持解析上述“双方均应遵守的设计”中定义的标准 URI，同时在处理非正式 RFC 版本的日志时，必须能够正确识别和兼容带有“-草案版本号”后缀的 URI。


### QUIC 事件概览

#### 双方均应遵守的设计
* **事件类型与重要性分级**
  * 日志实现必须支持规范中定义的事件名称（`name` 值）及其对应的重要性级别（`Importance`）。事件分为 Core（核心）、Base（基础）和 Extra（额外）三个重要性级别。
  * 核心（Core）事件（例如：`quic:packet_sent`, `quic:packet_received`, `quic:packet_lost` 等）通常是分析协议行为最关键的日志，应优先保证记录。
  * 基础（Base）事件（例如：`quic:connection_started`, `quic:connection_closed`, `quic:stream_state_updated` 等）提供了连接生命周期和状态转换的关键上下文。
  * 额外（Extra）事件（例如：`quic:server_listening`, `quic:mtu_updated`, `quic:frames_processed` 等）提供更详细的内部状态和特定功能的操作细节。
* **数据结构扩展机制**
  * `QuicEventData` 聚合了规范中定义的所有具体 QUIC 事件结构（如 `QUICServerListening`, `QUICConnectionStarted` 等）。
  * 所有的 QUIC 事件都扩展自 qlog 主规范（[QLOG-MAIN]）中定义的 `$ProtocolEventData` 扩展点（使用 CDDL 语法 `$ProtocolEventData /= QuicEventData`）。
  * 为了保证直接的可扩展性，各具体事件的定义必须使用 CDDL 的 “group socket” 语法（即 `$$` 前缀），允许在不修改基础模式的情况下添加每个事件特有的扩展点，正如主规范中所述。

#### 发送端应遵守的设计
* 发送端在记录发送行为相关事件（如 `quic:packet_sent`, `quic:udp_datagrams_sent`, `quic:marked_for_retransmit` 等）时，必须确保使用正确的事件名称和数据结构，以准确反映网络层或传输层的外发动作。

#### 接收端应遵守的设计
* 接收端在记录接收或处理行为相关事件（如 `quic:server_listening`, `quic:packet_received`, `quic:packet_dropped`, `quic:packet_buffered`, `quic:frames_processed` 等）时，必须确保事件类型与其实际处理逻辑（接收、丢弃、缓存或解析）严格对应。


### 连通性事件 (Connectivity events)

#### 双方均应遵守的设计
* **连接启动 (`connection_started`)**
  * 重要性级别：基础 (Base)。
  * 用于记录尝试建立新连接（客户端视角）和接受新连接（服务端视角）。
  * 必须记录本地 (`local`) 和远程 (`remote`) 的端点元组信息。需要注意的是，部分不直接处理套接字（sockets）的QUIC协议栈可能无法记录IP和/或端口信息。
* **连接关闭 (`connection_closed`)**
  * 重要性级别：基础 (Base)。
  * 用于记录连接何时被关闭，通常在发生错误或超时、或者通过应用层主动操作（如竞速多个连接并中止最慢的连接）时触发。
  * 只要满足以下条件之一即可记录：本地端点因空闲超时静默丢弃连接；接收到无状态重置数据包（Stateless Reset）。此后的任何状态变更（如退出closing或draining状态）应使用 `connection_state_updated` 记录。
  * 错误处理：对于未映射到已知错误字符串的内部错误，可将 `connection_error` 或 `application_error` 设置为 "unknown"，并在 `error_code` 中记录原始无编码数值。更细粒度的内部错误码可通过 `internal_code` 字段记录。
  * 日志记录器（Loggers）**应该 (SHOULD)** 尽可能使用它们所能推断出的最准确的触发器 (`trigger`)。
* **连接ID更新 (`connection_id_updated`)**
  * 重要性级别：基础 (Base)。当任何一方更新其当前连接ID时触发，以替代在每次发包/收包时重复记录CID带来的开销。
  * 视角以应用新ID的端点为准。如果是自身更新连接ID，`initiator` 字段记为 "local"；如果收到来自对端的新连接ID，`initiator` 字段记为 "remote"。
* **自旋比特更新 (`spin_bit_updated`)**
  * 重要性级别：基础 (Base)。记录QUIC延迟自旋比特（latency spin bit）的改变。
  * **不应该 (SHOULD NOT)** 在自旋比特被设置但状态值没有实际改变时触发。
* **连接状态更新 (`connection_state_updated`)**
  * 重要性级别：基础 (Base)。用于跟踪QUIC复杂的握手和关闭流程进度。
  * 实现方 **应该 (SHOULD)** 主要记录简化的基础连接状态（`BaseConnectionStates`），在需要更深入调试时才增加更细粒度的状态（`GranularConnectionStates`）。
  * 允许实现方使用更贴合其内部逻辑的自定义连接状态，日志工具 **应该 (SHOULD)** 能够像处理预定义状态一样处理这些自定义状态。
* **元组分配 (`tuple_assigned`)**
  * 重要性级别：基础 (Base)。用于将单一的 `TupleID` 与描述唯一网络元组（如IP、CID等元数据）相关联。
  * 空字符串 (`""`) 是一个有效的 `TupleID`，且是未在事件中明确指定 `tuple` 字段时的默认隐式值（例如初始握手连接默认关联 `""`）。
  * 首次出现某 `TupleID` 表示创建，后续出现表示更新。如果在事件中同时省略了 `tuple_local` 和 `tuple_remote`，则隐式表示该 `TupleID` 已被废弃 (abandoned)。
* **MTU更新 (`mtu_updated`)**
  * 重要性级别：额外 (Extra)。在路径MTU发现 (Path MTU discovery) 过程中，用于指示估计的MTU值发生更新。
  * 可通过 `done` 字段标识由于已找到“足够好”的数据包大小，MTU发现过程是否已结束。

#### 发送端应遵守的设计
* 作为连接的发起方（通常是客户端），在尝试建立连接时应产生 `connection_started` 事件。
* 在主动关闭连接（发送 `CONNECTION_CLOSE` 帧并进入 'closing' 状态）时，应产生 `connection_closed` 事件。此时关闭的原因和触发器对于发送方通常是明确的。

#### 接收端应遵守的设计
* **服务器监听 (`server_listening`)**
  * 重要性级别：额外 (Extra)。
  * 仅当服务器（作为接收端）开始接受连接时触发。需记录监听的IPv4/IPv6地址及端口信息，以及是否强制要求重试（`retry_required`，即拒绝1-RTT的连接建立）。
  * 与连接启动事件类似，部分不直接处理套接字的QUIC协议栈可能无法记录IP/端口。
* 作为连接的接收方，当接收到 `CONNECTION_CLOSE` 帧（并进入 'draining' 状态），或者由于收到无状态重置数据包（Stateless Reset）而在接收端直接丢弃连接时，应产生 `connection_closed` 事件。接收方推断关闭触发器（`trigger`）可能更加困难（即更不透明），但仍需尽力提供最详细的推测。


### 连通性事件 (Connectivity events)

#### 双方均应遵守的设计
* **连接启动 (`connection_started`)**
  * 重要性级别：基础 (Base)。
  * 用于记录尝试建立新连接（客户端视角）和接受新连接（服务端视角）。
  * 必须记录本地 (`local`) 和远程 (`remote`) 的端点元组信息。需要注意的是，部分不直接处理套接字（sockets）的QUIC协议栈可能无法记录IP和/或端口信息。
* **连接关闭 (`connection_closed`)**
  * 重要性级别：基础 (Base)。
  * 用于记录连接何时被关闭，通常在发生错误或超时、或者通过应用层主动操作（如竞速多个连接并中止最慢的连接）时触发。
  * 只要满足以下条件之一即可记录：本地端点因空闲超时静默丢弃连接；接收到无状态重置数据包（Stateless Reset）。此后的任何状态变更（如退出closing或draining状态）应使用 `connection_state_updated` 记录。
  * 错误处理：对于未映射到已知错误字符串的内部错误，可将 `connection_error` 或 `application_error` 设置为 "unknown"，并在 `error_code` 中记录原始无编码数值。更细粒度的内部错误码可通过 `internal_code` 字段记录。
  * 日志记录器（Loggers）**应该 (SHOULD)** 尽可能使用它们所能推断出的最准确的触发器 (`trigger`)。
* **连接ID更新 (`connection_id_updated`)**
  * 重要性级别：基础 (Base)。当任何一方更新其当前连接ID时触发，以替代在每次发包/收包时重复记录CID带来的开销。
  * 视角以应用新ID的端点为准。如果是自身更新连接ID，`initiator` 字段记为 "local"；如果收到来自对端的新连接ID，`initiator` 字段记为 "remote"。
* **自旋比特更新 (`spin_bit_updated`)**
  * 重要性级别：基础 (Base)。记录QUIC延迟自旋比特（latency spin bit）的改变。
  * **不应该 (SHOULD NOT)** 在自旋比特被设置但状态值没有实际改变时触发。
* **连接状态更新 (`connection_state_updated`)**
  * 重要性级别：基础 (Base)。用于跟踪QUIC复杂的握手和关闭流程进度。
  * 实现方 **应该 (SHOULD)** 主要记录简化的基础连接状态（`BaseConnectionStates`），在需要更深入调试时才增加更细粒度的状态（`GranularConnectionStates`）。
  * 允许实现方使用更贴合其内部逻辑的自定义连接状态，日志工具 **应该 (SHOULD)** 能够像处理预定义状态一样处理这些自定义状态。
* **元组分配 (`tuple_assigned`)**
  * 重要性级别：基础 (Base)。用于将单一的 `TupleID` 与描述唯一网络元组（如IP、CID等元数据）相关联。
  * 空字符串 (`""`) 是一个有效的 `TupleID`，且是未在事件中明确指定 `tuple` 字段时的默认隐式值（例如初始握手连接默认关联 `""`）。
  * 首次出现某 `TupleID` 表示创建，后续出现表示更新。如果在事件中同时省略了 `tuple_local` 和 `tuple_remote`，则隐式表示该 `TupleID` 已被废弃 (abandoned)。
* **MTU更新 (`mtu_updated`)**
  * 重要性级别：额外 (Extra)。在路径MTU发现 (Path MTU discovery) 过程中，用于指示估计的MTU值发生更新。
  * 可通过 `done` 字段标识由于已找到“足够好”的数据包大小，MTU发现过程是否已结束。

#### 发送端应遵守的设计
* 作为连接的发起方（通常是客户端），在尝试建立连接时应产生 `connection_started` 事件。
* 在主动关闭连接（发送 `CONNECTION_CLOSE` 帧并进入 'closing' 状态）时，应产生 `connection_closed` 事件。此时关闭的原因和触发器对于发送方通常是明确的。

#### 接收端应遵守的设计
* **服务器监听 (`server_listening`)**
  * 重要性级别：额外 (Extra)。
  * 仅当服务器（作为接收端）开始接受连接时触发。需记录监听的IPv4/IPv6地址及端口信息，以及是否强制要求重试（`retry_required`，即拒绝1-RTT的连接建立）。
  * 与连接启动事件类似，部分不直接处理套接字的QUIC协议栈可能无法记录IP/端口。
* 作为连接的接收方，当接收到 `CONNECTION_CLOSE` 帧（并进入 'draining' 状态），或者由于收到无状态重置数据包（Stateless Reset）而在接收端直接丢弃连接时，应产生 `connection_closed` 事件。接收方推断关闭触发器（`trigger`）可能更加困难（即更不透明），但仍需尽力提供最详细的推测。


### 安全事件 (Security Events)

#### 双方均应遵守的设计
* **密钥更新 (`key_updated`)**
  * 重要性级别：基础 (Base)。
  * 用于记录各类加密密钥的生成和更新。需明确指示密钥类型 (`key_type`)。
  * 对于 1-RTT 密钥的更新，**必须**记录完整的密钥阶段值 (`key_phase`)，数据包头中使用的密钥阶段位仅为该值的最低有效位。
  * `trigger` 字段用于说明触发更新的原因，例如由 TLS 协商产生（`tls`，如 initial, handshake 和 0-RTT 密钥），或者是由远端/本地发起的更新（`remote_update`, `local_update`）。
* **密钥丢弃 (`key_discarded`)**
  * 重要性级别：基础 (Base)。
  * 用于记录不再使用并被安全销毁的密钥，结构和字段要求与 `key_updated` 类似。

#### 发送端应遵守的设计
* 当发送端主动发起 1-RTT 密钥更新时，应记录 `key_updated` 事件，并将 `trigger` 设为 `local_update`。
* 当确认旧密钥不再被发送和确认所需时（如收到对端使用新密钥发送数据的 ACK 后），应记录 `key_discarded` 事件。

#### 接收端应遵守的设计
* 当接收端收到对端使用新 1-RTT 密钥阶段发送的数据包时，应记录 `key_updated` 事件，并将 `trigger` 设为 `remote_update`。
* 在握手完成后或检测到对端密钥更新并验证成功后，应按规范丢弃不再需要的旧阶段密钥，并记录相应的 `key_discarded` 事件。


### 恢复事件 (Recovery events)

#### 双方均应遵守的设计
* **通用设计与扩展性**
  * 该类别中的事件（大部分）被设计为通用的，以支持不同的恢复方法和各种拥塞控制算法。
  * 针对这些事件中的未定义或自定义数据，工具开发者**应该 (SHOULD)** 努力提供支持和可视化（例如，在时间轴可视化上按名称绘制未知的拥塞状态）。
  * 所有的恢复参数设置和指标更新事件，允许包含任意数量的未指定字段，以适应不同的内部恢复策略和算法实现。
* **恢复参数设置 (`recovery_parameters_set`)**
  * 重要性级别：基础 (Base)。
  * 将丢包检测（如 `reordering_threshold`, `time_threshold`, `initial_rtt`）和拥塞控制（如 `initial_congestion_window`, `loss_reduction_factor`）的初始参数分组到单个事件中。
  * 这些设置通常只需记录一次。如果在执行期间发生更改，**可以 (MAY)** 多次触发该事件。
* **恢复指标更新 (`recovery_metrics_updated`)**
  * 重要性级别：核心 (Core)。
  * 记录丢包检测（如 `min_rtt`, `smoothed_rtt`, `pto_count`）和拥塞控制（如 `congestion_window`, `bytes_in_flight`）等可见指标的变更。
  * **应该 (SHOULD)** 将在相同或相近时间发生的所有可能的指标更新分组到一个事件中（例如，不要将同时改变的 `min_rtt` 和 `smoothed_rtt` 拆分为两条记录）。
  * 应用程序**应该 (SHOULD)** 尽量只记录实际更新的指标值，不过为了简化日志记录逻辑，实现方也**可以 (MAY)** 记录与先前完全相同的重复值。
* **拥塞状态更新 (`congestion_state_updated`)**
  * 重要性级别：基础 (Base)。
  * 指示拥塞控制器进入重要的内部新状态并改变其行为（如慢启动、拥塞避免、应用限制、恢复等）。状态的具体名称刻意不作规定，由算法具体实现决定。
  * 如果有多种方式可以触发同一状态变更，**应该 (SHOULD)** 记录 `trigger` 字段（例如指示是因为持续拥塞还是 ECN 标记导致）。如果只能因单一事件触发（例如退出慢启动），则**可以 (MAY)** 省略该字段。
* **数据包丢失与重传标记 (`packet_lost`, `marked_for_retransmit`)**
  * `packet_lost`（重要性：Core）：当丢包检测判定数据包丢失时触发。**推荐 (RECOMMENDED)** 填充 `trigger` 字段（如乱序阈值、时间阈值或 PTO 过期）来消除判定原因的歧义。
  * `marked_for_retransmit`（重要性：Extra）：记录被标记为需要重传的数据（帧数组）。对于不记录完整帧的实现方（如只记录流的偏移和长度），需将内部逻辑转换为规范定义的相应帧结构。

#### 发送端应遵守的设计
* **ECN 状态机追踪**
  * 当发送端探测和验证路径对显式拥塞通知 (ECN) 的支持时，应通过 `ecn_state_updated` 记录 ECN 状态机（如 testing, unknown, failed, capable）的进展。

#### 接收端应遵守的设计
* 本章节事件主要描述本地传输层内部的拥塞与恢复状态机（如 RTT 计算、拥塞窗口调整、丢包判定等）。作为数据包的接收端，也会基于对端发来的 ACK 帧维护自身的恢复参数和指标，因此同样需遵守上述“双方均应遵守的设计”来记录这些内部状态事件。本节对接收端无额外的、特定的交互要求。


### QUIC数据类型定义

#### 双方均应遵守的设计
* **基础字段表示**
  * `QuicVersion`、`ConnectionID`、`StatelessResetToken` 等基础类型统一使用十六进制字符串（hexstring）表示。
  * **IP地址 (`IPAddress`)**：支持人类可读的字符串格式（如 "127.0.0.1" 或 IPv6 格式）或原始字节格式。出于隐私或安全原因，允许使用基于哈希或脱敏的表示形式。
* **元组端点信息 (`TupleEndpointInfo`)**
  * 该结构仅表示四元组的单向/半边（half/direction）。一个完整的元组由两个半边组成（例如：服务器使用特定的目标CID发送到客户端IP+端口；客户端使用不同的目标CID发送到服务器IP+端口）。
  * 记录元组信息的日志结构**应该 (SHOULD)** 包含两个不同的 `TupleEndpointInfo` 实例，分别对应元组的每一半。如果存在重叠或需要跟踪以前的 CID，可以记录多个连接 ID。
* **数据包头部 (`PacketHeader`)**
  * 对于类型为 `initial`、`handshake` 和 `0RTT` 的长头部数据包，其头部中的 `length` 字段必须记录在 qlog 的 `raw.length` 字段中，该值表示数据包编号长度加上有效载荷的长度。
  * 如果固定位和保留位的值无效，可以通过记录 `PacketHeader` 事件的 `raw.data` 字段来捕获这些原始值。
* **帧日志记录与优化 (`QUIC Frames`)**
  * 所有的具体 QUIC 帧都扩展自 `$QuicFrame` 这个 CDDL “类型套接字 (type socket)”，允许扩展以支持额外的帧类型。
  * **填充帧 (`PaddingFrame`)**：为了避免带来沉重的日志开销，实现方**应该 (SHOULD)** 仅发出一个 `PaddingFrame` 事件，并将 `raw.payload_length` 属性设置为该数据包中包含的 PADDING 字节/帧的总数，而不是为每个字节记录一个帧。
  * **确认帧 (`AckFrame`)**：包含包号区间的 `acked_ranges` 数组不需要强制有序（例如 `[[5,9],[1,4]]` 是有效值）。如果区间只包含单个数据包（首尾数字相同，如 `[120,120]`），实现方**应该 (SHOULD)** 将其简化记录为 `[120]`，日志解析工具**必须 (MUST)** 能够处理这两种表示法。
  * **包含长度信息的帧 (`CryptoFrame`, `StreamFrame`, `DatagramFrame`)**：如果这些帧在协议层面上包含长度字段，则**必须 (MUST)** 将其记录在 qlog 的 `raw.length` 字段中。如果协议帧中未显式包含长度字段，实现方**可以 (MAY)** 自行计算实际的字节长度并记录在 `raw.length` 中。
* **错误类型与处理 (`TransportError`, `ApplicationError`, `CryptoError`)**
  * `ApplicationError` 由运行在 QUIC 之上的应用层协议（如 HTTP/3）定义。因此，它被定义为一个 CDDL “类型套接字”，默认只包含 "unknown"。应用层 qlog 定义**必须 (MUST)** 通过扩展 `$ApplicationError` 来添加新的错误名称。
  * `CryptoError`（加密错误）映射自 TLS 警报。它的字符串具有动态组件，格式定义为正则表达式 `crypto_error_0x1[0-9a-f][0-9a-f]`（包含十六进制编码和补零的 TLS 警报描述）。
  * 在记录 `ResetStreamFrame`、`StopSendingFrame` 或 `ConnectionCloseFrame` 时，如果遇到未知的数值型错误码，可以将错误字符串值设置为 "unknown"，并在 `error_code` 字段中记录未经变长整数 (variable-length integer) 编码的原始数值。

#### 发送端应遵守的设计
* 发送端在构建包含连续多个 PADDING 字节的数据包时，必须执行上述合并记录的逻辑，以单个 `PaddingFrame` 和长度值输出日志，减少日志输出的冗余。
* 在生成并发送 `ConnectionCloseFrame`（由于传输层错误导致）时，可以通过 `trigger_frame_type` 字段记录触发该错误的源头帧类型。
* 在关闭连接时，`CONNECTION_CLOSE` 的原因短语通常可以作为 UTF-8 文本记录在 `reason` 字段中。如果发送端发送的是非 UTF-8 字节序列，或者不希望进行解码处理，可以直接记录 `reason_bytes` 原始字节流。发送端**应该 (SHOULD)** 至少记录这两种格式之一，也**可以 (MAY)** 两者都记录或都不记录。

#### 接收端应遵守的设计
* 接收端在解析数据包和帧时需要应对“未知”状态。如果解析出的数据包类型（`packet_type`）未知，可以使用 "unknown" 并在 `packet_type_bytes` 中记录无变长编码的原始数值。
* 如果接收端收到了未知的帧类型，应使用 `UnknownFrame` 结构进行记录，并在 `frame_type_bytes` 字段中记录其未经变长整数编码的原始数值。
* 当接收端收到对方发送的 `ConnectionCloseFrame`，且其中包含无法识别的触发帧类型时，`error` 字段应记录为 "unknown"，并同样在 `error_code` 字段提取并记录未经变长编码的原始数值。


### 安全与隐私注意事项

#### 双方均应遵守的设计
* qlog 主规范（[QLOG-MAIN]）中讨论的安全与隐私注意事项同样适用于本文档。这意味着任何实现记录工具或解析工具时，都必须严格遵守主规范中关于日志数据脱敏、隐私保护（例如对IP地址、连接ID、潜在包含用户数据的载荷进行脱敏处理）以及日志存储和传输安全的通用要求。

#### 发送端应遵守的设计
* 本章节未对发送端提出独立的专属要求。发送端应默认遵守主规范中的通用安全与隐私指南，特别是在决定将哪些网络层面的敏感数据写入本地日志文件时，需注意隐私合规。

#### 接收端应遵守的设计
* 本章节未对接收端提出独立的专属要求。接收端（日志收集、聚合与分析系统）应默认遵守主规范中的通用安全与隐私指南，确保在集中摄取、存储和共享这些网络追踪数据时，拥有适当的访问控制，防止终端用户的隐私或网络拓扑机密发生泄露。


### IANA 注意事项 (IANA Considerations)

#### 双方均应遵守的设计
* **注册表声明与遵循**
  * 本文档在 qlog 主规范（[QLOG-MAIN]）创建的“qlog事件模式URI (qlog event schema URIs)”注册表中注册了一个新条目，所有的协议栈日志实现与分析工具均必须遵循此注册信息。
  * **事件模式 URI (Event schema URI)**：必须使用 `urn:ietf:params:qlog:events:quic`。
  * **命名空间 (Namespace)**：必须使用 `quic`。
  * **描述信息 (Description)**：此类事件专用于 QUIC 传输协议相关的事件定义。
* **合法事件类型列表**
  * 日志实现必须支持并使用属于此注册命名空间下的以下事件类型（Event Types）：
    `server_listening`, `connection_started`, `connection_closed`, `connection_id_updated`, `spin_bit_updated`, `connection_state_updated`, `tuple_assigned`, `mtu_updated`, `version_information`, `alpn_information`, `parameters_set`, `parameters_restored`, `packet_sent`, `packet_received`, `packet_dropped`, `packet_buffered`, `packets_acked`, `udp_datagrams_sent`, `udp_datagrams_received`, `udp_datagram_dropped`, `stream_state_updated`, `frames_processed`, `stream_data_moved`, `datagram_data_moved`, `migration_state_updated`, `key_updated`, `key_discarded`, `recovery_parameters_set`, `recovery_metrics_updated`, `congestion_state_updated`, `timer_updated`, `packet_lost`, `marked_for_retransmit`, `ecn_state_updated`。

#### 发送端应遵守的设计
* 本章节为 IANA 注册要求，未对发送端网络行为提出专门要求。发送端在生成及输出 qlog 时，必须确保日志顶层的元数据（如 schema URI）以及各具体事件的名称，严格符合上述 IANA 注册的字符串常量。

#### 接收端应遵守的设计
* 本章节为 IANA 注册要求，未对接收端网络行为提出专门要求。接收端（包括对端端点、日志收集器和可视化分析工具）在摄取和验证日志时，必须将上述 URI、命名空间和定义的 34 种事件类型视为标准且合法的输入基准。