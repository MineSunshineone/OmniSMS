# 端口开发指南

OmniSMS core 是纯 C++17 逻辑库；把它带到一个新平台 = 实现
`core/include/omnisms/hal.h` 汇总的 HAL + 平台自己的装配代码。

## 设计原则

- **抽象层切在"短信事件"，不切在 AT 命令**。`SmsEvent` 是 core 的统一输入：
  串口 AT 平台用 core 的 `PduDecoder` + `ConcatAssembler` 生产它；
  ModemManager / Android Telephony 平台由系统直接给出完整短信，跳过 PDU 组件。
- “事件层是 core 边界”不代表把所有模组 AT 差异写进平台入口。串口平台内部仍分为
  `AtChannel`（物理串口）和 `ModemDriver`（厂商/型号命令差异），业务入口只消费统一结果。
- core 不创建线程、不睡眠、不读钟。时间通过参数注入（`now_us`），
  并发由端口负责（core 函数可重入，共享对象由调用方串行化）。
- 无异常设计：所有可失败操作以返回值表达，可 `-fno-exceptions` 编译。

## HAL 接口

| 接口 | 语义 | 参考实现 |
|---|---|---|
| `ModemInterface` | `SmsEvent` / `CallEvent` 回调；短信发送；可选 USSD | Linux: 串口 AT / ModemManager |
| `HttpClient` | 同步 HTTPS 请求，推送出口 | Linux: libcurl；ESP: esp_http_client |
| `StorageInterface` | 配置持久化 | Linux: 文件；ESP: NVS |
| `NetworkInterface` | 联网状态与本机地址 | NetworkManager / ESP WiFi / Android Connectivity |
| `AudioInterface` | WiFi Calling/通话音频能力探测 | Linux/Android；ESP 返回不支持 |
| `Logger` | 单行日志 | Linux: stdout/journald；ESP: 环形缓冲 |
| `Clock` | 单调微秒钟 | steady_clock / esp_timer |

## 硬件门槛

- C++17 工具链；POSIX `regex.h`（规则引擎用；ESP-IDF newlib 自带）
- ≥300KB 可用 RAM（std::string 风格 + TLS 连接缓冲）
- mbedTLS 级别 TLS（HTTPS 推送）

达不到门槛的芯片（ESP8266、Air101 等）明确不支持，不做裁剪版分裂逻辑。

## Linux modem driver

Linux driver 位于 `ports/linux/src/drivers/`。当前 `generic_3gpp.cpp` 实现标准
`CMGF/CNMI/CMGL/CMGD/CLIP` 流程；物理串口实现在 `at_channel.cpp`。

新增模组型号时只需新增一个 `src/drivers/<name>.cpp`：

1. 实现 `ModemDriver`。
2. 在该文件内调用 `register_modem_driver("<name>", factory)`。
3. 不修改 core、Linux `main.cpp` 或 CMake；构建会自动发现 `drivers/*.cpp`。

driver 单测使用假的 `AtChannel`，不依赖真实串口。配置中的 `modem.driver` 选择实现。

## Linux modem backend

`ports/linux/include/omnisms/linux/modem_backend.h` 是 Linux 业务入口消费的统一边界：

- `generic-3gpp` 等名称创建 Serial AT backend，再由 backend 组合 `AtChannel` 与对应
  `ModemDriver`；PDU 编解码和长短信分段继续复用 core。
- `modemmanager` 创建系统管理 backend，通过 `mmcli` 调用 ModemManager D-Bus；系统已经给出
  完整短信，因此跳过 PDU 解码。
- backend 必须显式报告 `pduSms`、`textSms`、来电、USSD、SIM 热插拔、raw AT 和 reset 能力。
  未声明的操作返回 unsupported；例如 ModemManager 拥有设备时不会开放 raw AT 终端。
- ModemManager 初始化会分别探测 Messaging、3GPP-USSD 和 Voice 接口。Messaging 是守护进程
  工作的必要能力；USSD 与 Voice 按具体 modem 动态启用。Voice backend 轮询 incoming
  `ringing-in`/`waiting` Call 对象并转换为统一来电事件，不把 D-Bus 对象泄漏到 core。

ModemManager 的 `endpoint` 可使用 `auto`、数字 modem ID 或完整 D-Bus 对象路径。`auto` 仅在系统
发现一个 modem 时成功；存在多个设备时必须显式选择，避免从错误的 SIM 发送短信。后端测试通过
注入 `CommandRunner` 模拟 D-Bus 结果，并通过构建出的 fake `mmcli` 覆盖真实 `fork/exec`、参数
引用、设备列举、短信与 USSD CLI 管道，不依赖 D-Bus 服务或真实硬件。用户可运行
`omnismsd --list-modems` 获取规范对象路径。

## 验收

跑通 `tests/`（host 或目标机），再用真实/构造 PDU 走一遍
解码 → 黑名单 → 规则 → 推送管道（Linux 端口的 `--test-pdu` 是现成范例）。
