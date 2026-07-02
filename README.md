# Zen Desk 项目说明

Zen Desk 是面向 SS928 开发板的多模态学习状态感知项目，当前主线采用“先稳定闭环，再逐步提高精度”的策略。

## 当前稳定版能力

视觉服务当前默认运行两个 NPU 模型：

1. `face_detection.om`: SCRFD 人脸检测。
2. `landmark106.om`: 106 点人脸关键点。

第三个 gaze 模型暂时不参与主流程。当前的注意力方向由 106 点规则估计得到：

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

这版目标是稳定判断“是否注意前方/是否低头/是否闭眼/是否打哈欠”，不是精确眼球 gaze。

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
- `frame_XXXXXXXX.json`: telemetry 摘要和 106 点坐标。

JSON 中重点字段：

- `has_face`
- `face`
- `yaw`
- `pitch`
- `roll`
- `attention`
- `eyes_closed`
- `yawning`
- `blink_count`
- `yawn_count`

## 第三模型现状

当前 `gaze_v2.om` 验证结果不可接受。用板端 dump 和原 ONNX 对比后：

- OM 输出 30 帧几乎恒定为 `[-0.3623, 0, 0, 0]`
- 原 ONNX 输出约为 `[0.186, -0.093, -0.904]`
- 平均向量夹角误差约 `101.6 deg`

因此主流程默认关闭第三模型：

```c
#define SAMPLE_SVP_ENABLE_GAZE_MODEL 0
```

相关调试工具保留：

```sh
python3 tools/compare_gaze_accuracy.py --dump-dir <gaze_debug_dir>
```

后续如果重新转换出可用 gaze OM，可先打开 `SAMPLE_SVP_ENABLE_GAZE_MODEL` 验证，不要直接替换稳定版判断。

## 下一步工作

1. 稳定版调参：根据实际坐姿采样调整 `attention` 的 yaw/pitch 阈值。
2. 接入融合：将 `attention/eyes_closed/yawning` 映射到 fusion_service 的学习状态。
3. 修第三模型：按原 ONNX 逻辑裁左眼、右眼并喂 head pose，再重新转换 OM。
4. 若第三模型仍不稳定，考虑轻量 iris/pupil 检测，用虹膜中心增强左右视线判断。
5. 清理调试 dump 开关，区分比赛演示模式和模型调试模式。

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
