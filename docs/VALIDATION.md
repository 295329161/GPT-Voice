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
- Step Plan 实机连接：收到 `session.created` 与 `session.updated`，进入聆听状态；用户网页填写的 Key 和原 Wi-Fi 配置在固件更新后保留。
- 修复 WebSocket 事件队列内部 RAM 分配失败：语音消息、工具和采集任务栈迁移到 PSRAM，蓝牙优先使用 PSRAM；启动后空闲内部 RAM 约 56 KB。
- 修复 Step Plan 握手响应超过默认 1024 字节缓冲：底层 WebSocket 握手缓冲增至 8192 字节。实机 TLS 和会话配置通过。
- 实测定位采集循环被网络发送阻塞（16 ms 音频耗时超过 30 ms）。拆分采集／上传任务后，每 160 帧约 2.56 秒，上传队列稳定在 0–2 帧；收到自动 speech_started / speech_stopped、输入转写和语音回答，用户确认有反应。
- 连续回答复现原 8 条消息接收队列拥塞；改为 64 条接收消息、1024 块播放缓冲后，实测连续完成至少 12 次回答未再拥塞，用户反馈“比较稳定”。
- 手动 commit 在 server_vad 模式下被服务器拒绝，已移除临时诊断命令。模型曾猜错日期，新增 get_time 校时工具和会话时间上下文；工具调用的实际日期回答仍待确认。
- 自动打断时服务器可能先行结束回答，忽略明确的 no ongoing response to cancel 时序提示，避免误报配置失败。
- 对话区新增自动跟随最新文字、手动拖动暂停跟随、回到底部或“最新”按钮恢复跟随；实机确认“最新”按钮显示和会话继续运行；手动拖动体验由用户继续验证。
- 本轮主机重采样、路径、迷宫、MP3 与构建缓存回归检查通过（voice-host-tests.log）。

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
- `voice-fixed.log` / `.png`：Step Plan 会话配置成功后的聆听页面。
- `voice-live.log`、`voice-vad-live.log`：通道峰值、上传帧数和服务端事件诊断（不记录音频内容和密钥）。

使用 `bash scripts/test_host.sh` 重跑主机测试。使用 `scripts/demo_serial.py` 获取状态和截图。当前串口命令包括 `status`、`page N`、`shot`、`ls`、`music <绝对路径>`、`stop`、`city <城市>`、`web`；Wi-Fi 配置命令中的密码不要放进共享日志或提交文件。

进一步语音证据：`voice-pipeline-live.log` / `voice-pipeline.png` 记录自动语音事件和首次连续会话的拥塞；`voice-buffer-live.log` 为扩大缓冲后的复测。

消息顺序修复：新增 conversation 主机回归，覆盖助手先返回、前一轮转写晚于后一轮回答、工具后续回答、整轮历史淘汰和 UTF-8 截断；ASan/UBSan 通过，见 `voice-order-tests.log`。固件构建／烧录见 `voice-order-build.log`、`voice-order-flash.log`。

原生搜索接入修正：依据官方中文 Realtime 开发指南启用 tools 中的 `type: web_search`；之前只注册自定义 Tavily 搜索属于接入遗漏。备用工具更名为 external_web_search，未填备用 Key 时不向模型公布。构建与烧录日志：`voice-search-build.log`、`voice-search-flash.log`；服务端 session.updated 回传工具列表，确认包含 web_search；用户确认按明确搜索提示能得到新闻结果，屏幕提到来源名称。未独立核验每条新闻的原文链接，不能仅凭模型自述认定来源准确。

打断修复：长新闻期间曾出现上传队列满而停止采集，导致无法打断、之后触发服务端闲置超时。现上传短时拥塞丢弃最旧帧并继续采集；接收线程直接处理 speech_started 的静音与播放代次失效，旧音频不会等待普通事件队列才被清理。消息改为已解析 JSON 与接收代次，统一释放，防止迟到的旧 response.created 恢复旧播放。回归测试、编译、烧录通过（voice-barge-*.log）；实机已记录打断丢弃 8 个待播块并继续产生下一轮回答，实际听感与多次打断由用户继续验证。

新闻查询后无响应的后续排查：`voice-barge-live.log` 中最后应用事件约在开机 73 秒，之后麦克风/上传持续至 600 秒，期间没有新的服务事件。旧版本没有接收计数，尚不能据此断定是服务器停顿还是客户端丢弃消息。修复 WebSocket FIN/continuation 跨帧组包，新增 ASan/UBSan 测试覆盖单帧分块、多帧分块、穿插 Ping、空 FIN、异常偏移和累计超限（`voice-recovery-tests.log`）。新增 30 秒应用事件空闲探测、15 秒探测回复期限、45 秒回答无进展期限，最多自动恢复两次；恢复保留屏幕历史，但服务器上下文重新开始，需要重说问题。`status` 增加接收/事件年龄、JSON 错误计数和三条队列深度，不记录密钥或音频内容。编译和保留 NVS 的 USB 刷写通过（`voice-recovery-build.log` / `voice-recovery-flash.log`）；新闻复现、超时恢复和长时间运行需继续实测。

新版实机复测：用户反馈“这次挺好的”。`voice-recovery-live.log` 记录连续多轮 completed 回答；约 94 秒诊断显示 invalid=0、消息/播放队列为空、上传队列 2 帧。约 134 秒发出空 session.update 健康探测，约 80 ms 后收到 session.updated，模型/VAD/三项工具配置保留，证明探测不会被服务端拒绝。尚未人工注入服务端卡死来验证完整的自动重连路径；旧故障的具体上游原因仍未证实。

## 天气主页、手势与新游戏（2026-10-06）

- 主页／菜单清除滚动标记，子控件显式向根页面传递左右手势；菜单按方向循环翻页，手势后等待抬手以避免误点应用。去除主页的导航说明文字与 Wi-Fi 连接文案。
- 实机 framebuffer 已检查新的昼夜天气布局、菜单、雷电突击、层叠三消和 Color Flood。证据：`ui-games-screen.png` 至 `ui-games-screen-4.png`；天气截图为夜间场景，时间、城市、温度、体感与高低温完整，农场图案和飞机素材颜色正确。
- 新规则测试通过 ASan/UBSan：100 个种子的初始牌局按生成的合法顺序可解，层叠遮挡、七格判负、三张消除、消除后撤回、洗牌次数及图案数量守恒；射击位置边界、武器拾取与三级上限、碰撞伤害／无敌、击毁与装备掉落通过。日志：`ui-games-tests.log`。旧迷宫与对应测试已被替换。
- 固件编译、保留 NVS 的 USB 刷写通过（`ui-games-build.log`、`ui-games-flash.log`）。新游戏使用固定容量模型与预创建 LVGL 对象，不在每帧增建精灵。
- 初始牌局保证存在解，不代表每次任意点击或洗牌后的局面都保证可解。实际触摸翻页、飞机姿态方向／手感、完整通关和雨雪动画的真机观察仍需用户验证，截图不替代操作验收。

## 大图标菜单、公共顶栏与姿态响应（2026-10-06）

- 菜单改成每页 4 个、两列两行的 148×90 卡片，图标使用 48 px 字号；7 个应用以 4+3 分页，末页单个下排卡片居中。菜单空间利用和实际 framebuffer 见 `layout-motion-menu.png`。
- Wi-Fi 图标由 shell 在所有页面的顶端统一显示。游戏 HUD、暂停、重开、撤回、洗牌合并至导航顶栏；实际游戏区域从 300×155 增至 312×200，射击边界、敌弹／掉落回收位置、三消遮挡和牌槽同步调整。截图：`layout-motion-check.png`、`layout-motion-check-1.png`、`layout-motion-check-2.png`、`layout-motion-check-3.png`。
- 姿态使用 10 ms 固定周期读取、12 ms 时间常数滤波，并处理 roll 的 ±180° 环绕；移动映射有 0.6° 死区、可调增益和 300 px/s 限速。逻辑更新周期改为 20 ms，不将其等同于屏幕实际帧率。
- `layout-motion-tests.log`：新旧主机测试通过，包括 40 ms 内超过 90% 的倾斜阶跃响应、角度环绕、死区／灵敏度／限速，以及扩大画布后的边界和牌局回归。
- 第一轮实机日志显示低优先级采样任务在界面繁忙时会明显掉频，因此将短时姿态任务固定到 core 1、优先级 5（低于语音采集）。最终刷写日志为 `layout-motion-priority-flash.log`，未修改 NVS、Wi-Fi 或语音配置。实际手感仍需用户持机操作判断。

最终实测 `layout-motion-live.log`：游戏期间 10 秒窗口的读取速率约 70–80 Hz，最大间隔约 47–53 ms，读取失败为 0。调度目标 100 Hz 不等于持续实测达到 100 Hz。用户确认“好的，现在好多了”，完成菜单／顶栏与操控手感的本轮验收；不继续为追求标称频率修改已可用的语音和显示配置。
