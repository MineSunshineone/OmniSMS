# ESP-IDF 完整固件

本目录已完整迁入
[sms_forwarding](https://github.com/MineSunshineone/sms_forwarding) 的 ESP32-C3 +
ML307R-DC 在役固件，作为 OmniSMS 的功能与 Web UI 回归基线。

## 已迁入范围

- 完整 `app_main` 启动流程。
- `idf_config`、`idf_modem`、`idf_sms`、`idf_push`、`idf_wifi`、`idf_web`。
- `idf_esim`、`idf_inbox`、`idf_logbuf`、`idf_pdu` 和 gzip `web_assets`。
- SoftAP/STA 配网、状态页、收发短信、来电提醒、10 类推送、SMTP、规则、保号任务、
  eSIM、诊断、日志、配置导入导出和 Web OTA。
- 4 MiB Flash 双 OTA 分区、独立短信 NVS 和 coredump 分区。
- Web 静态源、生成资产和 Web 打包校验脚本。

迁入当时的 `main/`、`components/`、`code/`、`dev_doc/` 和 `preview/` 已与参考仓库审计版本
逐文件 SHA-256 校验一致。当前 `idf_sms`/`idf_push`/`idf_config` 已开始改为调用共享 core，
因此这些组件不再与参考仓库保持文件级相同；Web 源码和生成资产仍保持不变。

## 构建验证

2026-07-10 使用 ESP-IDF 5.5.4、目标 `esp32c3` 构建通过：

- 固件版本：`1.0.8`
- `omnisms_esp32.bin`：`0x135b40` 字节
- 单个 1.625 MiB app 分区剩余 `0x6b850` 字节，约 26%
- Web assets `--check` 通过

```powershell
powershell -ExecutionPolicy Bypass -File ports\esp-idf\tools\idf.ps1 build

# 按实际串口修改 COM5
powershell -ExecutionPolicy Bypass -File ports\esp-idf\tools\idf.ps1 flash -Port COM5
powershell -ExecutionPolicy Bypass -File ports\esp-idf\tools\idf.ps1 monitor -Port COM5
```

Web 源码改动后必须执行：

```powershell
python ports\esp-idf\tools\build_web_assets.py
python ports\esp-idf\tools\build_web_assets.py --check
```

跨平台 JSON 配置使用 [config.schema.json](../../docs/config.schema.json)。为保持原页面像素级不变，
原 `/export` 文本备份默认行为不变；可通过 `/export?format=json` 导出脱敏 JSON，
`/export?format=json&full=1` 导出含密钥的可迁移 JSON。原 `/import` 接口现在可自动识别 JSON
或旧 `key=value` 备份，NVS key 与 Web API 均未改名。

## 当前边界

完整源码和构建基线已经迁移，但尚未在本仓库版本上完成真实硬件烧录与逐功能回归。因此在
实机验证前，参考仓库仍是生产固件基准。

号码、文本工具、规则、推送报文、PDU 收发、长短信合并、固定容量队列及完整配置
schema 已接入共享 core。下一步是参数化芯片目标和 UART/GPIO，验证 ESP32-S3 等组合
可编译，并在真实 ESP32-C3 + ML307R-DC 上执行逐功能回归。
