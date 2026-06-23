import os
import pandas as pd
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from sklearn.model_selection import train_test_split
import argparse

# ============================
# 1. 定义模型 (1D-CNN)
# ============================
class RadarNet(nn.Module):
    def __init__(self, num_classes=3):
        super(RadarNet, self).__init__()
        # 输入维度: (Batch, Channels, Time_Steps) -> (N, 32, 10)
        # 32 个通道分别对应 16个门的运动能量 + 16个门的静止能量
        # 10 个时间步代表 1 秒的历史数据
        
        self.conv1 = nn.Sequential(
            nn.Conv2d(in_channels=32, out_channels=64, kernel_size=(1, 3), padding=(0, 1)),
            nn.BatchNorm2d(64),
            nn.ReLU(),
            nn.MaxPool2d(kernel_size=(1, 2)) # 时间步减半: 10 -> 5
        )
        self.conv2 = nn.Sequential(
            nn.Conv2d(in_channels=64, out_channels=128, kernel_size=(1, 3), padding=(0, 1)),
            nn.BatchNorm2d(128),
            nn.ReLU(),
            nn.MaxPool2d(kernel_size=(1, 2)) # 时间步减半: 5 -> 2
        )
        self.fc = nn.Sequential(
            nn.Linear(128 * 2, 64),
            nn.ReLU(),
            nn.Dropout(0.3),
            nn.Linear(64, num_classes)
        )

    def forward(self, x):
        # x 形状要求: (N, 32, 10)
        # 为适配底层 NPU (不支持 1D 卷积)，增加一个虚拟的高度维度 H=1 -> (N, 32, 1, 10)
        x = x.unsqueeze(2) 
        x = self.conv1(x)
        x = self.conv2(x)
        x = x.view(x.size(0), -1) # 展平: (N, 256)
        x = self.fc(x)
        return x

# ============================
# 2. 数据集加载器
# ============================
class RadarDataset(Dataset):
    def __init__(self, X, y):
        """
        X: numpy array 形状 (N, 10, 32)
        y: numpy array 形状 (N,)
        """
        # PyTorch Conv1d 要求维度是 (Batch, Channels, Length)
        # 所以要把 (N, 10, 32) 转成 (N, 32, 10)
        X = np.transpose(X, (0, 2, 1))
        self.X = torch.tensor(X, dtype=torch.float32)
        self.y = torch.tensor(y, dtype=torch.long)

    def __len__(self):
        return len(self.y)

    def __getitem__(self, idx):
        return self.X[idx], self.y[idx]

def load_and_preprocess_data(csv_path, window_size=10):
    print(f"正在加载数据: {csv_path}")
    df = pd.read_csv(csv_path)
    
    # 假设 CSV 格式为: label, m0, m1...m15, s0, s1...s15 (共 33 列)
    # label: 0=AWAY, 1=NORMAL, 2=FIDGET
    labels = df['label'].values
    features = df.drop(columns=['label']).values
    
    X = []
    y = []
    # 使用滑动窗口提取时间序列
    for i in range(len(features) - window_size):
        # 检查窗口内的标签是否一致，如果不一致则跳过（过渡态）
        window_labels = labels[i : i + window_size]
        if len(set(window_labels)) == 1:
            X.append(features[i : i + window_size])
            y.append(window_labels[-1])
            
    X = np.array(X)
    y = np.array(y)
    print(f"提取出 {len(X)} 个有效时间窗口样本.")
    return X, y

# ============================
# 3. 模拟生成假数据 (供测试用)
# ============================
def generate_mock_data(csv_path):
    print("未找到真实数据，正在生成模拟数据供测试运行...")
    data = []
    # 为了让滑动窗口(10帧)能提取到同标签的数据，我们按“块”生成连续动作
    for _ in range(50): # 50个动作块
        label = np.random.choice([0, 1, 2])
        for _ in range(20): # 每个动作保持 20 帧 (2秒)
            if label == 0:
                features = np.random.normal(loc=30, scale=2, size=32) # AWAY: 底噪
            elif label == 1:
                features = np.random.normal(loc=45, scale=5, size=32) # NORMAL: 在座
            else:
                features = np.random.normal(loc=45, scale=15, size=32) # FIDGET: 乱动
            
            row = [label] + features.tolist()
            data.append(row)
        
    cols = ['label'] + [f'm{i}' for i in range(16)] + [f's{i}' for i in range(16)]
    df = pd.DataFrame(data, columns=cols)
    df.to_csv(csv_path, index=False)
    print(f"模拟数据已保存到 {csv_path}")

# ============================
# 4. 训练流程
# ============================
def train_model(epochs=20, batch_size=32, lr=0.001):
    csv_file = "radar_data.csv"
    if not os.path.exists(csv_file):
        generate_mock_data(csv_file)
        
    X, y = load_and_preprocess_data(csv_file)
    if len(X) == 0:
        print("数据量不足以切分时间窗口！")
        return
        
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)
    
    train_loader = DataLoader(RadarDataset(X_train, y_train), batch_size=batch_size, shuffle=True)
    test_loader = DataLoader(RadarDataset(X_test, y_test), batch_size=batch_size, shuffle=False)
    
    model = RadarNet()
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=lr)
    
    print("开始训练...")
    for epoch in range(epochs):
        model.train()
        total_loss = 0
        correct = 0
        total = 0
        
        for batch_X, batch_y in train_loader:
            optimizer.zero_grad()
            outputs = model(batch_X)
            loss = criterion(outputs, batch_y)
            loss.backward()
            optimizer.step()
            
            total_loss += loss.item()
            _, predicted = outputs.max(1)
            total += batch_y.size(0)
            correct += predicted.eq(batch_y).sum().item()
            
        train_acc = 100. * correct / total
        
        # 验证测试集
        model.eval()
        test_correct = 0
        test_total = 0
        with torch.no_grad():
            for batch_X, batch_y in test_loader:
                outputs = model(batch_X)
                _, predicted = outputs.max(1)
                test_total += batch_y.size(0)
                test_correct += predicted.eq(batch_y).sum().item()
        test_acc = 100. * test_correct / test_total
        
        print(f"Epoch [{epoch+1}/{epochs}] Loss: {total_loss/len(train_loader):.4f} "
              f"Train Acc: {train_acc:.2f}% | Test Acc: {test_acc:.2f}%")
              
    # 训练结束，导出为 ONNX 格式
    export_onnx(model)

def export_onnx(model, onnx_path="radar_model.onnx", pth_path="radar_model.pth"):
    # 顺手保存一份 PyTorch 格式模型，方便在虚拟机里直接测试
    torch.save(model.state_dict(), pth_path)
    
    model.eval()
    # 创建一个 dummy 输入 (Batch_Size=1, Channels=32, Time_Steps=10)
    dummy_input = torch.randn(1, 32, 10, requires_grad=True)
    
    torch.onnx.export(
        model, 
        dummy_input, 
        onnx_path, 
        export_params=True,
        opset_version=11,          # 严格遵守 NPU 约束：opset 必须 <= 12
        do_constant_folding=True, 
        input_names=['input'], 
        output_names=['output']
        # 注意：这里去除了 dynamic_axes。因为嵌入式 NPU 转换工具(ATC)通常不支持动态批处理，
        # 在板子上推理时始终是一帧一帧(Batch=1)处理，固定 Shape 最为安全。
    )
    print(f"\n✅ 模型已成功导出为: {onnx_path}")
    print("下一步：请使用华为海思 ATC 工具将该 ONNX 模型转为 OM 模型！")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="雷达状态分类模型训练")
    parser.add_argument("--epochs", type=int, default=30, help="训练轮数")
    parser.add_argument("--batch", type=int, default=32, help="批次大小")
    parser.add_argument("--lr", type=float, default=0.001, help="学习率")
    args = parser.parse_args()
    
    train_model(epochs=args.epochs, batch_size=args.batch, lr=args.lr)
