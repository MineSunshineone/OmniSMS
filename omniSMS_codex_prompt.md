# omniSMS 开发提示词（给 Codex）

## 一、项目背景与总体目标

你正在开发 **omniSMS** 项目，核心理念是"**一份代码，多个设备**"。

参考仓库（只读参考，不直接覆盖）：
- `E:\GitHub-Repo\sms_forwarding`：现有 **ESP32-C3 + ML307R-DC** 短信转发项目，**其 Web UI 样式与全部功能是基准线，必须 100% 保留，不允许回退**。
- `https://github.com/3899/SimAdmin`：从中评估 Linux 蜂窝设备管理、ModemManager、短信/通话、eSIM、通知与自动化等有价值能力，按需借鉴，不是整体合并。

目标：在保持原有样式与功能完全不变的前提下，将短信转发能力扩展到多类设备，并引入 WiFi Calling 等增强功能。项目基本骨架已搭建好，你的工作是在骨架上填充实现。

## 二、设备分类与目标平台

分为两大类：

**A. 嵌入式设备（MCU 类小板子）**
- ESP32-C3 + ML307R-DC（参考仓库当前完整支持，作为硬件与功能回归基准）
- ESP32、ESP32-S3、ESP32-C3 等同系列芯片 + AT 模组（编译目标可切换，外设引脚可配置）

### 蜂窝模组与小板支持路线

不以“整个品牌兼容”作模糊承诺，而是按模组系列、运行方式和能力声明逐项验证：

| 优先级 | 品牌/系列 | 目标平台与接入方式 |
|---|---|---|
| P0 | ML307R-DC | ESP32-C3 基准；先完成全部功能实机回归 |
| P1 | 移远 EC200U、EC800M/EC800G | ESP/MCU 使用独立 Quectel AT driver |
| P1 | 芯讯通 A7670、SIM7600 | MCU 使用 AT；Linux 优先 ModemManager/QMI/MBIM |
| P1 | 合宙 Air780E/Air780EPM | 先支持外置 AT 模式；独立 LuatOS/CSDK 作为单独平台端口评估 |
| P2 | 广和通 L610/L716、FM650 | MCU AT 或 Linux ModemManager，按具体接口实现 |
| P2 | 有方 N58/N720 | 独立 Neoway AT driver |
| P3 | 移远 BG95/BG96、SIM7070 | NB-IoT/Cat-M；先核实运营商短信/语音能力再实现 |
| P3 | 移远 RM500Q/RM520N 等 5G | Linux ModemManager/QMI/MBIM，不作为 ESP 基准 |

- 合宙 Air780E 作为外置 AT 模组时复用 AT backend；作为独立 LuatOS/CSDK SoC 时必须提供单独的
  Network/Storage/System 平台适配，不把合宙 SDK 逻辑写入 core，也不冒充 ESP32 端口。
- 每个 driver 必须声明并测试能力集合：PDU 短信收发、`+CMT/+CMTI`、来电 `RING/+CLIP`、
  USSD、SIM 热插拔、PDP/APN、模组重启、eSIM 和语音/VoLTE。未支持能力必须明确返回 unsupported。
- Linux USB/miniPCIe 模组优先使用 ModemManager/QMI/MBIM，避免系统服务与本项目争抢 AT 串口。

**B. 富系统设备（Linux / Android）**
- Linux：带 4G 模块、可插 SIM 卡的随身 WiFi（棒子/UFI 类，常见为 ARM + OpenWrt/Debian 精简系统）
- Linux：x86/ARM 小主机 + USB/miniPCIe 4G 模块（通过 AT 串口 / QMI / MBIM 驱动）
- Android：独立 APK，利用系统 Telephony API 读取短信、来电并转发

### Android 视觉与交互基线（硬性要求）

- Android APK 的视觉语言必须与共享 `webui/` 基本一致，而不是使用未经设计的系统默认控件堆叠：
  沿用场测仪/控制台风格、深色画布、琥珀单强调色、1px 分隔线、近直角卡片、等宽数据文本以及
  success/error/loading/info 四类状态反馈。
- 保持 Android 原生界面和原生无障碍语义，不通过 WebView 复制网页；Web 与 APK 应共享明确记录的
  设计令牌（颜色、间距、圆角、字体角色和组件状态），允许根据手机窄屏做单列、自适应和触控尺寸调整。
- APK 页面至少保持与 Web 相同的信息层级：品牌/平台标识、运行状态、功能卡片、字段标签/提示、
  主次操作和明确结果反馈。新增 Android 页面或组件必须继续使用这一套视觉组件，不得各自定义风格。
- Android UI 文案、资源和事件绑定应从 Activity 业务代码中拆分，优先使用 XML 资源、公共样式和小型
  可复用组件/辅助函数，避免继续扩大单个 Activity 的程序化布局与重复状态处理。

## 三、架构硬性要求（最重要）

1. **分层架构，核心与平台解耦**：
   - `core/`：与平台无关的业务逻辑——短信解析、转发规则、消息队列、推送渠道（Bark/Telegram/企业微信/Webhook/邮件等，以原仓库已有渠道为准）、配置模型。
   - `hal/`（或 `platform/`）：平台抽象层，定义统一接口，如 `ModemInterface`（AT 收发短信/来电）、`NetworkInterface`、`StorageInterface`、`AudioInterface`（为 WiFi Calling 预留）。
   - `platforms/esp32/`、`platforms/linux/`、`platforms/android/`：各平台仅实现 HAL 接口 + 入口与构建脚本，**禁止在平台层写业务逻辑**。
   - `web/`：共享的 Web 管理界面（沿用 sms_forwarding 现有前端，样式不改），嵌入式端编译进固件，Linux 端由内置 HTTP 服务托管。
   - Android 原生 UI：复用 `webui/` 的设计令牌与信息架构，但不复制 Web 业务逻辑；Activity/Fragment
     只负责展示、权限和平台操作，业务判断继续来自 core/JNI。
2. **AT 命令层独立抽象**：以 ML307R-DC 为首个回归 driver；EC200、SIM7600 等其他 4G 模块的 AT 差异通过 driver 适配，新增模块只需加一个 driver 文件。
3. **配置格式统一**：所有平台使用同一份配置 schema（建议 JSON），保证配置可在设备间迁移。
4. **消息协议统一**：短信/来电事件在内部用统一结构体表示，转发格式与原项目输出完全一致。

## 四、功能范围

**必须保留（来自 sms_forwarding，逐项对照原仓库确认，不得遗漏）：**
- 短信接收与转发（全部现有推送渠道）
- 来电提醒转发
- Web 配置界面（样式、交互、页面结构不变）
- 现有的配网 / 设备状态显示等已有能力

**新增（参考 SimAdmin 评估后引入）：**
- 优先评估 ModemManager D-Bus、短信远程发送、通话管理、eSIM/eUICC、设备自愈、通知中心和自动化任务等能力，先输出评估清单再实现，逐项征求确认。
- WiFi Calling 仍是项目目标，但 SimAdmin 当前没有已确认的 WiFi Calling 实现；该能力需独立调研和设计，不得假定能直接从参考仓库移植。
- 嵌入式平台若资源不足以支持 WiFi Calling，明确标注"该功能仅 Linux/Android 支持"，不要硬塞导致不稳定。

## 五、工程与质量要求

1. 代码**简洁、可读、注释适度**，避免过度设计；性能敏感路径（如串口读取、消息队列）注意内存与延迟，嵌入式端注意 RAM/Flash 占用。
2. **每完成一个阶段就更新 `README.md`**：支持的设备列表、编译/烧录/部署方法、功能矩阵表（哪个平台支持哪些功能）。
3. **进度持久化**：约定 `docs/progress/` 目录，每次工作会话结束时写入或更新进度文件（如 `docs/progress/2026-07-10.md` 或滚动更新的 `docs/progress/STATUS.md`），内容包括：已完成、进行中、待办、已知问题、下一步计划。**任何时候中断，都能从进度文件恢复上下文。**
4. Git 提交信息使用约定式提交（feat/fix/refactor/docs），小步提交。
5. 每个平台提供最小可运行示例与自测步骤；core 层关键逻辑（解析、规则匹配）编写单元测试，可在 PC 上直接跑。
6. Android 界面修改必须同时检查手机窄屏、较宽屏幕、动态字体、触控区域和深色主题；构建通过只能证明
   资源合法，不能替代真机视觉与可用性回归。
7. 引用 SimAdmin 代码前**检查其 License**。SimAdmin 当前为 GPLv3，而 omniSMS 当前为 MIT；未明确决定许可证兼容方案前，只允许做功能与接口调研，不直接复制 GPL 代码。引用处必须注明来源。

## 六、执行顺序（建议按此推进）

1. **阶段 0 — 调研**：通读两个参考仓库，输出：现有功能清单、SimAdmin 可借鉴功能评估清单、许可证兼容结论、目标架构图（写入 `docs/progress/`）。
2. **阶段 1 — core + HAL 抽象**：抽取 sms_forwarding 业务逻辑到 core，定义 HAL 接口。
3. **阶段 2 — ESP32 回归与 AT 模组扩展**：先让 ESP32-C3 + ML307R-DC 在新架构下达到与原仓库功能/样式完全一致（这是验收基线），再使其他 ESP32/S3/C3 组合可编译；随后按 P1 顺序实现移远、芯讯通和合宙 AT driver，每个系列都要有能力矩阵和测试向量。
4. **阶段 3 — Linux 平台**：实现 Linux HAL（AT 串口驱动 + 内置 Web 服务），在随身 WiFi / 小主机上跑通。
5. **阶段 4 — Android APK**：基于 Telephony API + JNI core 实现同等转发功能；完成短信/来电、HTTP/SMTP、发送短信、USSD、收发件箱、管理员命令、调度/开机恢复、配置编辑和前台服务。APK 原生 UI 必须按上述 Web 视觉基线统一组件风格，随后完成插卡真机、多卡、后台保活、动态字体和窄屏回归。
6. **阶段 5 — Linux 增强与 WiFi Calling**：先按确认清单引入 SimAdmin 相关 Linux 增强；WiFi Calling 独立调研后先在 Linux 端实现，再评估 Android / 嵌入式可行性。

每个阶段完成后：更新 README + 进度 md，再进入下一阶段。

## 七、验收标准

- ESP32-C3 + ML307R-DC 新版本与原 sms_forwarding 逐功能对照通过，Web 界面像素级不变。
- Linux 与 Android 端实现相同的转发功能，配置格式互通。
- Android APK 与共享 Web UI 的品牌、配色、卡片、表单、按钮和状态反馈基本一致；手机窄屏不横向
  溢出，关键操作具备清晰标签与至少 48dp 触控区域，且未用 WebView 绕过原生实现。
- 新增一个 4G 模块型号，只需新增一个 driver 文件，不改 core。
- README 与 `docs/progress/` 始终与代码状态同步。

## 八、遇到不确定时

- 涉及"是否改动原有行为/样式"的决策：**默认不改**，如确有必要，先在进度文件中记录理由并提出，等待确认。
- 硬件差异导致某功能无法实现时，降级为"该平台不支持"并在功能矩阵中标注，不要伪实现。
