import os
import torch
from torch import nn
from torch.utils.data import DataLoader
from dataset_on_loong import AccelerometerDataset

class BasicBlock(nn.Module):
    """基本残差块：包含两个卷积层和一个残差连接"""
    expansion = 1

    def __init__(self, in_channels, out_channels, stride=1, downsample=None):
        super(BasicBlock, self).__init__()
        self.conv1 = nn.Conv2d(in_channels, out_channels, kernel_size=3, stride=stride,
                               padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(out_channels)
        self.relu = nn.ReLU(inplace=True)
        self.conv2 = nn.Conv2d(out_channels, out_channels, kernel_size=3, stride=1,
                               padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(out_channels)
        self.downsample = downsample

    def forward(self, x):
        identity = x

        out = self.conv1(x)
        out = self.bn1(out)
        out = self.relu(out)

        out = self.conv2(out)
        out = self.bn2(out)

        if self.downsample is not None:
            identity = self.downsample(x)

        out += identity
        out = self.relu(out)
        return out


class ResNet(nn.Module):
    """适合小尺寸输入的残差网络"""

    def __init__(self, block, layers, num_classes=13, zero_init_residual=False):
        super(ResNet, self).__init__()
        self.in_channels = 16  # 减小初始通道数，适应小尺寸输入

        # 调整初始卷积层参数，避免过度降维
        self.conv1 = nn.Conv2d(3, 16, kernel_size=3, stride=1, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(16)
        self.relu = nn.ReLU(inplace=True)
        # 移除maxpooling层，避免进一步缩小特征图
        # 调整残差层结构
        self.layer1 = self._make_layer(block, 16, layers[0], stride=1)
        self.layer2 = self._make_layer(block, 32, layers[1], stride=2)  # 仅在这一层进行下采样
        self.layer3 = self._make_layer(block, 64, layers[2], stride=2)  # 仅在这一层进行下采样

        # 全局平均池化，适应不同尺寸的输入
        self.avgpool = nn.AdaptiveAvgPool2d((1, 1))

        # 全连接层，输出13个类别
        self.fc = nn.Linear(64 * block.expansion, num_classes)

        # 权重初始化
        for m in self.modules():
            if isinstance(m, nn.Conv2d):
                nn.init.kaiming_normal_(m.weight, mode='fan_out', nonlinearity='relu')
            elif isinstance(m, nn.BatchNorm2d):
                nn.init.constant_(m.weight, 1)
                nn.init.constant_(m.bias, 0)

        # 可选：初始化残差连接的最后一个BN层为0
        if zero_init_residual:
            for m in self.modules():
                if isinstance(m, BasicBlock):
                    nn.init.constant_(m.bn2.weight, 0)

    def _make_layer(self, block, out_channels, blocks, stride=1):
        downsample = None
        if stride != 1 or self.in_channels != out_channels * block.expansion:
            downsample = nn.Sequential(
                nn.Conv2d(self.in_channels, out_channels * block.expansion,
                          kernel_size=1, stride=stride, bias=False),
                nn.BatchNorm2d(out_channels * block.expansion),
            )

        layers = []
        layers.append(block(self.in_channels, out_channels, stride, downsample))
        self.in_channels = out_channels * block.expansion
        for _ in range(1, blocks):
            layers.append(block(self.in_channels, out_channels))
        return nn.Sequential(*layers)

    def forward(self, x):
        x = self.conv1(x)
        x = self.bn1(x)
        x = self.relu(x)

        x = self.layer1(x)
        x = self.layer2(x)
        x = self.layer3(x)

        x = self.avgpool(x)
        x = x.view(x.size(0), -1)
        x = self.fc(x)

        return x


def resnet18(num_classes=13):
    """创建一个适用于[3, 9, 13]输入尺寸的ResNet-18模型"""
    return ResNet(BasicBlock, [2, 2, 2], num_classes=num_classes)


if __name__ == "__main__":
    # 设置设备
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"使用设备: {device}")

    # 加载数据集
    DATA_DIR = "../Triaxial_Bearing_Vibration_Dataset_100watt"
    dataset = AccelerometerDataset(DATA_DIR)

    # 初始化模型
    model = resnet18(num_classes=len(dataset.classes)).to(device)

    # 加载模型权重
    model_path = "../checkpoints/best_model.pth"  # 模型路径
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


