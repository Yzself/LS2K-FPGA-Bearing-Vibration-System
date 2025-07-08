import os
import numpy as np
import pandas as pd
from sklearn.preprocessing import LabelEncoder
import torch
from torch.utils.data import Dataset, DataLoader

from pure_python_mfcc import safe_mfcc


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
        
        jingdu = 0
        # 处理每个CSV文件,tqdm用于进度条显示
        for csv_file in csv_files:
            jingdu += 1
            print(f"\n正在加载第{jingdu}个csv文件")
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

if __name__ == "__main__":
    # 设置参数
    DATA_DIR = "../Triaxial_Bearing_Vibration_Dataset_100watt"  # 替换为你的CSV文件目录
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
    feature,label = dataset[1700]
    print(feature.shape)
    print(label)