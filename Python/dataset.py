import os
import numpy as np
import pandas as pd
from scipy.io import wavfile
from python_speech_features import mfcc
from sklearn.preprocessing import LabelEncoder
import matplotlib.pyplot as plt
from tqdm import tqdm
import torch
from torch.utils.data import Dataset, DataLoader

from pure_python_mfcc import safe_mfcc

# 设置中文显示
plt.rcParams["font.family"] = ["SimHei", "WenQuanYi Micro Hei", "Heiti TC"]


class AccelerometerDataset(Dataset):
    """加速度计数据MFCC特征数据集"""

    def __init__(self, data_dir, sample_rate=10000, frame_length=1024, transform=None):
        """
        初始化数据集

        参数:
            data_dir: 包含CSV文件的目录
            sample_rate: 采样率，单位Hz
            frame_length: 每个样本的长度（行数）
            transform: 可选的数据转换函数
        """
        self.data_dir = data_dir
        self.sample_rate = sample_rate
        self.frame_length = frame_length
        self.transform = transform
        self.label_encoder = LabelEncoder()

        # 加载数据集
        self.data, self.labels = self._load_data()
        self.classes = self.label_encoder.classes_

    def _load_data(self):
        """加载CSV文件数据并提取标签"""
        data = []  # 特征
        labels = []  # 标签

        print("正在加载数据...")
        # 获取所有CSV文件
        csv_files = [f for f in os.listdir(self.data_dir) if f.endswith('.csv')]

        if not csv_files:
            raise ValueError(f"在目录 {self.data_dir} 中未找到CSV文件")

        # 处理每个CSV文件,tqdm用于进度条显示
        for csv_file in tqdm(csv_files, desc="处理文件"):
            file_path = os.path.join(self.data_dir, csv_file)
            label = os.path.splitext(csv_file)[0]  # 使用文件名作为标签

            try:
                # 读取CSV文件，跳过时间戳列，跳过第一行(标签行)
                df = pd.read_csv(file_path, header=0, usecols=[1, 2, 3])

                # 确保数据行数是frame_length的整数倍
                num_samples = len(df) // self.frame_length
                if num_samples == 0:
                    print(f"警告: 文件 {csv_file} 的行数少于 {self.frame_length}，已跳过")
                    continue

                # 分割数据为多个样本
                for i in range(num_samples):
                    #每frame_length个数据为一个样本，将一个csv文件数据分为多个样本
                    start_idx = i * self.frame_length
                    end_idx = start_idx + self.frame_length

                    # 提取X、Y、Z轴数据并转换为numpy数组
                    x_axis = df.iloc[start_idx:end_idx, 0].values
                    y_axis = df.iloc[start_idx:end_idx, 1].values
                    z_axis = df.iloc[start_idx:end_idx, 2].values

                    # 计算每个轴的MFCC特征
                    # x_mfcc：X 轴加速度数据的 MFCC 特征，形状为(帧数, MFCC系数)
                    x_mfcc = self._calculate_mfcc(x_axis)
                    y_mfcc = self._calculate_mfcc(y_axis)
                    z_mfcc = self._calculate_mfcc(z_axis)

                    # 合并三个轴的MFCC特征，由于每个MFCC特征为二维数据，因此axis=2
                    combined_mfcc = np.stack([x_mfcc, y_mfcc, z_mfcc], axis=2)

                    #将处理好的数据加到数据集中
                    data.append(combined_mfcc)
                    labels.append(label)

            except Exception as e:
                print(f"处理文件 {csv_file} 时出错: {str(e)}")
        #
        if not data:
            raise ValueError("未能从CSV文件中提取任何数据")

        # 编码标签
        encoded_labels = self.label_encoder.fit_transform(labels)

        print(f"数据集加载完成:")
        print(f"  总样本数: {len(data)}")
        print(f"  类别数: {len(self.label_encoder.classes_)}")
        print(f"  类别标签: {', '.join(self.label_encoder.classes_)}")
        print(f"  特征形状: {data[0].shape}")

        return data, encoded_labels

    def _calculate_mfcc(self, signal):
        """计算一维信号的MFCC特征"""
        # 确保信号是正确的格式
        signal = signal.astype(np.float32)

        # 计算MFCC特征
        # numcep: 特征数量，nfilt: 滤波器数量，nfft: FFT大小
        # mfcc_feat = mfcc(signal,
        #                  samplerate=self.sample_rate,
        #                  numcep=13, # MFCC系数数量
        #                  nfilt=26,  # 滤波器组数量
        #                  nfft=1024  # 与信号长度匹配
        #                  )

        mfcc_feat = safe_mfcc(
            signal,
            samplerate=self.sample_rate,
            numcep=13,  # MFCC系数数量
            nfilt=26,  # 梅尔滤波器数量
            nfft=1024  # FFT点数
        )

        return mfcc_feat

    def __len__(self):
        """返回数据集的大小"""
        return len(self.data)

    def __getitem__(self, idx):
        """获取指定索引的样本"""
        # 获取特征和标签
        features = self.data[idx]
        label = self.labels[idx]

        # 转换为PyTorch张量
        features = torch.tensor(features, dtype=torch.float32).permute(2, 0, 1)  # [C, H, W]
        label = torch.tensor(label, dtype=torch.long)

        # 应用数据转换（如果有）
        if self.transform:
            features = self.transform(features)

        return features, label

    def visualize_sample(self, idx):
        """可视化指定索引的样本"""
        features, label = self[idx]
        features = features.numpy().transpose(1, 2, 0)  # 转回 [H, W, C]

        class_name = self.classes[label.item()]
        # 设置字体为 Microsoft YaHei，解决中文和符号显示问题
        plt.rcParams['font.family'] = 'Microsoft YaHei'

        fig, axes = plt.subplots(1, 3, figsize=(15, 5))

        # 可视化X、Y、Z轴的MFCC特征
        for i, axis_name in enumerate(['X', 'Y', 'Z']):
            # 注意：这里需要转置矩阵，使横轴表示时间（帧），纵轴表示MFCC系数
            mfcc_data = features[:, :, i].T  # 转置矩阵

            im = axes[i].imshow(mfcc_data, aspect='auto', origin='lower')
            axes[i].set_title(f'{axis_name}轴MFCC - {class_name}')
            axes[i].set_xlabel('帧')  # 横轴表示时间（帧）
            axes[i].set_ylabel('MFCC系数')  # 纵轴表示MFCC系数

            # 添加颜色条以显示值的范围
            cbar = plt.colorbar(im, ax=axes[i])
            cbar.set_label('幅度')

        plt.tight_layout()
        plt.show()


def create_data_loaders(data_dir, batch_size=32, test_size=0.2, random_state=42):
    """创建训练集和测试集的数据加载器"""
    # 创建完整数据集
    full_dataset = AccelerometerDataset(data_dir)

    # 划分训练集和测试集
    train_size = int((1 - test_size) * len(full_dataset))
    test_size = len(full_dataset) - train_size

    train_dataset, test_dataset = torch.utils.data.random_split(
        full_dataset, [train_size, test_size],
        generator=torch.Generator().manual_seed(random_state)
    )

    # 创建数据加载器
    train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True, num_workers=4)
    test_loader = DataLoader(test_dataset, batch_size=batch_size, shuffle=False, num_workers=4)

    print(f"数据加载器创建完成:")
    print(f"  训练集样本数: {len(train_dataset)}")
    print(f"  测试集样本数: {len(test_dataset)}")
    print(f"  批次大小: {batch_size}")
    print(f"  训练集批次数量: {len(train_loader)}")
    print(f"  测试集批次数量: {len(test_loader)}")

    return train_loader, test_loader, full_dataset.classes


if __name__ == "__main__":
    # 设置参数
    DATA_DIR = "Triaxial Bearing Vibration Dataset-100watt"  # 替换为你的CSV文件目录
    SAMPLE_RATE = 10000  # 采样率，Hz
    FRAME_LENGTH = 1024  # 每个样本的长度
    BATCH_SIZE = 32  # 批次大小

    # 创建数据加载器
    #train_loader, test_loader, class_names = create_data_loaders(
    #    data_dir=DATA_DIR,
    #    batch_size=BATCH_SIZE
    #)

    # 可视化示例（可选）
    dataset = AccelerometerDataset(DATA_DIR)
    print(dataset.classes[12])
    dataset.visualize_sample(0)  # 可视化第一个样本
    feature,label = dataset[1700]
    print(feature.shape)
    print(label)