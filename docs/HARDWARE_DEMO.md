# 触摸硬件测试 Demo

> 阶段已收尾：用户已确认工具与硬件测试完成。以下保留当时的操作说明与联调记录；其中 `artifacts/` 日志、截图和旧版测试源码已迁入 `backups/hardware-validation-20261006.tar.gz`，不再散放在工作目录。归档恢复方法见 [备份说明](../backups/README.md)。

适用：立创实战派 ESP32-S3 V1.0.1 / N16R8。硬件定义依据相邻参考资料目录中的知识库和官方示例。摄像头保持断电控制状态，不创建驱动、不测试。

## 操作

顶部左右箭头、底部标签和内容区横向滑动切换六页。长页面可上下滚动。屏幕采用英文短标签，以适应 320×240 分辨率；下文给出中文说明。

- **SYS**：内存、运行时间、触摸操作计数、BOOT 按键计数和背光滑块。BOOT 请在程序运行时按，启动时按住会进入下载模式。
- **MIC**：Record 5s 录制双麦克风，L/R 电平表按 -60～0 dBFS 显示采样峰值；Play 回放；Tone 播放一秒 440 Hz 音；下方带数值的滑块调整音量（0–90，初始 70）；Save WAV 在已挂载 SD 根目录保存唯一名字 `REC_<时间>.wav`。录音先保存到 PSRAM，没有 SD 也能录制/回放。格式为 16 kHz / 16-bit / stereo PCM。播放时使能功放，录音时关闭功放。
- **WiFi**：Scan 扫描 2.4 GHz 接入点，最多显示 8 个；向下滚动填写 SSID 和密码，软键盘完成后关闭；Connect 发起连接，获取 DHCP 地址后显示 IP。凭据只存 RAM，重启不保留。连接验证覆盖 Wi-Fi 关联和 DHCP，不代表互联网可达。
- **BLE**：Scan BLE 扫描 8 秒，显示最近设备地址、信号强度和去重数量（上限 32）。此页验证 BLE 接收，不包含 GATT 服务或配对。ESP32-S3 不支持经典蓝牙 / A2DP。零设备是结果不确定，不能据此断言硬件损坏。
- **IMU**：QMI8658 芯片 ID 检查、加速度（g）、角速度（°/s）、温度、由重力估算的 roll/pitch 和水平气泡。快速运动时重力角度会失真；不输出无磁力计依据的绝对航向角。
- **SD**：Mount 挂载 FAT 卡，Eject 安全卸载；选择列表项后 Open 进入目录或读取前 96 字节预览，Up 返回父目录，Refresh 刷新。最多列出 48 项。New test 创建根目录 `DEMO_<时间>.txt`，关闭后重新读取并逐字节校验。Rename 给选中的测试文件追加 `.renamed`，不覆盖已存在目标；Delete test file 弹窗确认后删除。改名/删除限根目录 DEMO_/REC_ 文件。挂载失败不格式化。取卡前先 Eject。

所有录音、播放、扫描和文件操作通过有界任务队列串行执行；录音/播放最长约 5 秒，I2S 单次等待限 1 秒。UI 在后台任务运行期间仍可翻页。

## 构建与烧录

```bash
bash dev.sh build
bash dev.sh flash
```

现有 `scripts/pio_run.py` 处理工程目录中的空格，并在 sdkconfig.defaults 修改后重新生成构建配置。依赖由 ESP-IDF Component Manager 下载。编译产物位于 `.pio/build/szp_s3/`。

之前的连接验证源码及 `check-serial` / `check-debug` 脚本已归档，当前 `dev.sh` 不再提供这些过时入口。原始完整 Flash 备份仍独立保存在 `backups/`。

## 串口检查 / 截图

UART0：115200，GPIO43/44，通过 CH340。原生 USB 仍用于烧录/JTAG，新 demo 的测试命令使用 CH340。

```bash
python3 scripts/demo_serial.py status
python3 scripts/demo_serial.py wifi ble --wait 10
python3 scripts/demo_serial.py record play --wait 7
python3 scripts/demo_serial.py mount sdtest save --wait 4
python3 scripts/demo_serial.py 'page 0' shot --output artifacts/screen-home.log
```

截图来自运行中的 LVGL framebuffer，可用于检查实际 UI 布局，但不能证明物理 LCD 色彩或触摸坐标正确。命令 `tone` / `play` 会播放声音；`sdtest` / `save` 会新增文件。脚本需要 Python 的 pyserial 和 Pillow。

硬件验收还需要实际点击每页、向两个麦克风分别说话、听回放、移动板卡观察姿态。未插 SD 或未输入 Wi-Fi 凭据时，相应项目不能标记为通过。

## 本次板上验证（2026-10-06）

- 固件实际烧录和 Flash 摘要校验通过；I²C 音频驱动与 ESP-IDF 5.5 的兼容问题已修复。
- 用户确认界面可用；RGB565 字节交换已开启，正文改为 16 像素，SPI 屏幕时钟为 40 MHz。实际 LVGL 截图已检查。
- Wi-Fi 实测扫描到至少 8 个接入点；未配置凭据，尚未实测连接 / DHCP。
- BLE 实测扫描到 2 个不同设备。
- 初次验证只检查了 320,000 字节传输、回放驱动返回值和 WAV 保存，未发现录音为全零；该结果不能视为麦克风通过。后续专项修复见下文。
- SD 已挂载，测试文本创建后读回校验 PASS；未格式化、未更改原有音频文件。测试生成的 DEMO_*.txt 和 REC_*.wav 保留在卡中。
- QMI8658 实时数据随板卡姿态变化；联调期间记录到一次 I²C 超时，后续恢复，界面会显示错误而不会把它当作正常数据。
- 截图导出已修复内存池不足和长时间忙输出的问题，保留看门狗功能。

日志：`artifacts/demo-hardware.log`、`artifacts/demo-flash-console.log`。
屏幕截图：`artifacts/demo-final-screen*.png`。

字体说明：英文正文为 Montserrat 16，文件名 / 扫描信息添加内置常用中文字体回退；生僻字不保证覆盖。FAT 文件 API 使用 UTF-8。

## 录音全零修复

网表确认 ES7210 SDOUT1 经 R36、SDOUT2 经 R37 共接 GPIO12；普通双声道模式不适合本板布线。恢复官方 speech 示例的四通道 TDM，I2S 为两个 32-bit slot（每帧共 64 bit），ES7210 为四路 16-bit 数据。按官方缓冲排列提取索引 1/3 的两个物理麦克风，排除索引 0 的 DAC 回采。对外录音和 WAV 仍是 16-bit 双声道；播放时把 16-bit 数据放进 32-bit slot 的高 16 位。

修复前实测：左右各 80,000 样本全部为 0。现在增加了左右声道峰值、RMS/dBFS 诊断，全零声道会显示 SILENT CHANNEL。串口 `audio` 输出当前录音统计和 ES7210 寄存器；`artifacts/audio-before.log` / `audio-after.log` 保留对比记录。此前保存的静音 WAV 不会被自动修改，需要重新录制。

修复后板上采样：左右各 80,000 样本，非零样本分别 79,419 / 79,420，峰值 421 / 407，AC RMS 58.53 / 60.62（约 -55 dBFS 的当前环境声）。录音和 32-bit slot 回放完成，无全零现象；语音清晰度由实际说话和听回放验收。

## 回放过轻 / 位宽修复

进一步回采发现 ES8311 格式寄存器为 0x1c：esp_codec_dev 1.3.x 把复位值中的 16-bit 字段和 32-bit 标志按位或，未清除旧字段。BSP 在配置后完整替换位宽字段并读回检查（DAC 0x09 位宽应为 0x10，ADC 0x0a 保留关闭标志）。

默认输出音量从 35 提高到 70（驱动的 35 约对应 -32.5 dB，70 约 -15 dB），范围 0–90；麦克风模拟增益从 24 提到 30 dB。仅在本地回放时去掉 DC，并按峰值调整响度，上限 32 倍，目标峰值 12000，含限幅保护。WAV 保存和录音电平始终使用原始采样，不做这项回放增益处理。

串口 `loopback` 顺序测量无声、音量 35、音量 70 下的四通道 ADC 数据以及 440 Hz 分量，同时检查 PCA9557 功放使能位；会播放测试音。`artifacts/dac-loopback.log` 为修复前回采记录；不能把模拟回采单独当作扬声器听感验收。

修复后实测（`artifacts/playback-after.log`）：ES8311 寄存器 0x09=0x10、0x0a=0x50；音量 70 时 PA 输出位有效，DAC 回采 slot0 的 440 Hz 分量约 15987（无声时约 0.39），两个物理麦克风也检测到较弱的同频分量。环境录音约 -46 dBFS，本地回放自动提升 26.3 dB，完成 5 秒播放。以上证明模拟输出与采集有响应，语音可辨识性及最终扬声器听感仍待人工确认。
