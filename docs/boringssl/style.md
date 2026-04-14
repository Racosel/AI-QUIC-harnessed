# 导读：STYLE

源文件：`ai-quic/boringssl/STYLE.md`

## 作用

给出 BoringSSL 开发和提交时的代码风格规则，用于保持可读性与一致性。

## 重点章节

- `Language`
- `Formatting`
- `Integers`
- `Naming`
- `Return values`
- `Parameters`

## 对 AI-QUIC 的直接价值

- 统一 `ai-quic/boringssl` 周边适配代码（尤其是 `tls` 层桥接代码）的风格。
- 降低后续同步上游补丁时的冲突成本。
- 避免因局部风格差异干扰问题定位。

## 快速检索

```bash
rg -n "Language|Formatting|Integers|Naming|Return values|Parameters" ai-quic/boringssl/STYLE.md
```
