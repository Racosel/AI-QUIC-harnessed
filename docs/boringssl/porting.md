# 导读：PORTING

源文件：`ai-quic/boringssl/PORTING.md`

## 作用

说明如何把已有 OpenSSL 代码迁移到 BoringSSL，包括宏判断、已移除能力、常见 API 替代方式。

## 重点章节

- `Major API changes`
- `Optional BoringSSL-specific simplifications`
- `Replacements for CTRL values`
- `Significant API additions`

## 对 AI-QUIC 的直接价值

- 明确哪些 OpenSSL 旧接口在 BoringSSL 中不可用。
- 指导 `tls` 适配层的条件编译方式（优先用特性宏，不滥用实现探测）。
- 减少因 API 兼容误判导致的握手集成故障。

## 快速检索

```bash
rg -n "Major API changes|OPENSSL_IS_BORINGSSL|CTRL|additions" ai-quic/boringssl/PORTING.md
```
