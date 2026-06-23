import torch
import pandas as pd
import numpy as np
from train import RadarNet

def test_model(csv_file="test_data1.csv", model_path="radar_model.pth"):
    print(f"正在加载测试数据: {csv_file}")
    try:
        df = pd.read_csv(csv_file)
    except FileNotFoundError:
        print(f"[错误] 找不到文件 {csv_file}！请确保您已经把测试数据放到了当前目录。")
        return

    # 1. 预处理数据 (与 train.py 一致)
    y_raw = df['label'].values
    X_raw = df.drop(columns=['label']).values

    # 滑动窗口
    window_size = 10
    X_windows = []
    y_windows = []
    
    for i in range(len(X_raw) - window_size + 1):
        window_labels = y_raw[i : i + window_size]
        if len(set(window_labels)) == 1:
            X_windows.append(X_raw[i : i + window_size].T) # 转置为 (Channels, Time_Steps)
            y_windows.append(window_labels[0])
            
    if len(X_windows) == 0:
        print("[错误] 没有提取到有效的测试窗口，数据量太少或者标签跳变太频繁！")
        return

    X = torch.tensor(np.array(X_windows), dtype=torch.float32)
    y = torch.tensor(np.array(y_windows), dtype=torch.long)
    
    print(f"提取出 {len(X)} 个有效测试样本。")

    # 2. 加载模型
    model = RadarNet()
    try:
        model.load_state_dict(torch.load(model_path, map_location=torch.device('cpu')))
    except FileNotFoundError:
        print(f"[错误] 找不到模型文件 {model_path}！请先运行一次 python3 train.py 重新生成模型。")
        return
        
    model.eval()

    # 3. 运行推理
    print("开始推理验证...")
    with torch.no_grad():
        outputs = model(X)
        _, predicted = outputs.max(1)
        
    correct = predicted.eq(y).sum().item()
    total = y.size(0)
    acc = 100. * correct / total
    
    print(f"\n================ 验证结果 ================")
    print(f"总样本数: {total}")
    print(f"正确预测: {correct}")
    print(f"测试准确率: {acc:.2f}%\n")
    
    # 打印混淆矩阵 (Confusion Matrix)
    labels_map = {0: "离座(0)", 1: "在座(1)", 2: "乱动(2)"}
    print("真实标签 -> 预测标签 统计:")
    
    confusion = torch.zeros(3, 3, dtype=torch.int32)
    for t, p in zip(y.view(-1), predicted.view(-1)):
        confusion[t.long(), p.long()] += 1
        
    print(f"{'':<10} | {'预测 0':<10} | {'预测 1':<10} | {'预测 2':<10}")
    print("-" * 50)
    for i in range(3):
        print(f"真实 {i:<4} | {confusion[i,0].item():<10} | {confusion[i,1].item():<10} | {confusion[i,2].item():<10}")
    print("==========================================\n")

if __name__ == "__main__":
    test_model()
