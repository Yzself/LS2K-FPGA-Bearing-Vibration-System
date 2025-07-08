import os
import pandas as pd
import numpy as np
import torch
from pure_python_mfcc import safe_mfcc

class AccelerometerDataPreprocessor:
    """加速度计数据MFCC特征预处理器，用于模型推理"""

    def __init__(self, sample_rate=10000, frame_length=1024, transform=None):
        """
        初始化预处理器

        参数:
            sample_rate: 采样率，单位Hz
            frame_length: 每个样本的长度（行数）
            transform: 可选的数据转换函数 (通常用于如归一化等操作)
        """
        self.sample_rate = sample_rate
        self.frame_length = frame_length
        self.transform = transform

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

    def preprocess_file(self, csv_file_path):
        """
        预处理单个CSV文件(每个CSV为一份预测数据)

        参数:
            csv_file_path: CSV文件的路径

        返回:
            torch.Tensor: 预处理后的特征张量，形状为 [C, H, W]
        """
        if not os.path.exists(csv_file_path):
            raise FileNotFoundError(f"文件 {csv_file_path} 不存在")

        try:
            # 读取CSV文件，跳过时间戳列，跳过第一行(标签行)
            # 假设CSV文件的第0列是时间戳，1,2,3列是X,Y,Z轴数据
            df = pd.read_csv(csv_file_path, header=0, usecols=[1, 2, 3])

            # 验证数据行数是否等于frame_length
            if len(df) != self.frame_length:
                raise ValueError(
                    f"文件 {csv_file_path} 的行数 ({len(df)}) "
                    f"与期望的 frame_length ({self.frame_length}) 不符。"
                )

            # 提取X、Y、Z轴数据并转换为numpy数组
            x_axis = df.iloc[:, 0].values
            y_axis = df.iloc[:, 1].values
            z_axis = df.iloc[:, 2].values

            # 计算每个轴的MFCC特征
            x_mfcc = self._calculate_mfcc(x_axis)
            y_mfcc = self._calculate_mfcc(y_axis)
            z_mfcc = self._calculate_mfcc(z_axis)

            # 合并三个轴的MFCC特征，由于每个MFCC特征为二维数据，因此axis=2
            # x_mfcc, y_mfcc, z_mfcc 的形状是 [num_frames, num_cep]
            # np.stack后形状是 [num_frames, num_cep, 3]
            combined_mfcc = np.stack([x_mfcc, y_mfcc, z_mfcc], axis=2)

            # 转换为PyTorch张量并调整维度顺序
            # 原始Dataset中是 .permute(2, 0, 1)，对应 [C, H, W]
            # 这里 C=3 (轴数), H=num_frames (MFCC帧数), W=num_cep (MFCC系数数量)
            features = torch.tensor(combined_mfcc, dtype=torch.float32).permute(2, 0, 1)

            # 应用数据转换（如果有）
            if self.transform:
                features = self.transform(features)

            return features

        except Exception as e:
            print(f"处理文件 {csv_file_path} 时出错: {str(e)}")
            raise # 重新抛出异常，让调用者处理


# --- 使用示例 ---
if __name__ == '__main__':
    # 假设我们有一个虚拟的CSV文件
    dummy_csv_file = "dummy_accelerometer_data.csv"
    FRAME_LENGTH = 1024
    SAMPLE_RATE = 10000

    # 创建虚拟数据 (时间戳, X, Y, Z)
    # header
    header = "timestamp,accel_x,accel_y,accel_z\n"
    # data
    data_rows = [f"{i*0.0001},{np.sin(i*0.1)},{np.cos(i*0.1)},{np.sin(i*0.2)}\n" for i in range(FRAME_LENGTH)]

    with open(dummy_csv_file, "w") as f:
        f.write(header)
        for row in data_rows:
            f.write(row)

    print(f"创建了虚拟CSV文件: {dummy_csv_file}，包含 {FRAME_LENGTH} 行数据。")

    # 初始化预处理器
    # 如果你有特定的transform，例如归一化，可以在这里传入
    # from torchvision import transforms
    # example_transform = transforms.Normalize(mean=[0.5, 0.5, 0.5], std=[0.5, 0.5, 0.5]) # 示例
    preprocessor = AccelerometerDataPreprocessor(
        sample_rate=SAMPLE_RATE,
        frame_length=FRAME_LENGTH,
        # transform=example_transform # 如果需要
    )

    try:
        # 预处理数据
        processed_features = preprocessor.preprocess_file(dummy_csv_file)
        print(f"\n预处理后的特征形状: {processed_features.shape}")
        # 预期的形状类似于 [3, num_mfcc_frames, 13]
        # 例如，如果MFCC计算得到9帧，则形状为 [3, 9, 13]

        # 现在 processed_features 可以作为模型的输入
        # model_input = processed_features.unsqueeze(0) # 如果模型需要batch维度
        # output = model(model_input)
        # print(f"模型输入形状 (带batch): {model_input.shape}")

    except Exception as e:
        print(f"预处理失败: {e}")
    finally:
        # 清理虚拟文件
        if os.path.exists(dummy_csv_file):
            os.remove(dummy_csv_file)
            print(f"删除了虚拟CSV文件: {dummy_csv_file}")