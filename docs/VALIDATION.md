# 验证记录 — 2026-10-06

## 已执行

- PlatformIO / ESP-IDF 5.5 编译与链接通过，USB 烧录及 Flash 摘要校验通过。
- 实机启动：SD、QMI8658、音频初始化通过；迁移大队列、索引和状态到 PSRAM 后，解决初始化分配失败。
- 使用用户提供的 Wi-Fi 配置完成连接与 DHCP，地址为 `192.168.3.19`。凭据仅写入设备 NVS，未硬编码到源码。
- SNTP 首次校时成功；重启后保存的 Wi-Fi 和城市配置可恢复，再次获取时间。
- 广东中山地理编码与 HTTPS 天气请求成功。主页实际显示日期、时间、天气、温度和体感温度。
- 实际 framebuffer 检查了主页、应用菜单、游戏菜单、迷宫、Color Flood、Wi-Fi、文件管理、音乐及配置网页入口。
- 现有 SD 卡上的《生如夏花.mp3》能解码并配置 44.1 kHz 音频输出，播放和停止命令完成；已修复上游播放器无效帧头不前进的问题。未修改原 MP3。
- 配置网页 GET 返回 200；错误配对码返回 403；不合法模型名返回 400；携带有效码、保留原值的保存请求返回 200。
- 主机 AddressSanitizer / UndefinedBehaviorSanitizer 检查通过：重采样计数、任意分块一致性与波形电平；SD 路径限制；MP3 错误恢复。
- 五关迷宫均通过按实际球半径计算的路径可达性检查。
- 构建脚本回归测试通过：组件清单变化会清除 CMake 缓存，未变化则保留缓存。

## 尚需实测，未标记通过

- 有效 StepFun Key 下的连续 20 轮对话、实际语音转写、函数调用与多次打断。
- 回声消除效果、扬声器实际听感和不同音量下的误触发率；驱动成功不等同于语音听感验收。
- 有效 Tavily Key 下的网页搜索。
- 实际 BLE 外设配对成功／失败场景，屏幕实际触摸手势和体感游戏操控手感。
- JPEG/PNG 真正从 SD 卡显示、不同码率 MP3 样本、损坏文件、热拔卡和文件修改的完整回归。
- 每日校时的长周期观察与至少两小时混合运行测试。

## 证据与复现

日志和截图保存在本机 `artifacts/`，不进入 Git：

- `flash-console.log`：最终构建和烧录。
- `host-tests.log`：主机回归测试。
- `network-test.log`：首次 Wi-Fi / SNTP 接入及 TLS 问题定位。
- `zhongshan-weather.log` / `.png`：TLS 改用 PSRAM 后的中山天气。
- `media-verified.log`、`media-verified.png`：修复后的 MP3 实测。
- `web-test.log`：配置网页接口验证。
- `final-desktop.png`、`final-desktop-1.png`：天气主页与应用菜单。
- `final-status.log`：最终设备状态。

使用 `bash scripts/test_host.sh` 重跑主机测试。使用 `scripts/demo_serial.py` 获取状态和截图。当前串口命令包括 `status`、`page N`、`shot`、`ls`、`music <绝对路径>`、`stop`、`city <城市>`、`web`；Wi-Fi 配置命令中的密码不要放进共享日志或提交文件。
