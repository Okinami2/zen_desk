#include "radar_model_inference.h"
#include "radar_model_weights.h"
#include <math.h>

static float max_f(float a, float b) {
    return a > b ? a : b;
}

int radar_model_predict(const double features[10][32]) {
    int i, oc, ic, k, idx;
    float x_np[32][10];
    
    /* 1. 转换输入形状: 从 features[10][32] 转换为 x_np[32][10] */
    for (i = 0; i < 10; i++) {
        for (ic = 0; ic < 32; ic++) {
            x_np[ic][i] = (float)features[i][ic];
        }
    }
    
    /* 2. Conv1: in=32, out=64, length=10, kernel=3, pad=1 */
    float out1[64][10];
    for (oc = 0; oc < 64; oc++) {
        for (i = 0; i < 10; i++) {
            float s = B_CONV1[oc];
            for (ic = 0; ic < 32; ic++) {
                for (k = 0; k < 3; k++) {
                    idx = i - 1 + k;
                    if (idx >= 0 && idx < 10) {
                        /* W_CONV1 shape is (64, 32, 3), flat index = oc*(32*3) + ic*3 + k */
                        int w_idx = oc * 96 + ic * 3 + k;
                        s += x_np[ic][idx] * W_CONV1[w_idx];
                    }
                }
            }
            out1[oc][i] = max_f(0.0f, s); /* ReLU */
        }
    }
    
    /* 3. MaxPool1: length=10 -> 5, kernel=2 */
    float out1_pool[64][5];
    for (oc = 0; oc < 64; oc++) {
        for (i = 0; i < 5; i++) {
            out1_pool[oc][i] = max_f(out1[oc][i * 2], out1[oc][i * 2 + 1]);
        }
    }
    
    /* 4. Conv2: in=64, out=128, length=5, kernel=3, pad=1 */
    float out2[128][5];
    for (oc = 0; oc < 128; oc++) {
        for (i = 0; i < 5; i++) {
            float s = B_CONV2[oc];
            for (ic = 0; ic < 64; ic++) {
                for (k = 0; k < 3; k++) {
                    idx = i - 1 + k;
                    if (idx >= 0 && idx < 5) {
                        /* W_CONV2 shape is (128, 64, 3), flat index = oc*(64*3) + ic*3 + k */
                        int w_idx = oc * 192 + ic * 3 + k;
                        s += out1_pool[ic][idx] * W_CONV2[w_idx];
                    }
                }
            }
            out2[oc][i] = max_f(0.0f, s); /* ReLU */
        }
    }
    
    /* 5. MaxPool2: length=5 -> 2, kernel=2 */
    /* 由于 length=5，PyTorch 中如果 pad=0，floor(5/2)=2，所以输出是 2 */
    float out2_pool[128][2];
    for (oc = 0; oc < 128; oc++) {
        for (i = 0; i < 2; i++) {
            out2_pool[oc][i] = max_f(out2[oc][i * 2], out2[oc][i * 2 + 1]);
        }
    }
    
    /* 6. Flatten: 128x2 -> 256 */
    float out_flat[256];
    for (oc = 0; oc < 128; oc++) {
        out_flat[oc * 2 + 0] = out2_pool[oc][0];
        out_flat[oc * 2 + 1] = out2_pool[oc][1];
    }
    
    /* 7. Linear1: 256 -> 64 */
    float fc1_out[64];
    for (oc = 0; oc < 64; oc++) {
        float s = B_FC1[oc];
        for (ic = 0; ic < 256; ic++) {
            /* W_FC1 shape is (64, 256), flat index = oc*256 + ic */
            s += out_flat[ic] * W_FC1[oc * 256 + ic];
        }
        fc1_out[oc] = max_f(0.0f, s); /* ReLU */
    }
    
    /* 8. Linear2: 64 -> 3 */
    float fc2_out[3];
    for (oc = 0; oc < 3; oc++) {
        float s = B_FC2[oc];
        for (ic = 0; ic < 64; ic++) {
            /* W_FC2 shape is (3, 64), flat index = oc*64 + ic */
            s += fc1_out[ic] * W_FC2[oc * 64 + ic];
        }
        fc2_out[oc] = s;
    }
    
    /* 9. Argmax: 输出最大预测值的类别 */
    int max_class = 0;
    float max_val = fc2_out[0];
    if (fc2_out[1] > max_val) {
        max_val = fc2_out[1];
        max_class = 1;
    }
    if (fc2_out[2] > max_val) {
        max_val = fc2_out[2];
        max_class = 2;
    }
    
    return max_class;
}
