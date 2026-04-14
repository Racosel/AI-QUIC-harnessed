# 导读：FUZZING

源文件：`ai-quic/boringssl/FUZZING.md`

## 作用

说明如何启用 BoringSSL Fuzz 构建和执行模糊测试，以及语料管理建议。

## 重点章节

- Fuzz 构建参数（`-DFUZZ=1`）
- 运行方式与并行参数
- 语料最小化
- TLS transcript 相关模式

## 对 AI-QUIC 的直接价值

- 可用于后续补充 `tls` 边界输入的健壮性验证。
- 有助于提前暴露罕见输入触发的解析问题。

## 快速检索

```bash
rg -n "FUZZ|libFuzzer|max_len|corpora|TLS transcripts" ai-quic/boringssl/FUZZING.md
```
