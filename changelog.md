

# 1.0.1 2026/8/20

## Fix

* example

# 1.0.0 2026/8/20

首个发布版本

- AudioPlayer JS 类（`audio` 模块），I2S 音频播放
- `playMP3` / `playWAV` / `playPCM`，支持文件路径或 ArrayBuffer 输入
- `pause` / `resume` / `stop` / `isPlaying` / `isPaused` / `setVolume` / `printStats`
- `stop` 事件（`finished` 参数标识是否完整播放）
- MP3：Helix 定点解码，自动跳过 ID3v2 标签，支持 320kbps 以内码率
- WAV：文件头在线解析，采样率/位宽/声道自适应
- PCM：裸数据播放，格式由参数指定
- 音频管道架构：element + ring buffer + 状态机，drain 机制保证完整播放不截断
- I2S：ng / legacy 双驱动（Kconfig 切换），运行时动态重配时钟，16bit→32bit 扩展输出，DMA 静音预填充
- 示例工程 `examples/beshell-app`
