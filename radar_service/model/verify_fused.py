import torch
import torch.nn as nn
import numpy as np

# Load the model
from train import RadarNet
model = RadarNet()
model.load_state_dict(torch.load('radar_model.pth', map_location='cpu'))
model.eval()

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
    return w_fused, b_fused

# Fuse Conv1 + BN1
w1, b1 = fuse_conv_bn(model.conv1[0], model.conv1[1])
# Fuse Conv2 + BN2
w2, b2 = fuse_conv_bn(model.conv2[0], model.conv2[1])

# Linear weights
w_fc1 = model.fc[0].weight.data
b_fc1 = model.fc[0].bias.data
w_fc2 = model.fc[3].weight.data
b_fc2 = model.fc[3].bias.data

# Create a random input
x = torch.randn(1, 32, 10) # (Batch, Channels, Time_Steps)
out_pt = model(x)

# Pure Python numpy implementation to verify logic
x_np = x.numpy().reshape(32, 10)
w1_np = w1.numpy().reshape(64, 32, 3)
b1_np = b1.numpy()

out1 = np.zeros((64, 10))
for oc in range(64):
    for i in range(10):
        s = b1_np[oc]
        for ic in range(32):
            for k in range(3):
                idx = i - 1 + k # padding=1
                if 0 <= idx < 10:
                    s += x_np[ic, idx] * w1_np[oc, ic, k]
        out1[oc, i] = max(0, s) # ReLU

# MaxPool1
out1_pool = np.zeros((64, 5))
for oc in range(64):
    for i in range(5):
        out1_pool[oc, i] = max(out1[oc, i*2], out1[oc, i*2+1])

# Conv2
w2_np = w2.numpy().reshape(128, 64, 3)
b2_np = b2.numpy()
out2 = np.zeros((128, 5))
for oc in range(128):
    for i in range(5):
        s = b2_np[oc]
        for ic in range(64):
            for k in range(3):
                idx = i - 1 + k
                if 0 <= idx < 5:
                    s += out1_pool[ic, idx] * w2_np[oc, ic, k]
        out2[oc, i] = max(0, s)

# MaxPool2
out2_pool = np.zeros((128, 2))
for oc in range(128):
    for i in range(2):
        out2_pool[oc, i] = max(out2[oc, i*2], out2[oc, i*2+1])

# Flatten
out_flat = out2_pool.flatten() # 128*2 = 256

# Linear 1
fc1_out = np.maximum(0, np.dot(w_fc1.numpy(), out_flat) + b_fc1.numpy())

# Linear 2
fc2_out = np.dot(w_fc2.numpy(), fc1_out) + b_fc2.numpy()

print("PyTorch Output:", out_pt.detach().numpy())
print("Numpy Output:  ", fc2_out)
print("Difference:    ", np.abs(out_pt.detach().numpy() - fc2_out).max())
