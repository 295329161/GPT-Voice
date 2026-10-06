# GPT Voice 桌面智能终端

基于立创实战派 ESP32-S3 V1.0.1（16 MB Flash / 8 MB PSRAM），使用 PlatformIO、ESP-IDF 5.5 和 LVGL 8.3。

## 使用

- 开机进入天气与时间看板，左右滑动进入应用菜单。菜单支持分页，点击图标进入应用，顶部返回键逐级返回，主页键直接回看板。
- 应用：设置、音乐、游戏、图片、GPT Voice、文件管理，以及 Home Assistant“开发中”入口。
- 设置中连接 Wi-Fi；连接信息、亮度、音量、城市等保存到 NVS，重启保留。
- 首次联网自动 SNTP 校时，默认北京时间，每 24 小时再次同步。天气每 15 分钟刷新，断网保留本次启动期间的缓存。
- 音乐优先扫描 SD 卡 `/Music`，没有音乐时扫描根目录及子目录；支持 MP3 和 16-bit PCM WAV。图片从 `/Pictures` 浏览，无此目录时可从根目录选择 JPEG/PNG。
- 文件管理点击文件进入操作页，长按文件夹可重命名或删除空文件夹。删除须再次确认；正在播放或暂停占用的文件不能修改。
- 游戏包含五关重力迷宫、倾斜躲避和 Color Flood。体感游戏开始／恢复时以当前姿态校准，灵敏度在游戏菜单调整。
- 音乐可后台播放。进入语音会话暂停音乐，退出语音页结束会话，音乐由用户手动恢复。
- 蓝牙支持 BLE 扫描、连接与免输入安全配对；需要密码／数字确认的设备会明确提示首版不支持，不包含经典蓝牙音箱协议。

## 语音与网页配置

1. 在设备“设置 / 网页与语音配置”开启配置网页。
2. 同一局域网浏览器访问屏幕显示的地址，输入设备屏幕配对码。
3. 填写 StepFun API Key；默认模型是 `stepaudio-3-realtime-preview`，声音是 `soft-spoken-gentleman`。使用 Step Plan 套餐时，接口地址填写 `wss://api.stepfun.com/step_plan/v1/realtime`，模型填写 `stepaudio-2.5-realtime`，音色可填写官方示例的 `linjiajiejie`；地址不需要带模型查询参数。参见 [Step Plan 语音接入](https://platform.stepfun.com/docs/zh/step-plan/integrations/audio-api)。
4. 模型、声音、WebSocket 地址可以修改。密钥字段留空保留原值；页面不回显已存密钥。
5. 天气工具使用无需密钥的 Open-Meteo。网页搜索采用可选 Tavily API Key；未配置时如实报告不可用，不伪造查询结果。
6. 配置网页 10 分钟后自动关闭。仅在可信局域网开启；个人开发版没有启用 Flash 加密。

实时语音已实现流式音频、服务端 VAD、取消旧回答、字幕、工具回传、ESP-SR AEC 和 16/24 kHz 转换。真实服务会话和扬声器回声效果需要有效密钥及实机说话验收，不能仅凭编译通过视为验证完成。

## 开发命令

```bash
bash dev.sh build
bash dev.sh flash-usb
bash dev.sh monitor
bash scripts/test_host.sh
python3 scripts/demo_serial.py status
python3 scripts/demo_serial.py 'page 0' shot --output artifacts/home.log
```

构建脚本处理路径中的空格，产物位于 `.pio/build/szp_s3/`，日志在 `artifacts/`。主机测试覆盖音频采样计数、分块连续性、波形电平、SD 路径限制以及五关迷宫可达性。截图是实际 LVGL framebuffer，不替代触摸或扬声器听感验收。

新增应用使用 `terminal_app_t` 和 `shell_register_app()`，提供独立的创建／进入／离开／销毁回调；内容根节点由 `shell_content()` 获取。自定义应用 ID 使用 100 以上，注册描述符保持静态生命周期。后台服务通过 `terminal_submit()` 提交有界作业。新增语音工具在 `services/tools.c` 的工具表登记描述、参数与执行函数。

详细说明：[架构](docs/ARCHITECTURE.md)、[验证记录](docs/VALIDATION.md)、[历史硬件测试](docs/HARDWARE_DEMO.md)。原始 Flash 和完整测试归档保留于 `backups/`，未更改 SD 卡原有文件。
