# 共享 Web UI

自 sms_forwarding `code/web_src/` 引入的同一套界面源（HTML/CSS/JS，无构建依赖），
保持与固件版一致的视觉风格与交互。

- ESP 端口：由固件的 `tools/build_web_assets.py` 压缩为 gzip C 数组嵌入。
- Linux 端口：后续里程碑直接托管本目录静态文件，并按 `app.js` 期望的
  接口（`/status`、`/config.json`、`/save`…）实现兼容后端。

修改界面请改这里，再同步回各端口的嵌入产物；不要在端口侧 fork 出第二套 UI。
