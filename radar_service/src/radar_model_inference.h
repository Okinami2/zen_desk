#ifndef RADAR_MODEL_INFERENCE_H
#define RADAR_MODEL_INFERENCE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 模型推理入口函数
 * 参数: features[10][32] - 过去 10 帧，每帧 32 个通道的特征 (前16个运动，后16个静止)
 * 返回值: 模型预测类别 (0: AWAY, 1: NORMAL, 2: FIDGET)
 */
int radar_model_predict(const double features[10][32]);

#ifdef __cplusplus
}
#endif

#endif // RADAR_MODEL_INFERENCE_H
