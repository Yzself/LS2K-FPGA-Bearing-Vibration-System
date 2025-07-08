import os
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, random_split
from torch.utils.tensorboard import SummaryWriter
import numpy as np
from tqdm import tqdm
from sklearn.metrics import confusion_matrix, classification_report
import matplotlib.pyplot as plt
import seaborn as sns
import json
import sys
import argparse

# 设置中文显示
plt.rcParams["font.family"] = ["SimHei", "WenQuanYi Micro Hei", "Heiti TC"]

# 导入模型和数据集类
from model import resnet18
from dataset import AccelerometerDataset


def main(num_epochs_from_args=50):
    """
    主训练和验证函数。
    :param num_epochs_from_args: 从命令行传入的训练轮数。
    """
    # --- 基础设置 ---
    torch.manual_seed(42)
    np.random.seed(42)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Python Log: 使用设备: {device}", flush=True)
    train_device_data = {
        "type": "Train_device",
        "dev": str(device)
    }
    print(json.dumps(train_device_data), flush=True)
    # 判断脚本是否在交互式终端中运行，以决定是否显示tqdm进度条
    is_interactive = sys.stdout.isatty()

    # --- 数据加载 ---
    data_dir = "CollectByYzself"
    print("Python Log: 加载数据集...", flush=True)
    dataset = AccelerometerDataset(data_dir)

    train_size = int(0.8 * len(dataset))
    val_size = len(dataset) - train_size
    train_dataset, val_dataset = random_split(dataset, [train_size, val_size])

    print(f"Python Log: 训练集样本数: {len(train_dataset)}", flush=True)
    print(f"Python Log: 验证集样本数: {len(val_dataset)}", flush=True)
    train_val_data = {
        "train_dataset_num" :len(train_dataset),
        "val_dataset_num" :len(val_dataset)
    }
    print(json.dumps(train_val_data), flush=True)
    # [关键修改] 在Windows上，多进程数据加载(num_workers > 0)必须放在 if __name__ == '__main__': 块中
    # 为了稳定，我们在这里将其设置为0，禁用多进程。这能解决大量的启动崩溃问题。
    batch_size = 32
    train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True, num_workers=0, pin_memory=True)
    val_loader = DataLoader(val_dataset, batch_size=batch_size, shuffle=False, num_workers=0, pin_memory=True)

    # --- 模型、损失函数、优化器设置 ---
    model = resnet18(num_classes=len(dataset.classes)).to(device)
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, mode='min', factor=0.5, patience=5, verbose=False)

    # --- 训练准备 ---
    writer = SummaryWriter('runs/resnet18_training')
    num_epochs = num_epochs_from_args
    best_val_acc = 0.0
    save_dir = 'checkpoints'
    save_dir_data = {
        "save_dir" :save_dir,
    }
    print(json.dumps(save_dir_data), flush=True)
    os.makedirs(save_dir, exist_ok=True)
    print("Python Log: 开始训练...", flush=True)

    # --- 训练循环 ---
    for epoch in range(num_epochs):
        model.train()
        train_loss, train_correct, train_total = 0.0, 0, 0
        train_progress = tqdm(enumerate(train_loader), total=len(train_loader),
                              desc=f'Epoch {epoch + 1}/{num_epochs} [Train]',
                              file=sys.stdout,
                              disable=not is_interactive)
        for i, (inputs, labels) in train_progress:
            inputs, labels = inputs.to(device), labels.to(device)
            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, labels)
            # ==================== 实时输出训练波形开始 ====================
            # 1. 获取当前batch的loss
            current_batch_loss = loss.item()

            # 2. 计算当前batch的accuracy
            # outputs.max(1) 返回 (最大值, 最大值的索引)
            _, predicted = outputs.max(1)
            # a. 当前batch的总样本数
            batch_total = labels.size(0)
            # b. 当前batch预测正确的样本数
            batch_correct = predicted.eq(labels).sum().item()
            # c. 计算当前batch的准确率 (0.0到1.0之间)
            current_batch_acc = batch_correct / batch_total if batch_total > 0 else 0.0

            # 3. 封装当前batch的loss和accuracy为JSON对象
            batch_progress_data = {
                "type": "training_batch_progress",  # 类型名改为更通用的 progress
                "epoch": epoch + 1,
                "batch_index": i + 1,
                "total_batches": len(train_loader),
                "loss": current_batch_loss,
                "accuracy": current_batch_acc  # 新增字段
            }
            # 4. 立即将这个JSON打印出去
            print(json.dumps(batch_progress_data), flush=True)
            # ==================== 实时输出训练波形结束 ====================
            loss.backward()
            optimizer.step()
            train_loss += loss.item()
            train_total += labels.size(0)
            _, predicted = outputs.max(1)
            train_correct += predicted.eq(labels).sum().item()

        train_loss /= len(train_loader)
        train_acc = 100. * train_correct / train_total

        model.eval()
        val_loss, val_correct, val_total = 0.0, 0, 0
        with torch.no_grad():
            for i, (inputs, labels) in enumerate(val_loader):
                inputs, labels = inputs.to(device), labels.to(device)
                outputs = model(inputs)
                loss = criterion(outputs, labels)
                val_loss += loss.item()
                val_total += labels.size(0)
                _, predicted = outputs.max(1)
                val_correct += predicted.eq(labels).sum().item()

        val_loss /= len(val_loader)
        val_acc = 100. * val_correct / val_total

        scheduler.step(val_loss)

        writer.add_scalar('Loss/train', train_loss, epoch)
        writer.add_scalar('Accuracy/train', train_acc, epoch)
        writer.add_scalar('Loss/val', val_loss, epoch)
        writer.add_scalar('Accuracy/val', val_acc, epoch)

        progress_data = {
            "epoch": epoch + 1,
            "loss": train_loss,
            "accuracy": train_acc / 100.0,
            "val_loss": val_loss,
            "val_accuracy": val_acc / 100.0
        }
        print(json.dumps(progress_data), flush=True)

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            torch.save(
                {'epoch': epoch, 'model_state_dict': model.state_dict(), 'optimizer_state_dict': optimizer.state_dict(),
                 'val_acc': val_acc}, os.path.join(save_dir, 'best_model.pth'))
            print(f'Python Log: 新的最佳模型已保存！验证准确率: {val_acc:.2f}%', flush=True)
            best_model_data = {
                "type": "Model_Val",
                "val_acc": val_acc
            }
            print(json.dumps(best_model_data), flush=True)
    writer.close()
    print(f"Python Log: 训练完成！最佳验证准确率: {best_val_acc:.2f}%", flush=True)


# --- 主程序入口 ---
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='PyTorch Model Training Script for Qt Client')
    parser.add_argument('--epochs', type=int, default=10, help='Number of training epochs')
    args = parser.parse_args()

    try:
        main(num_epochs_from_args=args.epochs)
    except Exception as e:
        print(f"Python Error: {e}", file=sys.stderr, flush=True)
        sys.exit(1)