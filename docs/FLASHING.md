# 固件下载与烧录

适用立创实战派 ESP32-S3 V1.0.1，16 MB Flash / 8 MB PSRAM。下载 [同一 Release](https://github.com/295329161/GPT-Voice/releases) 下的三个 BIN 文件，放在同一目录：

- `bootloader.bin`：启动程序，烧录地址 `0x0`。
- `partitions.bin`：分区表，烧录地址 `0x8000`。
- `firmware.bin`：应用固件，烧录地址 `0x10000`。

先结束设备的 USB 文件传输，在电脑安全弹出 SD 卡，并关闭占用设备的串口工具。

安装 esptool 后，在 BIN 所在目录执行（将 `PORT` 替换为实际串口）：

```bash
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

现有版本沿用相同分区布局，上述命令不写 NVS，可保留 Wi-Fi/API 等配置。**不要执行 `erase_flash`**。固件不包含 NVS、API Key 或 SD 文件；不要将三个文件拼接后按一个地址烧录。

GitHub Releases 仅上传这三个编译产物，SHA-256 写在各版本说明中。源码可从对应 Git 标签或 GitHub 自动生成的源码归档获取。异常应用可用 BOOT + RESET 进入 ROM 下载，再通过独立 CH340 恢复。

从源码构建与烧录：

```bash
bash dev.sh build
bash dev.sh flash-usb
```

`flash-usb` 优先原生 USB，缺失时回退独立 CH340；`bash dev.sh flash` 直接使用 CH340。详见项目 README。
