import torch
import torch.nn as nn
from dataset import AccelerometerDataset
from torch.utils.tensorboard import SummaryWriter
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


def resnet34(num_classes=13):
    """创建一个适用于[3, 9, 13]输入尺寸的ResNet-34模型"""
    return ResNet(BasicBlock, [3, 4, 6], num_classes=num_classes)


if __name__ == "__main__":
    # 创建随机输入数据，模拟一个批次
    random_input = torch.randn(16, 3, 9, 13)  # [批次大小, 通道, 帧数, MFCC系数]

    # 初始化模型
    model = resnet18(num_classes=13)

    # 前向传播测试
    output = model(random_input)
    print(f"输出形状: {output.shape}")  # 应输出 [16, 13]（批次大小, 类别数）

    writer = SummaryWriter('resnet18')
    # 初始化模型
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = resnet18(num_classes=13).to(device)

    # 创建一个随机输入张量，用于TensorBoard可视化
    dummy_input = torch.randn(1, 3, 9, 13).to(device)  # [批次大小, 通道, 帧数, MFCC系数]

    # 将模型结构写入TensorBoard
    writer.add_graph(model, dummy_input)
    writer.close()


