import os
import torch
from torch import nn
import time
import argparse # 用于解析命令行参数
import json     # 用于结构化输出
import sys      # 用于刷新输出流
import shutil   # 用于移动文件

from data_pretreater import AccelerometerDataPreprocessor

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

        # 全连接层，输出num_classes个类别
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


def main(watch_directory):
    # 设置设备
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Python: 使用设备: {device}", flush=True)  # flush=True 很重要
    #flush=True 会强制立即将缓冲区的内容输出到控制台，即使没有换行符。
    NUM_CLASSES = 13
    CLASS_NAMES = [
        "0.7inner", "0.7outer", "0.9inner", "0.9outer", "1.1inner",
        "1.1outer", "1.3inner", "1.3outer", "1.5inner", "1.5outer",
        "1.7inner", "1.7outer", "healthy"
    ]
    if len(CLASS_NAMES) != NUM_CLASSES:
        # 这种情况不应该发生，除非代码有误
        print(f"Python Error: CLASS_NAMES 列表的长度与 NUM_CLASSES 不匹配。", file=sys.stderr, flush=True)
        return

    # 初始化模型
    model = resnet18(num_classes=NUM_CLASSES).to(device)

    # 加载模型权重
    # 脚本和模型在同一个目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    model_path = os.path.join(script_dir, "best_model.pth")  # 模型文件在脚本同级目录 ！！！

    if os.path.exists(model_path):
        try:
            checkpoint = torch.load(model_path, map_location=device)
            model.load_state_dict(checkpoint['model_state_dict'])
            val_acc = checkpoint.get('val_acc', 'N/A')  # 兼容可能没有val_acc的情况
            if isinstance(val_acc, (float, int)):
                print(f"Python: 成功加载模型，验证准确率: {val_acc:.2f}%", flush=True)
            else:
                print(f"Python: 成功加载模型，验证准确率: {val_acc}", flush=True)
        except Exception as e:
            print(f"Python Error: 加载模型权重失败: {e}", file=sys.stderr, flush=True)
            return  # 无法加载模型，退出
    else:
        print(f"Python Error: 模型文件 {model_path} 不存在", file=sys.stderr, flush=True)
        return  # 模型文件不存在，退出

    # 设置为评估模式
    model.eval()

    # 初始化数据预处理器
    preprocessor = AccelerometerDataPreprocessor()

    # 创建一个子目录来存放已处理的文件
    processed_dir = os.path.join(watch_directory, "processed_csv")
    os.makedirs(processed_dir, exist_ok=True)
    print(f"Python: 将监视目录 '{watch_directory}' 中的CSV文件。", flush=True)
    print(f"Python: 已处理文件将移至 '{processed_dir}'。", flush=True)

    processed_files = set()  # 用于快速查找已处理的文件（可选，因为我们会移动文件, 所以用不上）

    try:
        while True:
            found_new_file = False
            for filename in os.listdir(watch_directory):
                # 有新的文件!
                if filename.endswith(".csv") and filename not in processed_files:
                    input_csv_path = os.path.join(watch_directory, filename)
                    # 确保它是一个文件而不是目录（processed_csv目录也可能被列出）
                    if not os.path.isfile(input_csv_path):
                        continue

                    found_new_file = True
                    print(f"Python: 发现新文件: {filename}", flush=True)

                    result_payload = {"file_name": filename}  # 用于JSON输出

                    try:
                        # 1.数据预处理
                        features_tensor = preprocessor.preprocess_file(input_csv_path)
                        # 转换为适合JSON序列化的Python列表,用于QT显示
                        features_for_json = features_tensor.cpu().numpy().tolist()

                        # 2. 准备模型输入
                        model_input = features_tensor.unsqueeze(0).to(device)

                        # 3. 模型预测
                        with torch.no_grad():
                            outputs = model(model_input)

                        # 4. 解析预测结果
                        probabilities = torch.softmax(outputs, dim=1)
                        confidence, predicted_idx = torch.max(probabilities, 1)

                        predicted_class_index = predicted_idx.item()
                        predicted_class_name = CLASS_NAMES[predicted_class_index]
                        prediction_confidence = confidence.item() * 100

                        # 获取所有类别的概率，并转换为Python列表
                        all_probs_list = probabilities.squeeze().cpu().numpy().tolist()

                        # 创建一个字典来存储每个类别及其对应的置信度（百分比）
                        all_class_probabilities = {
                            # 键值对，键为CLASS_NAME,值为概率
                            CLASS_NAMES[i]: prob * 100
                            for i, prob in enumerate(all_probs_list)
                        }

                        result_payload["status"] = "success"
                        result_payload["predicted_class_index"] = predicted_class_index
                        result_payload["predicted_class_name"] = predicted_class_name
                        result_payload["confidence"] = prediction_confidence
                        result_payload["features_to_display"] = features_for_json
                        result_payload["all_class_probabilities"] = all_class_probabilities

                        print(
                            f"Python: 文件 '{filename}' 预测结果: {predicted_class_name} ({prediction_confidence:.2f}%)",
                            flush=True)

                    except ValueError as e:  # 来自预处理的错误
                        print(f"Python DataError: 文件 '{filename}' 处理失败: {e}", file=sys.stderr, flush=True)
                        result_payload["status"] = "error"
                        result_payload["error_message"] = str(e)
                    except Exception as e:
                        print(f"Python Error: 文件 '{filename}' 预测过程中发生未知错误: {e}", file=sys.stderr,
                              flush=True)
                        result_payload["status"] = "error"
                        result_payload["error_message"] = f"未知错误: {e}"

                    # 将结果作为JSON打印到stdout
                    print(json.dumps(result_payload), flush=True)

                    # 移动已处理的文件
                    try:
                        destination_path = os.path.join(processed_dir, filename)
                        shutil.move(input_csv_path, destination_path)
                        # processed_files.add(filename) # 如果不移动文件，则需要这个
                        print(f"Python: 文件 '{filename}' 已移至 '{processed_dir}'", flush=True)
                    except Exception as e:
                        print(f"Python Error: 移动文件 '{filename}' 失败: {e}", file=sys.stderr, flush=True)
                        # 即使移动失败，也尝试将其标记为已处理，避免无限循环处理同一个错误文件
                        # 但这可能会导致文件在主目录中累积。更好的做法是记录下来。
                        # 为了简单起见，我们这里不添加它到 processed_files，以便下次尝试移动。

            if not found_new_file:
                # 没有新文件时，可以稍微等待一下，避免CPU空转
                time.sleep(1)  # 轮询间隔1秒
            else:
                # 如果处理了文件，可以立即再次检查，或者也稍作等待
                time.sleep(0.1)  # 短暂等待

    except KeyboardInterrupt:
        print("Python: 模型服务被用户中断。", flush=True)
    except Exception as e:
        print(f"Python Error: 主循环发生严重错误: {e}", file=sys.stderr, flush=True)
    finally:
        print("Python: 模型服务正在关闭。", flush=True)


if __name__ == "__main__":
    # Python脚本可以独立运行，并作为后端服务响应Qt应用放置在共享目录中的数据文件。
    parser = argparse.ArgumentParser(description="PyTorch模型推理服务，监视CSV文件目录。")
    parser.add_argument("watch_dir", type=str, help="需要监视的包含CSV文件的目录路径。")

    args = parser.parse_args()

    if not os.path.isdir(args.watch_dir):
        print(f"Python Error: 指定的监视目录 '{args.watch_dir}' 不存在或不是一个目录。", file=sys.stderr, flush=True)
        sys.exit(1)
    # Python脚本的 main 函数设计为一个持续运行的服务，用于监视一个指定的目录，
    # 一旦发现新的CSV文件，就加载它，用预训练的PyTorch模型进行推理，然后将结果输出，并把处理过的文件移走。
    main(args.watch_dir)


