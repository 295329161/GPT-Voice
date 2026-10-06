# gtpvoice — 测试阶段已完成

用户已确认工具与硬件测试完成。当前保留可用的硬件测试源码作为开发基线；正式项目的功能、接口和工程结构，等待后续详细规划。

## 保留内容

- `src/`：最终硬件测试程序及已修复的板级驱动。
- `docs/HARDWARE_DEMO.md`：测试操作、硬件约束及音频/显示问题修复记录。
- `scripts/`、`dev.sh`：构建、烧录、串口诊断、USB JTAG 权限设置和原固件恢复工具。
- `backups/`：原始完整 Flash 备份，以及测试工程的完整归档。

## 日常命令

```bash
bash dev.sh build
bash dev.sh flash
bash dev.sh flash-usb
bash dev.sh monitor
bash dev.sh debug
```

构建包装脚本处理当前目录中的空格。编译时会重新生成 `/tmp` 构建目录、`.pio/`、`artifacts/` 和 `sdkconfig.szp_s3`。首次重新构建会恢复依赖并完整编译。

当前使用 PlatformIO / ESP-IDF 5.5，适配立创实战派 ESP32-S3 V1.0.1（16 MB Flash / 8 MB PSRAM）。芯片引脚、四通道 TDM、ES8311 位宽修正、RGB565 字节交换及 I²C 兼容配置须在后续开发中保留。

## 收尾与归档

2026-10-06 已清理重复日志、截图传输数据、编译产物、Python 缓存、临时构建目录和不适用于当前固件的旧串口/JTAG自动检查脚本，移除约 363 MiB 文件。

完整测试归档包含当时的源码、配置、脚本、全部日志/截图，以及最终固件与调试符号；归档内每个文件已与原文件逐一校验。原始 Flash 备份独立保留。

恢复与校验说明见 [backups/README.md](backups/README.md)。本次清理未烧录开发板，也未修改 SD 卡文件。
