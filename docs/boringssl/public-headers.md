# 导读：include/openssl 公共头文件

源目录：`ai-quic/boringssl/include/openssl/`

## 作用

该目录是 BoringSSL 公共 API 的权威入口。函数说明通常直接写在头文件注释中。

## 与 AI-QUIC 步骤01关系最直接的头文件

- `ssl.h`、`tls1.h`：TLS/QUIC 握手相关接口。
- `evp.h`、`hkdf.h`：密钥派生和算法抽象。
- `err.h`：错误栈与错误码处理。
- `bytestring.h`：字节序列读写工具。
- `crypto.h`、`base.h`：公共类型与基础设施。

## 检索建议

- 定位符号定义：

```bash
rg -n "SSL_set_quic_transport_params|SSL_get_peer_quic_transport_params|ALPN|SSL_CTX_set_alpn" ai-quic/boringssl/include/openssl
```

- 按头文件快速查看：

```bash
ls -1 ai-quic/boringssl/include/openssl
```

## 说明

- 需要精确 API 语义时，优先查看头文件原注释，再回到实现代码核实行为。
