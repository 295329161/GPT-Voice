# 构建、串口诊断与复测

## 主机测试

运行 `bash scripts/test_host.sh`。使用 AddressSanitizer 和 UndefinedBehaviorSanitizer，UBSan 遇到问题立即失败。覆盖 BOOT 消抖、搜索意图与时间路由、来源处理、WebSocket 分片、对话排序、重采样、SD 路径、目录 I/O 故障保留有效列表、三消/射击、姿态滤波、MP3 异常帧恢复及 WAV 非法输入。另验证含空格路径的构建暂存同步与组件清单缓存失效。

## 实机串口

1.1 新增 `core/usb_disk` 的跨扇区写保护、越界、I/O 错误和计时回绕测试，以及传输中本机存储拦截、烧录保护与依赖锁同步测试。USB 实机测试步骤及边界见 [USB 使用说明](USB_TRANSFER.md) 和 [验证记录](VALIDATION_USB.md)。

运行 `python3 scripts/device_console.py artifacts/session`。同一时刻只允许一个串口读取程序。脚本使用 Linux termios，打开与关闭不改变 DTR/RTS，不会像旧的串口助手一样意外复位。截图需要 Pillow。

- `status`：网络、校时、SD、姿态、音频、内存、语音队列、音乐状态及背光。
- `ui`：当前页面编号和可见控件文字/坐标。
- `page N`：进入页面；文件详情和图片页面必须通过文件选择进入。
- `tap X Y [毫秒]`：LVGL 指针按压，默认 100 ms；600–1000 ms 可验证长按。
- `swipe X Y X2 Y2`：滑动；`text 内容` 设置当前获得焦点的文本输入框。
- `:shot 文件名`：保存当前 LVGL framebuffer 的 PNG，不包含物理背光状态。
- `music /sdcard/文件.mp3`、`music-toggle`、`music-next 1`、`music-next -1`、`stop`：播放控制。
- `audio-probe`：语音关闭时测量 PA 输出/引脚、DAC 与两个麦克风幅度，不保存录音。
- `voice-test 文本`：当前语音会话空闲就绪时，从真实会话发起文字问题，实际调用服务、工具并播放语音。日志记录这次诊断的输出。此命令不能证明物理麦克风 ASR 与真人打断效果。
- `voice-sources 0/1`：语音对话/来源视图。
- `fixtures 1`：在 SD 上独占创建 `GPTVoice_Test_1_0` 测试目录；若已存在则拒绝覆盖。`fixtures 0`：验证所有者标记后清除固定测试文件；不会递归删除额外文件或其他目录。
- `:boot-down` / `:boot-up`：通过 CH340 控制 GPIO0，验证真实 BOOT 电平和消抖；`:reset` 执行硬件复位。实测映射为 DTR=BOOT、RTS=RESET，两条线一次性写入，避免瞬态复位。退出时只释放本次主动改变过的控制线。
- `:quit`：关闭串口。

## 复测原则

固件编译、屏幕显示“正在播放”、模拟点击或成功发送网络请求都不能分别替代实际音频输出、物理触摸、麦克风语音识别或得到有效查询结果。报告应保留这一区分。

重要流程：音乐播放→暂停→停止→SD 卸载/挂载；音乐→GPT Voice→音乐恢复；正常/损坏图片及音频；创建/重命名/冲突/删除确认；网页错误配对码和非法字段拒绝；空字段保留设置；BOOT 连续按压、长按、熄屏期间后台任务与复位默认亮屏。

最终发布记录在 `validation/1.0.0/`。完整旧日志和含环境信息的调试材料保留在项目外的归档中，不将私有 Flash/NVS 纳入发布包。
