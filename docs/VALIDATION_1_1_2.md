# 1.1.2 界面风格与验证

日期：2026-10-08。设备：立创实战派 ESP32-S3 N16R8，320×240 横屏。

## 界面改动

- 返回、主页和工具栏使用无底色控件，按下时给轻微反馈；返回保留 54×40 点击区域，按深浅页面设置箭头颜色。
- 统一纯色卡片、列表、强调色与滑杆，设置列表增加图标和入口箭头。
- 音乐采用浅色导航与简洁封面；亮度、音量页突出百分比；天气使用纯色背景，避免 RGB565 渐变色带。
- 共用样式收敛至 `src/apps/ui_style.c`，便于后续页面保持一致。

## 验证

`bash scripts/test_host.sh` 完成 14 组 ASan/UBSan C/C++ 测试和 2 个 Python 测试用例。`bash dev.sh flash-usb` 构建并烧录成功，写入校验通过；设备信息页确认版本 1.1.2。

本轮界面开发阶段完成逐页截图检查、游戏入口、主页导航和 USB 确认取消测试。发布固件重新截取主界面、两页菜单、音乐、羊了个羊、USB 传文件，并在 (50,36) 检查音乐、三消及 USB 返回，确认无底色控件仍覆盖原来扩大后的点击区域。机器记录见 [results.json](../validation/1.1.2/results.json)。

截图与输入来自实际设备帧缓冲及串口注入 LVGL 指针事件，不能代替真人触摸手感评估。本轮没有重新执行真人语音识别、声学打断或 USB 文件传输全流程；历史验证边界见 [USB 功能验证](VALIDATION_USB.md)。

## 发布

Git 标签 `v1.1.2`。GitHub Releases 只上传 `firmware.bin`、`bootloader.bin`、`partitions.bin`，SHA-256 写在版本说明中。旧版 1.0.0、1.1.0、1.1.1 同样仅保留三个 BIN 上传附件，标签、发布记录和源码历史保留。

分区表与 1.1.1 相同，烧录不擦除 NVS，不修改 SD 文件。安装步骤见 [烧录说明](FLASHING.md)。

## 实机截图

- [主界面](../validation/1.1.2/screenshots/home.png)
- [应用菜单第一页](../validation/1.1.2/screenshots/menu-1.png)
- [应用菜单第二页](../validation/1.1.2/screenshots/menu-2.png)
- [音乐](../validation/1.1.2/screenshots/music.png)
- [羊了个羊](../validation/1.1.2/screenshots/tiles.png)
- [USB 传文件](../validation/1.1.2/screenshots/usb.png)
