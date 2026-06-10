# ASR 语音控制及全链路网络联动更新日志

本文档记录了我们在本次开发过程中，为实现**语音模块接入**、**融合中心流转**以及**Qt 客户端 UI 联动**所做出的所有核心代码修改。如果在未来的调试或上板测试中遇到问题，可以参考此文档进行回溯。

---

## 🚨 最新硬件与界面调试总结 (重要必读)
在进行真机联调测试时，我们发现了以下几个极其核心的问题，需重点跟进：
1. **语音模块硬件接线规范**：组长在底层统一了引脚规范，语音模块（UART5）的 RX 和 TX 分别绑定在了海鸥派的物理插槽 **35** 和 **40**。实际接线时**必须使用交叉线**（模块的 TX 接板子的 35，模块的 RX 接板子的 40）。
2. **串口地址 Hardcode Bug (已修复)**：开发初期在虚拟机本地测试时，`serial_setup.h` 中将串口地址硬编码为了 `/dev/ttyUSB0`。上板实测前我们已将其紧急修正为动态引用组长规范的 `BOARD_VOICE_UART_DEVICE`（即 `/dev/ttyAMA5`），确保了真机数据通信的畅通。
3. **老版 Qt 客户端的“单机脱机”限制**：通过深入排查发现，板子上目前运行的组长老版本 Qt 界面，其内部使用的是 `MockFusionClient`。该模块完全依赖本地定时器（`QTimer`）循环发射假状态，**并没有任何真实的 TCP/UDP 网络交互代码**。这就是为什么底层硬件（如台灯）已经能响应语音模块的控制指令，但 UI 界面却毫无反应的原因。
   - **解决方案**：我们在 Ubuntu 中修改的包含 `QUdpSocket` (监听 8889 端口) 和麦克风响应图标的**新版 Qt 代码是正确且必须的**。后续跟进时，只需利用交叉编译器将这份新代码编译出对应的 ARM `.bin` 文件并覆盖板子上的旧文件，即可实现最终的全屏幕自动跳转联动。

---


## 1. 语音服务模块 (`asr_service`) 核心集成
我们从零构建了 `asr_service` 独立进程，以保障系统的健壮性和解耦：
- **目录结构建立**：提取原始代码至 `asr_service/src` 与 `include`。
- **串口设备配置**：在 `serial_setup.h` 中将默认串口设为 `/dev/ttyUSB0`（上板若使用板载引脚，需修改为 `/dev/ttyAMA1` 等）。
- **进程鲁棒性修复**：
  - 在 `main.c` 中添加了 `signal(SIGPIPE, SIG_IGN)`，防止因对端 Socket 强退导致整个语音进程崩溃。
  - 在 `asr_controller.c` 中修复了连接失败或断开时的文件描述符泄漏问题（`close(g_socket_fd)` 并置为 `-1`），并加入了**断线自动重连**机制。
- **指令解析拓宽**：除基础指令外，新增了唤醒 (`0x00`)、专注25分钟 (`0x25`)、45分钟 (`0x26`)、60分钟 (`0x27`) 的十六进制指令解析并向融合中心转发。

## 2. 核心通信协议更新 (`common/include/protocol.h`)
为支持语音和 UI 联动，扩展了公共协议结构：
- **新消息类型**：加入 `MSG_ASR_COMMAND = 0x06`。
- **状态携带时长**：在 `FusionState` 结构体中新增了 `uint16_t duration_minutes;`，以支持向 UI 传递专注倒计时时长。
- **UI 专属事件包**：新增了 `UiEventType` 枚举和 `UiEventMessage` 结构体，专用于 `fusion_service` 通过 UDP 广播向外下发界面绘制指令（如弹出麦克风、同步状态等）。

## 3. 融合中心状态机重构 (`fusion_service/src/fusion_service.c`)
融合中心不仅要能接纳语音数据，还要解决多传感器的数据冲突：
- **TCP 分支新增**：在 `tcp_server_thread` 中加入了处理 `MSG_ASR_COMMAND` 的分支，根据接收到的具体十六进制码，设置 `duration_minutes` 时长参数，并将系统置为 `STATE_FOCUSED`。
- **🔥 雷达强制覆盖 Bug 修复**：
  - **原逻辑缺陷**：雷达只要探测到有人（`presence=1`），就会无脑将全局状态覆盖为 `STATE_SEATED_IDLE`，导致语音刚开启的专注模式瞬间被打断。
  - **新逻辑**：雷达只在“无人(`STATE_ABSENT`)”时，发现有人才切换为“闲置(`STATE_SEATED_IDLE`)”。如果当前已经是专注模式，雷达的“有人”信号将被静默吸收，专注状态得以受到保护。雷达探测到“无人”时，仍旧执行离座操作。
- **UDP 广播通道建设**：
  - 新增 `g_udp_fd` 套接字，指向本机 `127.0.0.1:8889` 端口。
  - 收到语音唤醒 (`0x00`) 时触发 `fusion_send_ui_event(UI_EVENT_WAKEUP_ASR)`。
  - 状态切换时通过 `fusion_send_state` 发送 `UI_EVENT_STATE_UPDATE` UDP 包。

## 4. Qt 客户端免手势全自动联动 (`qt_client`)
通过网络监听，使界面可以自行“动”起来：
- **工程配置**：`qt_client.pro` 追加 `network` 模块支持。
- **UDP 监听引擎**：在 `MainWindow` 构造函数中挂载 `QUdpSocket` 绑定 `8889` 端口，实时监听后台事件。
- **状态同步跳转 (`UI_EVENT_STATE_UPDATE`)**：
  - 收到专注事件时：自动关闭当前的一切交互弹窗，并调用 `startStudy(minutes)` 丝滑切入全屏倒计时界面。
  - 收到离座/结束事件时：自动调用 `stopStudy()` 返回主页。
- **唤醒视觉反馈 (`UI_EVENT_WAKEUP_ASR`)**：
  - 在 `MainWindow` 的右下角悬浮创建了一个紫色半透明边框的麦克风（🎤）`QLabel`。
  - 配合 `QTimer::singleShot`，实现收到唤醒指令后图标立即浮现，并在 3 秒后无人值守式地自动渐隐消失。

## 5. 环境妥协与编译配置 (`Makefile` & `sample_comm_isp.c`)
- **虚拟机缺库屏蔽**：由于当前 Ubuntu 虚拟机的海思 SDK 库文件缺失（缺少 `libsns_os08a20.a` 等驱动），为了能在本地完成 `make` 全局编译验证，注释了 `sample_comm_isp.c` 中相关的报错摄像头调用逻辑。
- *(注：上板时若物理硬件确实使用了 OS08A20 摄像头，需要将其取消注释，并确保板子的 SDK 环境完整。)*

## 6. 涉及变更的文件清单汇总

为了方便追踪，以下是我们在此次开发过程中**新建**和**修改**的所有核心代码文件：

### 🟢 新增/提取的文件
这些文件主要是为了把原有的零散串口测试代码打包成稳健的模块化服务：
- `asr_service/Makefile` (负责编译该模块为海鸥派/本地可执行文件)
- `asr_service/include/asr_controller.h` (指令解析的头文件)
- `asr_service/include/serial_setup.h` (串口配置及 `/dev/ttyUSB0` 宏定义)
- `asr_service/src/asr_controller.c` (处理 0x25, 0x00 等解析及网络 TCP 转发逻辑)
- `asr_service/src/main.c` (主入口，带防宕机保护与断线重连守护循环)
- `asr_service/src/serial_setup.c` (打开串口、设置波特率与无阻塞读写)
- `asr_service/README.md` (本文档自身)

### 🟡 修改的已有文件
- **顶层构建**：
  - `Makefile`（在构建链中添加了 `asr_service`）
  - `Makefile.param`（注释了本地不存在的传感器驱动以防报错）
- **核心协议与中枢**：
  - `common/include/protocol.h`（添加了新协议号、`UiEventMessage`结构体及倒计时长字段）
  - `fusion_service/src/fusion_service.c`（处理指令逻辑、修复雷达覆盖Bug、开启 UDP `8889` 端口并发送广播）
  - `vo_init/sample_comm_isp.c`（注释本地无用的 ISP 摄像头报错调用以通过编译）
- **前端客户端界面**：
  - `qt_client/qt_client.pro`（引入了 `network` 模块支持网络接收）
  - `qt_client/src/MainWindow.h`（增加了 `QUdpSocket`，麦克风图标控件，以及相应槽函数的声明）
  - `qt_client/src/MainWindow.cpp`（实现了 UDP 收发解析、呼出/自动关闭弹窗、页面全自动切换及麦克风图标的限时展示逻辑）
