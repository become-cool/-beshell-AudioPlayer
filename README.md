# beshell-AudioPlayer

这是嵌入式 JavaScript 框架 BeShell 的音频播放器模块，通过 I2S 输出音频，支持：

* MP3 播放（内置 Helix MP3 定点解码器）
* WAV/PCM 播放
* 暂停 / 恢复 / 停止
* 音量调节
* 播放事件通知

内置的第三方解码库：

| 库 | 用途 |
|------|------|
| [Helix MP3](dep/helix) | MP3 定点解码 |
| [TinySoundFont](dep/TinySoundFont) | MIDI / SoundFont 音源渲染 |

## 快速开始

#### 1. 命令行安装

```
idf.py add-dependency "become-cool/beshell-AudioPlayer"
```

#### 2. 配置文件

也可以使用配置文件声明依赖。

在项目的 `main` 目录（ idf_component_register 所在目录下 ）下建立文件 `idf_component.yml`:

```yml
name: "YourProjectName"
dependencies:
  become-cool/beshell-AudioPlayer:
    version: ">=1.0.0"
```

然后重新编译项目，idf 构建工具会自动下载 beshell 和 beshell-AudioPlayer 存放到 `managed_components`

## I2S 驱动切换

组件根据 Kconfig 选项 `BESHELL_SERIAL_I2S_USE_LEGACY`（menuconfig → BeShell Configuration → Serial Module）选择 I2S 驱动实现，需与 beshell 的 serial 模块保持一致：

* 关闭（默认）：使用 ESP-IDF 新版 I2S 驱动（`driver/i2s_std.h`），播放前需先通过 `serial.i2sX.setup()` 创建 I2S 通道
* 开启：使用旧版 I2S 驱动（`driver/i2s.h`）

两种实现分别位于 `src/beshell/media/pipeline/el_i2s.c` 和 `el_i2s_legacy.c`，对外 API 不变。

## JS API

模块名为 `audio`，导出 `AudioPlayer` 类：

```js
import { AudioPlayer } from "audio"

// 参数为 I2S 端口号，默认 0（i2s0）；超出芯片实际 I2S 数量会抛出异常
// ESP32/S3/P4 可选 0/1，S2/C3/C6/H2 只有 0
const player = new AudioPlayer(0)
player.setVolume(80)

// 播放结束（或被停止）时触发
player.on("stop", (finished) => {
    console.log("播放结束, 是否完整播放:", !!finished)
})

player.playMP3("/mp3/music.mp3")
```

### 方法

| 方法 | 说明 |
|------|------|
| `playMP3(source, sync=false)` | 播放 MP3，`sync=true` 时阻塞直到播放结束 |
| `playWAV(source)` | 播放 WAV（采样率/位宽/声道从文件头解析） |
| `playPCM(source, sampleRate=16000, bits=16, channels=1)` | 播放裸 PCM（无文件头，格式由参数指定） |
| `pause()` | 暂停播放 |
| `resume()` | 恢复播放 |
| `stop(sync=false)` | 停止播放，`sync=true` 时阻塞直到停止完成 |
| `isPlaying()` | 是否正在播放 |
| `isPaused()` | 是否处于暂停状态 |
| `setVolume(vol)` | 设置音量，取值 0-100 |
| `printStats()` | 打印音频管道中各节点的运行状态（调试用） |

### 输入来源

三个 play 方法的 `source` 参数均支持两种类型：

- **文件路径**（string）：VFS 路径，如 `/test1.mp3`
- **ArrayBuffer**：内存中的完整音频数据，如 `fs.readFileSync()` 的返回值：

```js
import * as fs from "fs"

const data = fs.readFileSync("/test2.mp3")   // 返回 ArrayBuffer
player.playMP3(data)
```

播放期间播放器内部持有该 ArrayBuffer 的引用，防止被 GC 回收；播放结束后自动释放，无需手动管理。

### 事件

`AudioPlayer` 继承自 `EventEmitter`：

| 事件 | 参数 | 说明 |
|------|------|------|
| `stop` | `finished` | 播放停止时触发；`finished` 为真表示文件完整播放结束，否则为中途停止 |

## MP3 文件建议

MP3 文件可用 ffmpeg 转码，`-q:a` 选项的值应 `>=7` 以降低码率：

```
ffmpeg -i input.mp4 -ac 2 -q:a 7 -map a output.mp3
```
