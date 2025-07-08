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

# 设置中文显示
plt.rcParams["font.family"] = ["SimHei", "WenQuanYi Micro Hei", "Heiti TC"]

# 导入模型和数据集类
from model import resnet18, resnet34
from dataset import AccelerometerDataset


def main():
    # 设置随机种子，确保结果可复现
    torch.manual_seed(42)
    np.random.seed(42)

    # 设置设备
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"使用设备: {device}")

    # 数据目录
    data_dir = "CollectByYzself"  # 数据目录

    # 创建数据集
    print("加载数据集...")
    dataset = AccelerometerDataset(data_dir)

    # 划分训练集和验证集
    train_size = int(0.8 * len(dataset))
    val_size = len(dataset) - train_size
    train_dataset, val_dataset = random_split(dataset, [train_size, val_size])

    print(f"训练集样本数: {len(train_dataset)}")
    print(f"验证集样本数: {len(val_dataset)}")

    # 创建数据加载器
    batch_size = 32
    train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True, num_workers=4)
    val_loader = DataLoader(val_dataset, batch_size=batch_size, shuffle=False, num_workers=4)

    # 初始化模型
    model = resnet18(num_classes=len(dataset.classes)).to(device)

    # 定义损失函数和优化器
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    # 在训练过程中根据某个指标（如损失值）的变化情况动态调整学习率
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, mode='min', factor=0.5, patience=5, verbose=True)

    # 创建TensorBoard写入器
    writer = SummaryWriter('runs/resnet18_training')

    # 可视化模型结构
    dummy_input = torch.randn(1, 3, 9, 13).to(device)
    writer.add_graph(model, dummy_input)

    # 训练参数
    num_epochs = 50
    best_val_acc = 0.0
    save_dir = 'checkpoints' #模型保存路径
    os.makedirs(save_dir, exist_ok=True)

    # 训练循环
    print("开始训练...")
    for epoch in range(num_epochs):
        print("-----第{}轮训练开始-----".format(epoch + 1))
        # 训练阶段
        model.train()
        train_loss = 0.0      #统计每轮训练的平均loss = 每轮训练总loss/每轮训练次数
        train_correct = 0
        train_total = 0

        train_progress = tqdm(enumerate(train_loader), total=len(train_loader),
                              desc=f'Epoch {epoch + 1}/{num_epochs} [Train]')
        for i, (inputs, labels) in train_progress:
            inputs, labels = inputs.to(device), labels.to(device)

            # 梯度清零
            optimizer.zero_grad()

            # 前向传播
            outputs = model(inputs)
            loss = criterion(outputs, labels)

            # 反向传播和优化
            loss.backward()
            optimizer.step()

            # 统计
            train_loss += loss.item()
            train_total += labels.size(0)
            _, predicted = outputs.max(1)
            train_correct += predicted.eq(labels).sum().item()

            # 更新进度条
            train_progress.set_postfix({'loss': train_loss / (i + 1), 'acc': 100. * train_correct / train_total})

        # 计算训练指标
        train_loss /= len(train_loader)
        train_acc = 100. * train_correct / train_total

        # 验证阶段
        model.eval()
        val_loss = 0.0
        val_correct = 0
        val_total = 0
        all_labels = []
        all_preds = []

        with torch.no_grad():
            val_progress = tqdm(enumerate(val_loader), total=len(val_loader),
                                desc=f'Epoch {epoch + 1}/{num_epochs} [Val]')
            for i, (inputs, labels) in val_progress:
                inputs, labels = inputs.to(device), labels.to(device)

                # 前向传播
                outputs = model(inputs)
                loss = criterion(outputs, labels)

                # 统计
                val_loss += loss.item()
                val_total += labels.size(0)
                _, predicted = outputs.max(1)
                val_correct += predicted.eq(labels).sum().item()

                # 保存所有标签和预测结果用于混淆矩阵
                all_labels.extend(labels.cpu().numpy())
                all_preds.extend(predicted.cpu().numpy())

                # 更新进度条
                val_progress.set_postfix({'loss': val_loss / (i + 1), 'acc': 100. * val_correct / val_total})

        # 计算验证指标
        val_loss /= len(val_loader)
        val_acc = 100. * val_correct / val_total

        # 学习率调整
        scheduler.step(val_loss)

        # 记录到TensorBoard
        writer.add_scalar('Loss/train', train_loss, epoch)
        writer.add_scalar('Loss/val', val_loss, epoch)
        writer.add_scalar('Accuracy/train', train_acc, epoch)
        writer.add_scalar('Accuracy/val', val_acc, epoch)
        writer.add_scalar('Learning Rate', optimizer.param_groups[0]['lr'], epoch)

        # 打印训练信息
        print(f'Epoch {epoch + 1}/{num_epochs}, '
              f'Train Loss: {train_loss:.4f}, Train Acc: {train_acc:.2f}%, '
              f'Val Loss: {val_loss:.4f}, Val Acc: {val_acc:.2f}%')

        # 保存最佳模型
        if val_acc > best_val_acc:
            best_val_acc = val_acc
            torch.save({
                'epoch': epoch,
                'model_state_dict': model.state_dict(),
                'optimizer_state_dict': optimizer.state_dict(),
                'val_acc': val_acc,
            }, os.path.join(save_dir, 'best_model.pth'))
            print(f'模型保存成功！验证准确率: {val_acc:.2f}%')

    #到此训练结束
    # 关闭TensorBoard写入器
    writer.close()

    # 加载最佳模型进行最终评估
    checkpoint = torch.load(os.path.join(save_dir, 'best_model.pth'))
    model.load_state_dict(checkpoint['model_state_dict'])
    model.eval()

    # 最终评估
    print("\n最终评估结果:")
    test_loss = 0.0
    test_correct = 0
    test_total = 0
    all_labels = []
    all_preds = []

    with torch.no_grad():
        for inputs, labels in val_loader:
            inputs, labels = inputs.to(device), labels.to(device)

            outputs = model(inputs)
            loss = criterion(outputs, labels)

            test_loss += loss.item()
            test_total += labels.size(0)
            _, predicted = outputs.max(1)
            test_correct += predicted.eq(labels).sum().item()

            all_labels.extend(labels.cpu().numpy())
            all_preds.extend(predicted.cpu().numpy())

    test_loss /= len(val_loader)
    test_acc = 100. * test_correct / test_total

    print(f'最终测试损失: {test_loss:.4f}, 最终测试准确率: {test_acc:.2f}%')

    # 打印分类报告
    print("\n分类报告:")
    print(classification_report(all_labels, all_preds, target_names=dataset.classes))

    # 绘制混淆矩阵
    cm = confusion_matrix(all_labels, all_preds)
    plt.figure(figsize=(12, 10))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                xticklabels=dataset.classes, yticklabels=dataset.classes)
    plt.xlabel('预测类别')
    plt.ylabel('真实类别')
    plt.title('混淆矩阵')
    plt.tight_layout()
    plt.savefig(os.path.join(save_dir, 'confusion_matrix.png'))
    plt.show()

    print(f"训练完成！最佳模型已保存至 {os.path.join(save_dir, 'best_model.pth')}")


if __name__ == "__main__":
    main()