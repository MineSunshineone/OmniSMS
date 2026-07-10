# OmniSMS Android 端口

目标：让 Android 8.0+ 插卡旧手机作为短信/来电转发器，同时与 ESP 和 Linux 共用同一份
C++ core 与 JSON schema v1。

## 已实现

- `SMS_RECEIVED` 接收短信；Android 系统负责 PDU 与长短信重组，事件直接进入共享 core。
- `PHONE_STATE` 来电事件；号码不可见时明确使用“未知号码”。
- JNI 直接编译仓库 `core/`，提供配置校验、号码规范化、黑名单、转发规则和 10 类 HTTP
  推送报文组装，没有在 Kotlin 复制业务规则。
- WorkManager + SharedPreferences 持久化 HTTP 请求并使用指数退避重试。
- JNI 复用 core 固定容量收发件箱（收件 50、发件 10）；SharedPreferences 只负责恢复/落盘，
  多目标全部成功才标记已处理，最终失败保持未处理。
- SmsManager 发送单段/长短信，TelephonyManager 发送 USSD。
- 管理员号码可使用共享 core 的 `SMS:号码:内容` 命令代发短信；普通 APK 无权执行设备级
  `RESET`，收到时会明确记录并通知“不受支持”，不会伪装成功。
- 前台服务通知、运行时权限请求和 schema v1 JSON 配置编辑器。
- JNI 复用 core `scheduler` 判定保号、六个周期任务、每日心跳和每日维护时机；WorkManager
  每 15 分钟唤醒，昂贵周期检查在应用内节流到一小时，并在开机后自动恢复。
- 调度动作支持指定蜂窝网络的 HTTP GET、SmsManager 短信、TelephonyManager USSD 和本地通知。
  普通 APK 无权切换 eSIM Profile 或执行设备级每日重启，这两项会明确通知“不受支持”。
- SMTP 支持 465 隐式 TLS、其他端口强制 STARTTLS、AUTH LOGIN、系统证书与主机名校验、
  RFC 2047 主题和 Base64 正文；邮件与 HTTP 一样由 WorkManager 持久化并指数退避重试。
- 原生控制台复用 `webui/app.css` 的场测仪设计令牌：深色画布、琥珀强调、1px 分隔、近直角卡片、
  等宽数据/配置和 success/error/loading/info 状态。布局使用 XML 资源，不用 WebView，也不在
  Activity 中继续动态堆控件；Gradle `verifyWebStyleTokens` 会在构建前阻止关键颜色与 Web 漂移。
- 短信/USSD 权限在实际调用前再次检查；窄屏模拟器和 1.3 倍系统字体下无横向溢出，主要按钮
  保持至少 48dp 触控高度。
- `arm64-v8a`、`armeabi-v7a`、`x86`、`x86_64` 四 ABI Debug APK 已构建通过。

邮件主题/正文由共享 core 生成，Android 只处理 SMTP/TLS I/O；平台不支持或尚未完成的能力不得
视为已实现。

## 构建

要求：JDK 17+、Android SDK 36、NDK `28.1.13356709`。仓库已包含 Gradle wrapper。

```powershell
cd ports/android
.\gradlew.bat assembleDebug
.\gradlew.bat lintDebug
```

APK：`app/build/outputs/apk/debug/app-debug.apk`。

首次启动需授予短信、电话、通话记录、USSD 和通知权限，再点“启动前台服务”。配置默认从
`app/src/main/assets/default_config.json` 读取，保存到 SharedPreferences；格式与
`../../docs/config.schema.json` 一致，可直接导入 ESP/Linux 的 schema v1 导出。

## 架构边界

- Kotlin/Android：权限、生命周期、BroadcastReceiver、Telephony、通知、持久任务与 HTTP I/O。
- JNI/C++ core：配置解析、号码/黑名单、规则决策、通道选择、签名与请求报文。
- Android 不走 AT/PDU 层；这是事件级 `ModemInterface` 在非 AT 平台上的预期用法。

## 尚待真机验证

- Android 10+ 对后台来电号码的限制；部分设备即使有权限也只提供空号码。
- 双卡默认订阅选择、厂商自启动/省电白名单和长时间后台可靠性。
- 真实运营商的短信发送回执、USSD 行为、10 类 HTTP 推送与 SMTP 端到端结果。
- 不同厂商手机、系统字体和显示缩放下的最终视觉/无障碍回归。
- 消息列表 UI、配置导入文件选择和发布签名。
