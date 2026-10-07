# 1.1.1 界面优化验证

日期：2026-10-07。设备：立创实战派 ESP32-S3 N16R8，320×240 横屏。此版本仅包含图标、返回按钮及相关布局维护，Home Assistant 仍为开发中入口。

## 本次范围

- 应用菜单统一渐变图标；设置采用八齿金属齿轮，Home Assistant 采用青蓝小屋与连接节点。
- 三个游戏入口加入独立图标，音乐封面与 Home Assistant 占位页同步更新。
- 返回按钮从 32×30 扩大到 54×40，点击面积增加到 2.25 倍；同步调整菜单、游戏工具栏与天气预览标题。

## 验证方式与边界

执行 `bash scripts/test_host.sh`，包含 14 组带 ASan/UBSan 的 C/C++ 测试和 2 个 Python 测试用例。使用 `bash dev.sh flash-usb` 构建与烧录 1.1.1，并通过设备信息页确认实际运行版本。

实机回归使用串口注入 LVGL 指针输入，检查三个游戏图标入口，以及游戏、音乐、USB、天气预览和菜单的返回操作。在坐标 (50,36) 点击，覆盖旧 32×30 按钮范围之外的新区域。截图检查两页菜单、游戏列表、三消、音乐、USB、天气预览和设备信息页。结果见 [机器记录](../validation/1.1.1/results.json)，图片位于 `validation/1.1.1/screenshots/`。

上述指针输入不能替代真人触摸手感评估。本轮没有重复进行真人语音识别、声学打断、USB 文件传输或跨操作系统兼容性测试；历史功能验证与限制见 [1.1.0 验证](VALIDATION_USB.md)。

## 发布方式

沿用原分区布局，烧录不擦除 NVS，不修改 SD 文件。保留历史版本。本地发布标签为 `v1.1.1`；发布包含应用、bootloader、分区表、调试 ELF、源码归档、说明文档与 SHA-256 校验清单，不含个人配置或整片 Flash 备份。未配置远程仓库，发布范围为本机“发布”目录。

## 实机截图

- [应用菜单第一页](../validation/1.1.1/screenshots/menu-1.png)
- [应用菜单第二页](../validation/1.1.1/screenshots/menu-2.png)
- [三个游戏入口](../validation/1.1.1/screenshots/games.png)
- [三消工具栏](../validation/1.1.1/screenshots/tiles.png)
- [音乐页](../validation/1.1.1/screenshots/music.png)
- [USB 普通模式](../validation/1.1.1/screenshots/usb.png)
- [天气效果预览](../validation/1.1.1/screenshots/weather-preview.png)
- [设备信息 1.1.1（IP 已遮盖）](../validation/1.1.1/screenshots/about.png)
