# LS2K-FPGA-Bearing-Vibration-System
An intelligent bearing fault diagnosis system based on Loongson 2K1000LA, using ResNet model and MFCC for real-time vibration monitoring and edge computing. (一个基于龙芯2K1000LA的智能轴承故障诊断系统，使用ResNet模型和MFCC进行实时振动监测与边缘计算。)
---

## 📖 项目简介

本仓库包含了一套全栈式、国产化的工业轴承振动监测与智能故障诊断系统。该系统以国产**龙芯2K1000LA**处理器为核心，结合**紫光PGL22G-FPGA**进行高精度数据采集，并创新性地在边缘端部署了**残差神经网络（ResNet-18）**，实现了对旋转机械轴承健康状态的实时、本地化、智能化分析与预警。

项目旨在解决传统工业监测系统对国外技术依赖度高、智能化程度不足的问题，提供一个从硬件采集到软件分析、从边缘计算到远程监控的完整解决方案。

---

## ✨ 功能特性

*   **国产化硬件平台**: 核心计算单元采用龙芯2K1000LA，数据采集前端采用国产FPGA，实现关键技术自主可控。
*   **高精度同步采集**: 利用FPGA的并行处理能力，实现对三轴加速度传感器信号的微秒级同步采集。
*   **边缘智能诊断**: 在嵌入式龙芯平台上直接运行轻量化的ResNet-18模型，无需依赖云端算力，实现低延迟的本地故障诊断。
*   **创新特征提取**: 创造性地采用**梅尔频率倒谱系数（MFCC）**对振动信号进行特征提取，有效提升了13种不同轴承工况（含健康状态）的识别准确率（**>97%**）。
*   **丰富的数据可视化**:
    *   实时振动信号波形图
    *   MFCC特征热力图
    *   模型输出概率饼形图（仪表盘式）
    *   类别随时间变化趋势图
*   **分布式架构**:
    *   **服务器端 (龙芯)**: 负责数据采集、AI推理、TCP服务。
    *   **客户端 (PC)**: 负责远程监控、数据波形显示、以及在PC端进行模型训练。
*   **跨语言进程通信**: Qt (C++) 前端通过`QProcess`和`JSON`数据格式，与Python AI后端进行高效、稳定的多进程通信。
*   **友好的用户界面**: 基于 **Qt 5** 开发，界面美观，交互逻辑清晰，支持跨平台运行。

---

## 🛠️ 技术栈

*   **硬件**: 龙芯2K1000LA, 国产FPGA, 三轴加速度传感器 (ADXL357)
*   **软件-后端/嵌入式**: C++17, Qt 5, Python 3.7.3, PyTorch, Verilog
*   **软件-通信**: 自定义TCP/IP封包协议, JSON
*   **操作系统**: Loongnix (龙芯Linux发行版), Windows/Linux (PC端)
*   **核心算法**: ResNet-18 (轻量化), 梅尔频率倒谱系数 (MFCC)

---

## 🚀 快速开始

### 依赖项

在运行本项目前，请确保您已安装以下环境和库：

**通用:**
*   Git
*   Qt 5 或更高版本
*   支持Loongarch的交叉编译链 (loongarch64-Loongson-linux-)

**Python 环境 (用于AI后端和训练):**
*   Python 3.7.3(适配Loongnix默认环境)
*   PyTorch 1.13.1(`pip install torch`)
*   numpy 1.21.5
*   TensorBoard (`pip install tensorboard`)
*   tqdm, pandas...
*   推荐使用anaconda进行安装

**龙芯平台:**
*   已正确安装 Loongnix 操作系统。
*   已部署好WLAN驱动，用于和远程客户端进行TCP/IP通信。
*   已部署好SPI驱动，用于和FPGA通信。
*   已部署好Beep驱动，用于蜂鸣器警报。
*   已配置好Qt5开发环境
*   已配置好PyTorch 1.13.1、numpy 1.21.5等模型部署环境

### 编译与运行

#### 1. 克隆仓库
```bash
git clone https://github.com/Yzself/LS2K-FPGA-Bearing-Vibration-System.git
cd LS2K-FPGA-Bearing-Vibration-System
```

#### 2. 编译服务器端 (在龙芯平台)
```bash
# 使用 Qt Creator 打开 Qt_Loong/LoongQt.pro 文件，配置好编译器后直接编译运行。
# 或者使用在交叉编译环境(如Ubuntu)下使用命令行生成可执行文件LoongQt：
cd .\Qt_Loong
qmake
make
将生成的可执行文件LoongQt放到龙芯开发板上运行.
```

#### 3. 编译客户端 (在PC)
```bash
# 使用 Qt Creator 打开 Qt_Client/Loong_Client.pro 文件，编译运行。
# 或者使用命令行：
cd .\Qt_Client
qmake
make
./Client
```

#### 4. 模型训练 (在PC)
模型训练脚本位于 `[\LS2K-FPGA-Bearing-Vibration-System\Python]`。
```bash
cd .\Python
python train.py 
```
训练好的最佳模型将保存在 `checkpoints/best_model.pth`。你需要将此模型文件放置到龙芯服务器端指定的目录下才能进行推理。

---

## 📁 项目结构

```
.
├── Qt_Client/              # 远程客户端Qt项目
│   ├── Loong_Client.pro
│   ├── main.cpp
│   └── ...
├── Qt_Loong/               # 龙芯服务器端Qt项目
│   ├── LoongQt.pro
│   ├── main.cpp
│   └── ...
├── Python/                 # 在Pycharm上运行的Python脚本
│   ├── train.py            # 模型训练脚本
│   ├── model.py            # ResNet模型脚本
│   └── dataset.py          # 数据集处理
|   └── pure_python_mfcc.py # 梅尔倒谱系数算法脚本
├── Trainer_For_Client/     # 客户端训练调用的Python脚本
│   ├── train.py            # 模型训练脚本(QProcess调用)
│   ├── model.py            # ResNet模型脚本
│   └── dataset.py          # 数据集处理
|   └── pure_python_mfcc.py # 梅尔倒谱系数算法脚本
├── LS2K_Driver/            # 相关驱动
│   ├── Beep                # 蜂鸣器驱动
│   ├── FPGA_SPI            # SPI通信驱动
|   ├── Device_Tree_n_Kernel# 设备树dts和内核vmlinuz
├── FPGA_SPI_ADC_ADrate/rtl # FPGA端源代码                 
├── Figure/                 # 报告或README中使用的图片
├── .gitignore              # Git忽略文件
├── LICENSE                 # 开源许可证 (MIT)
└── README.md               # 项目说明文档
```

---

## 🤝 贡献

我们非常欢迎对本项目的任何贡献！无论是提交Bug报告、提出功能建议还是直接贡献代码。请通过提交 Pull Request 的方式参与进来。

1.  Fork 本仓库
2.  创建您的新分支 (`git checkout -b feature/AmazingFeature`)
3.  提交您的更改 (`git commit -m 'Add some AmazingFeature'`)
4.  将您的分支推送到远程 (`git push origin feature/AmazingFeature`)
5.  开启一个 Pull Request

---

## 📜 许可证

本项目采用 [MIT License](LICENSE) 开源许可证。

---
