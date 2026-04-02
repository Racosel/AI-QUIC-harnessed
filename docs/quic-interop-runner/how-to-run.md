# 代理说明手册：Interop Test Runner (互操作性测试运行器)

## 1\. 核心概述

  * **系统功能：** 通过在不同实现之间运行测试用例，自动生成互操作性矩阵 (interoperability matrices)。
  * **支持的协议：** \* [QUIC](https://www.google.com/search?q=quic.md)
      * [WebTransport](https://www.google.com/search?q=webtransport.md)
  * **注册文件 (包含实现列表及 Docker 镜像)：** \* `implementations_quic.json`
      * `implementations_webtransport.json`
  * **实时结果发布地址：** [https://interop.seemann.io/](https://interop.seemann.io/)

## 2\. 核心运行机制与环境配置
Interop Runner 通过挂载目录和环境变量与 Docker 容器进行交互以执行测试。

* **服务器端 (Server) 配置：**
  * **挂载目录：** 运行器会将 `/www` 目录挂载到服务器的 Docker 容器中，该目录包含一个或多个随机生成的文件。
  * **运行要求：** 服务器需要在 `443` 端口上运行，并提供该目录下的文件服务。
* **客户端 (Client) 配置：**
  * **挂载目录：** 运行器会将 `/downloads` 目录挂载到客户端的 Docker 容器中（初始为空）。客户端需将下载的文件存储至此目录。
  * **请求变量：** 需要下载的文件 URL 将通过 `REQUESTS` 环境变量（以空格分隔）传递。
* **测试验证与退出机制：**
  * **退出状态码：** 传输完成后，客户端容器必须以状态码 `0` 退出（或在发生错误时以状态码 `1` 退出）。
  * **结果校验：** Interop Runner 会验证客户端是否下载了预期的文件且内容匹配。对于某些特定的测试用例，运行器还会检查 pcap 抓包文件，以验证协议级别的合规性。
* **证书与密钥管理 (Crypto/Certs)：**
  * **挂载目录：** 运行器会生成密钥和证书链，并将其挂载到 `/certs` 目录中。
  * **文件加载：** 服务器需从 `priv.key` 加载其私钥，并从 `cert.pem` 加载证书链。


## 3\. 环境与依赖要求

  * **编程语言：** Python 3
  * **Python 依赖安装命令：**
    ```bash
    pip3 install -r requirements.txt
    ```
  * **系统工具：**
      * [Docker](https://docs.docker.com/engine/install/) 和 [docker compose](https://docs.docker.com/compose/)
      * [Wireshark](https://www.wireshark.org/download.html) (版本需为 4.5.0 或更新版本)

## 4\. 执行指令指南 (Running the Interop Runner)

  * **运行 QUIC 互操作性测试：**
    ```bash
    python3 run.py
    ```
  * **运行 WebTransport 互操作性测试：**
    ```bash
    python3 run.py -p webtransport
    ```
  * **自定义参数运行测试：**
      * **参数说明：** `-s` 指定服务器 (server)，`-c` 指定客户端 (client)，`-t` 指定测试用例 (test cases)。
      * **示例指令：**
        ```bash
        python3 run.py -s quic-go -c ngtcp2 -t handshake,transfer
        ```

## 5\. 端点构建规范 (Building an Endpoint)

  * **封装格式：** 每个实现都必须打包为 Docker 镜像。
  * **通信机制：** 测试运行器完全通过 **环境变量 (environment variables)** 和 **挂载目录 (mounted directories)** 与各个实现进行通信。
  * **测试用例传递：** 使用 `TESTCASE` 环境变量传递测试用例。
  * **错误处理强制要求 (CRITICAL)：** 如果实现不支持某个测试用例，必须 (MUST) 以状态码 `127` 退出。这确保了在添加新测试用例时不会破坏现有的实现。
  * **协议特定设置：** 协议相关的设置说明和测试用例定义，请参见 [quic.md](https://www.google.com/search?q=quic.md) 和 [webtransport.md](https://www.google.com/search?q=webtransport.md)。
  * **注册与发布流程：**
    1.  按照 [quic-network-simulator 端点设置指南](https://github.com/quic-interop/quic-network-simulator) 创建 Docker 镜像。
    2.  将镜像发布到 [Docker Hub](https://hub.docker.com)。
    3.  将实现添加到 `implementations_quic.json` 或 `implementations_webtransport.json` 文件中。
    4.  准备就绪后，提交 PR (Pull Request) 以完成添加。

## 6\. 多平台构建要求 (Multi-Platform Builds)

  * **平台限制：** [在线互操作性运行器](https://interop.seemann.io/) 需要 `linux/amd64` 架构的镜像。
  * **跨架构构建：** 如果在不同架构（例如 Apple silicon）上构建，需在 `docker build` 时使用 `--platform linux/amd64`。
  * **推荐构建方案：** 建议使用多平台构建，同时提供 `amd64` 和 `arm64` 镜像。构建命令如下：
    ```bash
    docker buildx create --use
    docker buildx build --pull --push --platform linux/amd64,linux/arm64 -t <name:tag> .
    ```

## 7\. IPv6 支持配置

  * **系统配置：** 要在 Linux 上为模拟器启用 IPv6 支持，需要在主机上加载 `ip6table_filter` 内核模块。
  * **配置命令：**
    ```bash
    sudo modprobe ip6table_filter
    ```

## 8\. 日志记录架构 (Logs)

  * **日志存储位置：** 测试运行器将日志文件保存在 `logs` 目录中（每次运行都会被覆盖）。
  * **目录结构：** 按照 `<server>_<client>/<testcase>/` 结构组织。每个子目录包含：
      * `output.txt`：测试运行器的控制台输出（包含失败原因）。
      * `server/` 和 `client/`：服务器和客户端的日志文件。
      * `sim/`：模拟器记录的 pcaps 抓包文件。
  * **密钥记录规范：** 导出 TLS 密钥的实现应使用 [NSS Key Log 格式](https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS/Key_Log_Format)。`SSLKEYLOGFILE` 环境变量会指向 `logs` 目录中的一个文件。
  * **qlog 支持：** 支持 [qlog](https://github.com/quiclog/internet-drafts) 的实现应将日志文件导出到由 `QLOGDIR` 环境变量指定的目录中。


