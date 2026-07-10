# OmniSMS 开发状态

更新时间：2026-07-10（Asia/Singapore）

## 已完成

- 阶段 0 基线调研、SimAdmin GPLv3 兼容性核查、能力评估和目标架构图。
- 硬件基准已按参考仓库校正为 **ESP32-C3 + ML307R-DC**；Air780E/合宙不是原核心项目硬件。
- core 基础逻辑：PDU、长短信合并、号码处理、文本转义、规则与推送请求组装。
- 共享 Web UI 静态源码同步；13 个文件与 ESP32 基准仓库逐文件 SHA-256 相同。
- Linux AT 轮询 MVP、SMTP/HTTP 推送、JSONL 收件箱与 OpenWrt 打包骨架。
- core 来电事件与 `RING/+CLIP` 去重；Linux 已接入轮询响应内的来电提醒。
- 统一 JSON 配置 schema v1、core 解析/序列化、Linux JSON 导入和旧键值兼容导入。
- Linux `AtChannel` 与自注册 `ModemDriver` 已拆分；当前提供 `generic-3gpp` driver，新增
  driver 文件可被 CMake 自动发现。
- Linux 已增加统一 `ModemBackend` 边界：保留 Serial AT/PDU 路径，并新增通过 `mmcli`
  调用 ModemManager D-Bus 的系统管理后端。ModemManager 支持单设备自动发现、多设备显式
  ID/对象路径选择、完整文本短信收发、USSD、SIM 状态、Voice/Call 来电提醒和 modem reset；
  Messaging/USSD/Voice 能力按设备接口动态探测，raw AT 明确报告 unsupported，避免与系统服务
  争抢串口。命令执行不经过 shell，并可注入 fake runner 测试。
- ModemManager 兼容性已按本机 `mmcli 1.18.6` 的帮助、man page 和二进制 key 名复核：修正
  `modem.3gpp.ussd.*` 与 `sms.properties.timestamp` 字段，列表值即使附带 `(received)`/
  `[device]` 注释也会提取规范 D-Bus path；外部进程失败和超时会返回明确错误；新增
  `omnismsd --list-modems` 诊断入口。
- core GSM7/UCS2 短信 PDU 编码与长短信分段；Linux generic-3gpp 已实现 `AT+CMGS`
  提示符/负载发送，并提供 `--send-sms` 最小运行入口。
- `ModemInterface`、`NetworkInterface`、`StorageInterface`、`AudioInterface` HAL 命名已建立。
- core `ForwardRetryQueue` 已持有容量、首轮状态、到期顺序、统一退避和重试上限；Linux
  worker 只负责线程与实际投递。
- Linux 已实现空闲期独立 URC 读取、`+CMT` 直推短信、`+CMTI` 立即补收、USSD CLI、
  `AT+CPIN?` 状态变化与插卡后重新初始化。
- Linux 已内置无第三方 server 依赖的 HTTP/1.1 Web 后端，直接托管原 `webui/`，提供
  Basic Auth、CSRF、静态面板、状态、消息、schema v1 导出、短信发送、USSD 和 AT 终端；
  Web 与轮询线程通过同一 modem mutex 串行访问 AT 通道。`/save` 与 `/import` 已支持原分域表单、
  schema-v1 JSON 和 ESP 旧键值备份，配置通过临时文件、`fsync`、原子重命名写回；推送、SMTP、
  规则、调度和 Web 凭据即时热加载，串口/驱动/监听地址变更明确提示重启。
- Android Gradle application 已建立：`SMS_RECEIVED`/`PHONE_STATE` 进入 Kotlin 平台层，JNI
  直接编译并调用共享 C++ core 完成配置校验、号码/黑名单、规则决策和 10 类 HTTP 推送报文组装，
  WorkManager 持久重试；同时提供 SmsManager 发送、USSD 和前台服务。Android SMTP 已实现
  465 隐式 TLS、其他端口强制 STARTTLS、AUTH LOGIN、证书/主机名校验、MIME Base64 与持久重试。
- Android 主界面已从 Activity 程序化控件堆叠重构为 XML 资源和统一组件样式，复用 Web 的
  `#0E1014` 深色画布、`#E8B23A` 琥珀强调、1px 分隔、2dp 圆角、等宽数据与四态反馈；
  Gradle 构建前自动核对 13 个 Web/Android 设计令牌。API 36 模拟器验证窄屏单列、配置编辑器
  内部滚动、空短信输入错误反馈和 1.3 倍字体无横向溢出，Android lint 为 0 error。
- 原 ESP 管理员短信命令已抽为 core `admin`：三平台共用管理员号码匹配、`SMS:号码:内容`
  解析/校验、任务忙判断和 RESET 防重启风暴策略。ESP 保持原执行行为；Linux 代发短信并通过
  systemd/procd 重拉服务而不擅自重启主机；Android 支持代发，但普通 APK 无设备重启权限时明确拒绝。
- 原 ESP 收发件箱已抽为 core `MessageStore`：固定收件 50/发件 10、正文 320 UTF-8 字节截断、
  ID 单调递增、删除留洞、环形覆盖、newest-first JSON、转发成功标记和最终失败回退均统一。
  ESP 继续使用原 NVS 分区/blob/key 格式；Linux Web 已支持删除、手动重发及多目标全部成功后才标记；
  Android JNI 使用同一状态机并由 SharedPreferences 恢复，WorkManager 完成状态会回写 `fwd`。
- 原 ESP 调度时间语义已抽为 core `scheduler`：有效 epoch、首次启用只建基准、周期到期、
  NTP 回拨保护、任务失败次日重试、本地日/小时计算、心跳防重复和运行满两小时的重启门槛。
  ESP 保号/六任务/心跳/重启已改用 core 判定；Linux 已建立相同运行骨架，支持通知、HTTP、
  SMS、USSD、每日心跳与服务重启，并把 lastRun/本地日状态原子写入配置旁 `.state` 文件。
  Android 由 JNI 调用同一 core 判定，WorkManager 每 15 分钟唤醒并在内部按小时节流，可执行
  蜂窝 HTTP、SMS、USSD 与本地通知，`BOOT_COMPLETED` 恢复调度。Linux/Android eSIM Profile
  切换和 Android 设备级每日重启均明确返回/通知不支持，未伪实现。
- 原短信邮件主题/正文已抽为 core `email`，ESP、Linux、Android 共用同一生成逻辑；Android JNI
  只输出平台无关的邮件内容计划，SMTP 凭据和 TLS I/O 仍由 Android 平台层处理。
- ESP32-C3 + ML307R-DC 完整固件已迁入：app_main、10 个功能组件、Web 源/生成资产、
  sdkconfig、分区表、开发文档、预览和构建工具齐全；基线文件逐文件 SHA-256 相同。
- ESP 完整固件已开始消费共享 core：`idf_sms` 接入号码规范化/黑名单/校验；
  `idf_config`/`idf_push` 接入规则转换、校验和决策；`idf_push` 接入 UTF-8 截断、
  JSON/HTML 转义、URL 编解码、占位符展开及 10 类 HTTP 推送报文组装；
  `idf_sms` 已接入共享 PDU 解码、GSM7/UCS2 编码分段和长短信合并，收发共用
  单个 4096 字节 `PduCodec` 工作缓冲；`idf_push` 的转发、推送和邮件队列均已接入
  core `FixedDueQueue`，保持原固定容量、到期顺序和无堆扩容特性。
- ESP 完整配置已与 core schema v1 双向映射：通用短信/推送/SMTP 字段以及
  WiFi、Web 账号、管理员号码、时间/重启/心跳、保号、蜂窝网络和 6 个调度任务均可 JSON
  导入导出；精简 Linux JSON 按字段合并，不会重置未出现的 ESP 专有配置。原 `/export` 文本格式、
  `/import` 路径、NVS key 和页面不变，新增 `/export?format=json[&full=1]`。
- Ninja/MinGW host 测试 9/9 通过；WSL Linux 完整测试 18/18 通过（含管理员命令、收件箱、调度器、邮件内容、
  配置原子写入/导入、真实 TCP Web server、ModemManager fake-runner，以及 fake `mmcli` 的
  `fork/exec` 设备列举/短信/USSD CLI 端到端测试）；ESP-IDF 5.5.4 `esp32c3` 完整固件构建通过，最新固件 `0x135b40` 字节，app 分区约
  26% 空闲；Web assets 校验通过。
- Linux 全量目标另以 `-Wall -Wextra -Wpedantic` 严格告警选项构建并运行 18/18 测试通过，
  当前新增 backend、fake CLI 和入口代码无编译警告。
- Android API 36 / NDK r28b 已完成 `assembleDebug`，四个 ABI 的 core/JNI 均编译通过；
  Debug APK `app-debug.apk` 为 5,524,879 字节，清单验证为 minSdk 26 / targetSdk 36。
- 根 README 已按大型开源项目首页重构：CI/平台徽章、成熟度提示、能力摘要、快速开始、
  Serial AT/ModemManager 配置、Mermaid 架构、功能矩阵、生产部署、常见问题、模组路线、
  验证状态和贡献入口齐全；Linux 示例与 systemd 统一使用 `/etc/omnisms/omnisms.conf`，
  `/usr/local` 安装的 Web assets 可自动发现；
  按用户要求未使用 imagegen 或 AI 生成插图。

## 进行中

- 阶段 2/3：ESP 实机回归、Linux Serial AT/ModemManager 真模组回归和 Android 真机权限/后台保活回归。

## ESP32 基准迁移状态

| 范围 | 状态 | 证据/缺口 |
|---|---|---|
| 纯 core 逻辑 | 🚧 逐项接回中 | 号码、黑名单、文本工具、规则、推送报文、PDU 收发、长短信合并、固定容量队列、管理员命令、收发件箱、调度到期状态机与完整配置 schema 已由完整 ESP 固件直接消费；具体动作编排仍由 HAL 执行 |
| Web 静态源码 | ✅ 已迁移 | `webui/` 13 个文件与参考仓库逐文件 SHA-256 相同 |
| ESP-IDF 平台组件 | ✅ 完整源码已迁入 | `idf_modem`、`idf_sms`、`idf_web`、`idf_wifi`、eSIM、收件箱等均已导入 |
| 固件入口与构建 | ✅ 已迁移 | app_main、CMake、sdkconfig、分区表和构建/烧录脚本齐全 |
| 可编译/可烧录固件 | 🚧 构建通过 | 完整 `esp32c3` 固件已生成；尚待本仓库版本实机烧录与逐功能验证 |

结论：原 ESP32-C3 + ML307R-DC 完整源码和构建基线已经迁入；当前正在保持行为不变的
前提下逐项替换为共享 core，并准备实机回归。

## 待办

- 阶段 2：在已迁入的 ESP32-C3 + ML307R-DC 完整固件上消费 core，并逐功能实机回归。
- 阶段 3：继续完成 ModemManager 完整通话管理、并发多 modem 运行策略与真实设备回归。
- 阶段 4：Android 真机验证和厂商后台限制适配。
- 阶段 5：按确认项独立实现 SimAdmin 相关增强；独立评估/实现 WiFi Calling。

## 已知问题

- SimAdmin 为 GPLv3，而 omniSMS 当前为 MIT；未决定整体许可证策略前不直接复制其代码。
- Windows 默认 NMake 不可用；host 验证固定使用 Ninja + MinGW，完整 POSIX/Linux 验证使用 WSL。
- 当前 README 的部分端口状态曾高于仓库内实际实现；本次开始按可验证证据校正。
- Linux AT 收发目前通过 fake channel、PDU 向量和无硬件管道测试，尚未在真实模组/运营商网络上验证。
- Linux ModemManager 后端目前通过 fake `mmcli` runner 测试；尚未在运行 ModemManager 的真实
  QMI/MBIM/USB 模组上验证 key-value 输出差异、长短信、运营商 USSD、Voice 来电和多 modem 选择。
- Android APK 已构建，但尚未在真实插卡手机上验证 RECEIVE_SMS、来电号码可见性、USSD、
  多卡选择、各厂商省电策略和最终 UI 观感；Android 10+ 可能隐藏来电号码，必须按设备权限实际确认。
- 完整固件虽已构建通过，但尚未烧录真实 ESP32-C3 + ML307R-DC 硬件，不能仅凭编译宣称实机回归完成。

## 下一步

1. 在真实 ESP32-C3 + ML307R-DC 上烧录当前固件，按功能清单对照原项目回归。
2. 使用真实 AT 模组验证 Linux Web 发短信/USSD、轮询并发与配置热加载；再使用真实
   ModemManager/QMI/MBIM 设备验证短信、USSD、SIM 状态、Voice 来电、多设备选择和 reset。
3. 在插卡 Android 8.0+ 真机验证短信、来电、HTTP/SMTP 重试、多卡和后台保活。
4. 安装 ESP32-S3 Xtensa 工具链后补做 S3 构建验证；保持与 SimAdmin GPLv3 代码隔离。

详细调研见 [2026-07-10-stage0.md](2026-07-10-stage0.md)。
