import torch
import numpy as np
import matplotlib.pyplot as plt
from tqdm import tqdm
import os

# 设置中文显示
plt.rcParams["font.family"] = ["SimHei", "WenQuanYi Micro Hei", "Heiti TC"]

# 导入模型和数据集类
from model import resnet18
from dataset import AccelerometerDataset


def main():
    # 设置设备
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"使用设备: {device}")

    # 数据目录
    data_dir = "Triaxial Bearing Vibration Dataset-100watt"  # 替换为你的数据目录

    # 创建数据集
    print("加载数据集...")
    dataset = AccelerometerDataset(data_dir)

    # 初始化模型
    model = resnet18(num_classes=len(dataset.classes)).to(device)

    # 加载模型权重
    model_path = "checkpoints/best_model.pth"  # 替换为你的模型路径
    if os.path.exists(model_path):
        checkpoint = torch.load(model_path, map_location=device)
        model.load_state_dict(checkpoint['model_state_dict'])
        print(f"成功加载模型，验证准确率: {checkpoint['val_acc']:.2f}%")
    else:
        raise FileNotFoundError(f"模型文件 {model_path} 不存在")

    # 设置为评估模式
    model.eval()

    # 选择指定索引的样本
    selected_indices = [10, 571, 1000, 1700, 1200]
    print("\n选择的样本索引:", selected_indices)

    # 查看真实类别
    print("\n真实类别:")
    true_labels = []
    for idx in selected_indices:
        _, label = dataset[idx]
        true_class = dataset.classes[label]
        true_labels.append(label)
        print(f"样本 {idx}: 类别索引 {label}, 类别名称 '{true_class}'")

    # 预测这些样本
    print("\n模型预测结果:")
    with torch.no_grad():
        for i, idx in enumerate(selected_indices):
            # 获取样本
            features, _ = dataset[idx]
            features = features.unsqueeze(0).to(device)  # 添加批次维度

            # 模型预测
            outputs = model(features)
            probs = torch.softmax(outputs, dim=1)
            confidence, predicted_idx = torch.max(probs, 1)

            # 获取预测类别名称
            predicted_class = dataset.classes[predicted_idx.item()]

            # 输出结果
            is_correct = "✓" if predicted_idx.item() == true_labels[i] else "✗"
            print(f"样本 {idx}: "
                  f"真实类别 '{dataset.classes[true_labels[i]]}', "
                  f"预测类别 '{predicted_class}' ({confidence.item() * 100:.1f}%) {is_correct}")

if __name__ == "__main__":
    main()