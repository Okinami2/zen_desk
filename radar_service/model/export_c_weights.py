import torch
import torch.nn as nn
import numpy as np
import sys
import os

from train import RadarNet

def fuse_conv_bn(conv, bn):
    w = conv.weight.data
    b = conv.bias.data if conv.bias is not None else torch.zeros_like(bn.running_mean)
    mean = bn.running_mean
    var = bn.running_var
    gamma = bn.weight.data
    beta = bn.bias.data
    eps = bn.eps

    std = torch.sqrt(var + eps)
    w_fused = w * (gamma / std).view(-1, 1, 1, 1)
    b_fused = (b - mean) * (gamma / std) + beta
    return w_fused.flatten().tolist(), b_fused.flatten().tolist()

def write_c_array(f, name, arr):
    # arr is a flat 1D list
    f.write(f"static const float {name}[{len(arr)}] = {{\n    ")
    for i, val in enumerate(arr):
        f.write(f"{val}f, ")
        if (i + 1) % 8 == 0:
            f.write("\n    ")
    f.write("\n};\n\n")

def main():
    if not os.path.exists('radar_model.pth'):
        print("错误: 找不到 radar_model.pth!")
        sys.exit(1)
        
    model = RadarNet()
    model.load_state_dict(torch.load('radar_model.pth', map_location='cpu'))
    model.eval()

    # Fuse Conv1 + BN1
    w1, b1 = fuse_conv_bn(model.conv1[0], model.conv1[1])
    # Fuse Conv2 + BN2
    w2, b2 = fuse_conv_bn(model.conv2[0], model.conv2[1])

    # Linear weights
    w_fc1 = model.fc[0].weight.data.flatten().tolist()
    b_fc1 = model.fc[0].bias.data.flatten().tolist()
    w_fc2 = model.fc[3].weight.data.flatten().tolist()
    b_fc2 = model.fc[3].bias.data.flatten().tolist()

    # Write to C header
    out_file = "../src/radar_model_weights.h"
    with open(out_file, 'w') as f:
        f.write("/* 自动生成的雷达模型权重数据 (Fused Conv+BN) */\n")
        f.write("#ifndef RADAR_MODEL_WEIGHTS_H\n")
        f.write("#define RADAR_MODEL_WEIGHTS_H\n\n")

        # w1 shape: (64, 32, 1, 3) -> we will use it as (64, 32, 3)
        write_c_array(f, "W_CONV1", w1)
        write_c_array(f, "B_CONV1", b1)

        # w2 shape: (128, 64, 1, 3) -> (128, 64, 3)
        write_c_array(f, "W_CONV2", w2)
        write_c_array(f, "B_CONV2", b2)

        write_c_array(f, "W_FC1", w_fc1)
        write_c_array(f, "B_FC1", b_fc1)

        write_c_array(f, "W_FC2", w_fc2)
        write_c_array(f, "B_FC2", b_fc2)

        f.write("#endif // RADAR_MODEL_WEIGHTS_H\n")

    print(f"✅ 模型权重已成功导出到 {out_file}")

if __name__ == "__main__":
    main()
