# Zen Desk 项目说明

Zen Desk 是面向 SS928 开发板的多模态学习状态感知项目，当前主线采用“先稳定闭环，再逐步提高精度”的策略。

## 当前稳定版能力

视觉服务当前默认运行四个 NPU 模型，其中坐姿检测按低频调度执行：

1. `face_detection.om`: SCRFD 人脸检测，每帧执行。
2. `landmark106.om`: 106 点人脸关键点，每帧在检测到人脸后执行。
3. `pose_detector.om`: MediaPipe Pose 检测，默认 2 秒执行一次。
4. `pose_landmarks_detector.om`: MediaPipe Pose 关键点，默认 2 秒执行一次。

当前的注意力方向由 106 点规则估计得到：

- 左右眼中心：landmark `33-42`、`87-96`
- 鼻尖/鼻下部：landmark `80-85`
- 嘴巴区域：landmark `52-71`
- 输出 `yaw/pitch/roll`，并在 telemetry/snapshot JSON 中给出 `attention`

`attention` 取值：

- `front`: 面向前方
- `left`: 头部左偏
- `right`: 头部右偏
- `up`: 抬头
- `down`: 低头
- `eyes_closed`: 闭眼
- `no_face`: 未检测到人脸
- `error`: 推理失败

坐姿模型不会每帧运行，默认 0.5Hz，用上一轮结果缓存到每帧 telemetry，避免额外 resize 把整体帧率拖慢。

`posture` 取值：

- `normal`: 坐姿正常
- `shoulder_tilt`: 肩部明显倾斜
- `lean_left`: 身体左倾
- `lean_right`: 身体右倾
- `head_offset`: 头部相对肩部明显偏移
- `unknown`: 关键点置信度不足
- `no_pose`: 未检测到人体

这版目标是稳定判断“是否注意前方/是否低头/是否闭眼/是否打哈欠/坐姿是否规范”。

## 目录结构

- `vision_service/`: 摄像头采集、人脸检测、关键点、注意力方向和调试输出。
- `radar_service/`: 毫米波雷达串口接入。
- `fusion_service/`: 多模态融合、状态机和 EC11 旋钮处理。
- `asr_service/`: 语音模块接入。
- `device_service/`: 设备控制相关代码。
- `qt_client/`: 屏幕 UI。
- `scripts/`: 板端启动、停止和硬件测试脚本。
- `tools/`: PC/WSL 辅助分析脚本。
- `out/`: 编译产物、日志、运行时文件。

## 编译

在 WSL 项目目录执行：

```sh
cd /home/okinami/pegasus/platform/ss928v100_gcc/smp/a55_linux/mpp/zen_desk
make
```

如果提示 `aarch64-openeuler-linux-gnu-gcc: No such file or directory`，说明当前 shell 没有加载交叉编译器环境，需要先恢复 SDK 工具链 PATH。

构建会把 SVP NPU 运行时动态库同步到 `out/lib/svp_npu`。运行脚本会自动加载 `scripts/env.sh`，设置 `LD_LIBRARY_PATH`。

## 板端运行

常规后台服务：

```sh
cd /root/zen_desk
./scripts/start_all.sh
```

`start_all.sh` 默认会启动 fusion/radar/asr/vision，其中 vision 会：

- 发送 UDP telemetry 到 `127.0.0.1:8889`
- 保存 snapshot 到 `/root/zen_desk/out/snapshots`
- 每 10 帧保存一次，最多 100 组

如果只想启动非视觉服务：

```sh
VISION_ENABLE=0 ./scripts/start_all.sh
```

停止服务：

```sh
./scripts/stop_all.sh
```

带屏幕 UI：

```sh
./scripts/start_everything.sh
```

单独运行视觉服务并保存调试帧：

```sh
./out/bin/vision_service \
  --device /dev/video0 \
  --format MJPEG \
  --width 1280 \
  --height 720 \
  --snapshot-dir /root/zen_desk/out/snapshots \
  --snapshot-every 10 \
  --snapshot-limit 100
```

如果要独占 HDMI 预览：

```sh
./out/bin/vision_service --hdmi-preview
```

## 视觉调试输出

开启 `--snapshot-dir` 后会保存：

- `frame_XXXXXXXX.raw.nv21`: 原始 NV21 帧。
- `frame_XXXXXXXX.annotated.ppm`: 带人脸框和关键点的图。
- `frame_XXXXXXXX.json`: telemetry 摘要、106 点人脸坐标和低频 pose 坐标。

JSON 中重点字段：

- `has_face`
- `face`
- `yaw`
- `pitch`
- `roll`
- `attention`
- `posture`
- `posture_ok`
- `pose_present`
- `pose_age_ms`
- `eyes_closed`
- `yawning`
- `blink_count`
- `yawn_count`


## 下一步工作

1. 稳定版调参：根据实际坐姿采样调整 `attention` 的 yaw/pitch 阈值。
2. 接入融合：将 `attention/posture/eyes_closed/yawning` 映射到 fusion_service 的学习状态。
3. 清理调试 dump 开关，区分比赛演示模式和模型调试模式。

## 常见问题

如果 `vision_service.log` 中出现：

```text
dlopen libsvp_aicpu.so failed
```

说明运行环境没有加载 SVP NPU 动态库。先确认：

```sh
ls /root/zen_desk/out/lib/svp_npu/libsvp_aicpu.so
. /root/zen_desk/scripts/env.sh
echo $LD_LIBRARY_PATH
```

然后重新启动：

```sh
./scripts/stop_all.sh
./scripts/start_everything.sh
```
