# 导读：API-CONVENTIONS

源文件：`ai-quic/boringssl/API-CONVENTIONS.md`

## 作用

定义 BoringSSL API 的通用语义，包括错误处理、内存模型、对象初始化/销毁、回调约定和线程安全。

## 重点章节

- `Error-handling`
- `Memory allocation`
- `Object initialization and cleanup`
- `Ownership and lifetime`
- `Thread safety`
- `Callbacks and Closures`

## 对 AI-QUIC 的直接价值

- 规范 `aiq_tls_if` 的返回值、错误映射和资源释放路径。
- 避免连接对象和 TLS 对象出现生命周期错配。
- 为回调接口（如密钥就绪、TP 提取）提供一致的契约语义。

## 快速检索

```bash
rg -n "Error-handling|Memory allocation|initialization|Ownership|Thread safety|Callbacks" ai-quic/boringssl/API-CONVENTIONS.md
```
