# AI-QUIC

AI-QUIC 是本仓库下用于承载后续 QUIC 实现的主目录。

当前阶段只搭建基础承载面，不实现真实的 QUIC 协议逻辑。已固定的目标包括：

- `C11 + CMake` 项目骨架
- in-tree `BoringSSL` 接入
- `demo` 入口空壳
- `interop` 容器入口
- `tests/unittest` 最小 smoke test

当前 `qlog` 输出采用 `qlog 0.3` 的 `JSON-SEQ` 形式：先写一条 trace metadata，再逐事件落盘。
这样即使 interop runner 在中途强制结束进程，已写出的记录仍然是完整可读的。

## 目录

```text
ai-quic/
├── include/ai_quic/
├── src/common/
├── src/tls/
├── src/transport/
├── demo/
├── interop/
└── tests/unittest/
```

## 命名约束

AI-QUIC 的公共符号统一使用 `ai_quic_*` 前缀，包括：

- 函数
- 类型
- 宏
- 二进制名称
- shell 脚本内函数

这一约束从基础骨架阶段开始执行，避免后续再做批量重命名。
