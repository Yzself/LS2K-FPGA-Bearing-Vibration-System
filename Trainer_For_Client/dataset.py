import os
import numpy as np
import pandas as pd
from scipy.io import wavfile
# from python_speech_features import mfcc  # 你原来的库
from sklearn.preprocessing import LabelEncoder
import matplotlib.pyplot as plt
from tqdm import tqdm
import torch
from torch.utils.data import Dataset, DataLoader
import json  # [新增]
import sys  # [新增]

from pure_python_mfcc import safe_mfcc

plt.rcParams["font.family"] = ["SimHei", "WenQuanYi Micro Hei", "Heiti TC"]


class AccelerometerDataset(Dataset):
    """加速度计数据MFCC特征数据集"""

    def __init__(self, data_dir, sample_rate=10000, frame_length=1024, transform=None):
        """
        初始化数据集
        """
        self.data_dir = data_dir
        self.sample_rate = sample_rate
        self.frame_length = frame_length
        self.transform = transform
        self.label_encoder = LabelEncoder()
        self.data, self.labels = self._load_data()
        self.classes = self.label_encoder.classes_

    def _load_data(self):
        """加载CSV文件数据并提取标签"""
        # --- 你的原有代码，保持不变 ---
        data = []
        labels = []
        print("Python Log: 正在加载数据...", flush=True)
        csv_files = [f for f in os.listdir(self.data_dir) if f.endswith('.csv')]
        if not csv_files:
            raise ValueError(f"在目录 {self.data_dir} 中未找到CSV文件")

        # --- [新增] 进度汇报逻辑 ---
        total_files = len(csv_files)
        last_reported_percent = -1
        # --- [新增] 进度汇报逻辑结束 ---

        # --- 你的原有文件遍历循环 ---
        # [修改] 将 tqdm 包装在外面，并根据交互模式禁用
        is_interactive = sys.stdout.isatty()
        for i, csv_file in enumerate(tqdm(csv_files, desc="处理文件", disable=not is_interactive)):
            file_path = os.path.join(self.data_dir, csv_file)
            label = os.path.splitext(csv_file)[0]

            # --- [新增] 更新并汇报进度 ---
            if total_files > 0:
                current_percent = int(((i + 1) / total_files) * 100)
                if current_percent > last_reported_percent:
                    progress_data = {
                        "type": "dataset_progress",
                        "progress": current_percent
                    }
                    print(json.dumps(progress_data), flush=True)
                    last_reported_percent = current_percent
            # --- [新增] 进度汇报逻辑结束 ---

            try:
                # --- 你原有循环体内的所有逻辑，保持不变 ---
                df = pd.read_csv(file_path, header=0, usecols=[1, 2, 3])
                num_samples = len(df) // self.frame_length
                if num_samples == 0:
                    # print(f"警告: 文件 {csv_file} 的行数少于 {self.frame_length}，已跳过") # 注释掉，避免过多日志
                    continue
                for j in range(num_samples):
                    start_idx = j * self.frame_length
                    end_idx = start_idx + self.frame_length
                    x_axis = df.iloc[start_idx:end_idx, 0].values
                    y_axis = df.iloc[start_idx:end_idx, 1].values
                    z_axis = df.iloc[start_idx:end_idx, 2].values
                    x_mfcc = self._calculate_mfcc(x_axis)
                    y_mfcc = self._calculate_mfcc(y_axis)
                    z_mfcc = self._calculate_mfcc(z_axis)
                    combined_mfcc = np.stack([x_mfcc, y_mfcc, z_mfcc], axis=2)
                    data.append(combined_mfcc)
                    labels.append(label)
            except Exception as e:
                print(f"Python Error: 处理文件 {csv_file} 时出错: {str(e)}", file=sys.stderr, flush=True)

        if not data:
            raise ValueError("未能从CSV文件中提取任何数据")

        encoded_labels = self.label_encoder.fit_transform(labels)

        # --- 你的原有打印信息，保持不变 ---
        print(f"Python Log: 数据集加载完成:", flush=True)
        print(f"Python Log:   总样本数: {len(data)}", flush=True)
        print(f"Python Log:   类别数: {len(self.label_encoder.classes_)}", flush=True)
        print(f"Python Log:   类别标签: {', '.join(self.label_encoder.classes_)}", flush=True)
        print(f"Python Log:   特征形状: {data[0].shape}", flush=True)
        # a. 将类别标签的numpy数组转换为Python列表，这是JSON序列化所必需的
        class_labels_list = self.label_encoder.classes_.tolist()
        # b. 将特征形状的元组(tuple)也转换为列表，以保证JSON的兼容性
        feature_shape_list = list(data[0].shape) if data else []
        dataset_data = {
            "type": "dataset_summary",
            "sample_num": len(data),
            "class_num": len(self.label_encoder.classes_),
            "class_labels": class_labels_list,
            "feature_shape": feature_shape_list
        }
        #    ensure_ascii=False 保证中文字符能被正确编码，而不是转为\uXXXX格式
        print(json.dumps(dataset_data, ensure_ascii=False), flush=True)

        return data, encoded_labels

    def _calculate_mfcc(self, signal):
        # --- 你的原有代码，保持不变 ---
        signal = signal.astype(np.float32)
        mfcc_feat = safe_mfcc(
            signal,
            samplerate=self.sample_rate,
            numcep=13,
            nfilt=26,
            nfft=1024
        )
        return mfcc_feat

    def __len__(self):
        # --- 你的原有代码，保持不变 ---
        return len(self.data)

    def __getitem__(self, idx):
        # --- 你的原有代码，保持不变 ---
        features = self.data[idx]
        label = self.labels[idx]
        features = torch.tensor(features, dtype=torch.float32).permute(2, 0, 1)
        label = torch.tensor(label, dtype=torch.long)
        if self.transform:
            features = self.transform(features)
        return features, label

    def visualize_sample(self, idx):
        # --- 你的原有代码，保持不变 ---
        features, label = self[idx]
        features = features.numpy().transpose(1, 2, 0)
        class_name = self.classes[label.item()]
        plt.rcParams['font.family'] = 'Microsoft YaHei'
        fig, axes = plt.subplots(1, 3, figsize=(15, 5))
        for i, axis_name in enumerate(['X', 'Y', 'Z']):
            mfcc_data = features[:, :, i].T
            im = axes[i].imshow(mfcc_data, aspect='auto', origin='lower')
            axes[i].set_title(f'{axis_name}轴MFCC - {class_name}')
            axes[i].set_xlabel('帧')
            axes[i].set_ylabel('MFCC系数')
            cbar = plt.colorbar(im, ax=axes[i])
            cbar.set_label('幅度')
        plt.tight_layout()
        plt.show()