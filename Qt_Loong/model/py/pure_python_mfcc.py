import numpy as np
import time

def safe_mfcc(signal, samplerate=10000, numcep=13, nfilt=26, nfft=512, lowfreq=0, highfreq=None, preemph=0.97,
              ceplifter=22, appendEnergy=True):
    """安全的MFCC实现，避免使用可能触发非法指令的操作"""
    signal = np.asarray(signal, dtype=np.float32)

    # 1. 预加重
    if preemph > 0:
        emphasized_signal = np.append(signal[0], signal[1:] - preemph * signal[:-1])
    else:
        emphasized_signal = signal

    # 2. 分帧
    frame_len = int(0.025 * samplerate)
    frame_step = int(0.01 * samplerate)
    signal_length = len(emphasized_signal)
    num_frames = int(np.ceil(float(np.abs(signal_length - frame_len)) / frame_step)) + 1

    pad_signal_length = num_frames * frame_step + frame_len
    pad_signal = np.pad(emphasized_signal, (0, pad_signal_length - signal_length), 'constant')

    indices = np.tile(np.arange(0, frame_len), (num_frames, 1)) + \
              np.tile(np.arange(0, num_frames * frame_step, frame_step), (frame_len, 1)).T
    frames = pad_signal[indices.astype(np.int32)]

    # 3. 加窗
    window = np.hamming(frame_len)
    frames *= window

    # 4. FFT和功率谱计算
    fft_result = np.fft.rfft(frames, nfft)
    mag_frame = np.abs(fft_result)
    pow_frames = (1.0 / nfft) * np.square(mag_frame)

    # 5. 梅尔滤波器组
    highfreq = highfreq or samplerate / 2
    lowmel = hz2mel(lowfreq)
    highmel = hz2mel(highfreq)
    mel_points = np.linspace(lowmel, highmel, nfilt + 2)
    hz_points = mel2hz(mel_points)
    bin = np.floor((nfft + 1) * hz_points / samplerate).astype(np.int32)

    fbank = np.zeros((nfilt, nfft // 2 + 1))
    for j in range(nfilt):
        for i in range(bin[j], bin[j + 1]):
            fbank[j, i] = (i - bin[j]) / (bin[j + 1] - bin[j])
        for i in range(bin[j + 1], bin[j + 2]):
            fbank[j, i] = (bin[j + 2] - i) / (bin[j + 2] - bin[j + 1])

    # 6. 滤波器组应用 - 使用安全的矩阵乘法
    filter_banks = safe_matrix_multiply(pow_frames, fbank.T)

    # 7. 对数运算 - 使用安全的log10
    filter_banks = np.where(filter_banks == 0, np.finfo(float).eps, filter_banks)
    filter_banks = 20 * safe_log10(filter_banks)

    # 8. DCT转换获取MFCC
    ncoeff = numcep
    n = np.arange(nfilt)
    dctm = np.zeros((ncoeff, nfilt))

    for i in range(ncoeff):
        dctm[i, :] = np.cos((i + 1) * np.pi * (n + 0.5) / nfilt)

    # 修正矩阵乘法方向：filter_banks (9,26) × dctm.T (26,13) → (9,13)
    mfcc = safe_matrix_multiply(filter_banks, dctm.T)

    # 应用提升
    if ceplifter > 0:
        nframes, ncoeff = mfcc.shape
        n = np.arange(ncoeff)
        lift = 1 + (ceplifter / 2) * np.sin(np.pi * n / ceplifter)
        mfcc *= lift

    # 附加能量(可选)
    if appendEnergy:
        energy = np.sum(pow_frames, 1)
        energy = np.where(energy == 0, np.finfo(float).eps, energy)
        energy = np.log(energy)
        mfcc[:, 0] = energy  # 用能量替换第一维MFCC

    return mfcc


def hz2mel(hz):
    return 2595 * np.log10(1 + hz / 700.0)


def mel2hz(mel):
    return 700 * (10 ** (mel / 2595.0) - 1)


def safe_log10(x):
    """安全的log10实现"""
    return np.log(x) / np.log(10)


def safe_matrix_multiply(a, b):
    """使用einsum替代dot，可能更兼容"""
    return np.einsum('ij,jk->ik', a, b)


# 使用示例
if __name__ == "__main__":
    sample_rate = 10000
    num_samples = 1024
    t = np.linspace(0, num_samples / sample_rate, num_samples, endpoint=False)
    signal = np.sin(2 * np.pi * 10000 * t)

    start_time = time.time()
    mfcc_feat = safe_mfcc(signal, samplerate=sample_rate)
    end_time = time.time()

    print(f"MFCC特征形状: {mfcc_feat.shape}")
    print(f"计算耗时: {end_time - start_time:.6f} 秒")
