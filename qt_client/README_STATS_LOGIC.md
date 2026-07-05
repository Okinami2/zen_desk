# 学习数据获取与统计逻辑说明 (Stats Logic README)

本文档说明了 `qt_client` 中的学习数据（有效专注时间、离座次数、走神次数）的累计逻辑，以及如何将雷达和后续的摄像头数据进行融合处理。

## 1. 核心计算方法 (组长的原始预设与现有实现)

**组长的原始代码设计**：
在第一阶段，组长在 `StatsPage` 界面中仅写入了“3 小时 42 分”、“5 次发呆”等纯静态文本进行界面排版和视觉验证，并未在底层 (`fusion_service`) 实现任何真实数据的长期持久化或累计。

**现阶段的真实计算实现 (由前端主导)**：
为了不重新大范围修改底层的 C 语言服务并重编，现阶段的“有效学习累计”已被直接实现在了 Qt 前端程序中（详见 `MainWindow::onStudyAccumulationTick()` 和 `StatsPage::updateStatsData()`）。

**计算公式**：
```
最终有效学习时长 = (系统实际开启倒计时的秒数) - (因离座被自动暂停的秒数) - (摄像头判定为走神发呆的秒数)
```
由于“系统实际开启倒计时的秒数”在我们这里等同于“计时器正在跑的挂机秒数”（`effectiveStudySeconds`），所以：
```
最终有效学习时长 = effectiveStudySeconds - distractedSeconds
```

## 2. 状态获取来源说明

我们的学习专注数据来源于两大核心传感器：**雷达**与**摄像头（暂未接入）**。

### 来源 A：雷达数据 (已接入并生效)
雷达负责侦测用户的“物理在座”状态，可输出以下状态：
- `STATE_SEATED_IDLE` (在座，但动作幅度较小，可能在看屏幕或发呆)
- `STATE_SEATED_WORKING` (在座，有正常的伏案工作或写字动作)
- `STATE_ABSENT` (彻底离开座位)

**代码中对雷达数据的处理逻辑**：
1. 当用户处于专注模式时，如果雷达侦测到 `STATE_ABSENT`，前端会自动将专注倒计时挂起（`studyPage->pauseTimer()`），此时不再累加有效时长，同时 `absentCount` (离座次数) 会 +1。
2. 当雷达重新侦测到 `STATE_SEATED_XXX` (归座) 时，前端会自动恢复专注倒计时，有效学习时长继续累加。

### 来源 B：摄像头数据 (预留位)
摄像头负责捕捉面部数据和视线，主要用于判定用户是否“走神（Distracted）”。
此部分目前由组长负责开发，尚未联调，因此我们在代码中做了以下**无害预留**：

在 `MainWindow.h` 中，我们预留了变量：
```cpp
int distractedCount = 0;   // 走神发呆次数
int distractedSeconds = 0; // 走神总时长（秒）
```

**如何为未来预留？**
1. 这两个变量目前的初始值均为 0，所以在后续传入 `StatsPage::updateStatsData` 进行扣减时，`effectiveSeconds - distractedSeconds` 会直接等于原有的累计挂机时间，完全**不影响现在的调试和工作**。
2. **如何对接摄像头？**
   等组长完成了摄像头的算法并能通过 UDP 下发类似 `STATE_DISTRACTED` 的状态后，只需要在 `MainWindow::onUdpReadyRead` 的 `UI_EVENT_STATE_UPDATE` 分支中，加入类似于 `if (msg.state.current_state == STATE_DISTRACTED)` 的判断，并在定时器中让 `distractedSeconds++` 即可。

## 3. 统计页面的渲染

当用户在界面侧边栏点击“统计数据”按钮时，`MainWindow::showStats()` 会被触发。
在切入图表界面前，系统会调用：
```cpp
statsPage->updateStatsData(effectiveStudySeconds, absentCount, distractedCount, distractedSeconds);
```
该函数会将内存中累计好的这 4 个动态变量，转化为小时/分钟的文本，并动态替换掉原本界面上那些写死的“3 小时 42 分”等假数据。

### 5. 学习状态评估 (Learning State Assessment)
"今日专注表现" 页面右上角的综合评分 (S/A/B/C) 会根据用户的历史专注数据动态计算，规则如下：

* **基础分（满分 100 分）：**
  * **专注率（权重 70%）：** `有效专注时间 / 总学习时间 * 100`。
  * **抗干扰分（权重 30%）：** 满分 100 分。每发生一次“走神”或“离座”，扣减 5 分（最低为 0 分）。
  * **总分：** `专注率 * 0.7 + 抗干扰分 * 0.3`

* **评级转换（A-D）：**
  * **S (极佳)：** 总分 ≥ 90 分，且**离座次数为 0**。评语：“心如止水，极致专注！”（紫色UI）
  * **A (良好)：** 总分 ≥ 80 分。评语：“状态良好，继续保持！”（绿色UI）
  * **B (一般)：** 总分 ≥ 60 分。评语：“表现及格，但还有提升空间。”（橙色UI）
  * **C (较差)：** 总分 < 60 分。评语：“频繁分心，建议稍作休息调整状态。”（红色UI）
