# beshell-AudioPlayer 示例

演示 BeShell 的 `audio` 模块通过 I2S 播放 MP3 和 WAV 文件。

启动后自动执行 `js/main.js`：

1. 通过 `serial.i2s0.setup()` 初始化 I2S 总线
2. 播放 `/test.mp3`（同步，等待播放完毕）
3. 播放 `/test.wav`

随后在 REPL 中可继续操作全局对象 `player`：

```js
player.playMP3('/test.mp3')
player.pause()
player.resume()
player.stop()
player.setVolume(50)
```

## 接线

默认引脚在 `js/main.js` 中配置，请按实际接线修改：

| 信号 | 默认 GPIO | 说明 |
|------|-----------|------|
| BCK  | 4         | 位时钟 |
| WS   | 5         | 字时钟 (LRCK) |
| DOUT | 6         | 数据输出（接功放/解码芯片 DIN，如 MAX98357A） |

## 编译烧录

```bash
cd examples/beshell-app
idf.py build flash monitor
```

`idf.py build` 时会自动将 `js/` 目录（含 js 脚本和 mp3/wav 音频文件）打包为 `img/js.bin`，`idf.py flash` 时自动烧录到 `js` 分区。

> 默认目标芯片为 esp32s3（见 `sdkconfig.defaults`），4MB 分区表见 `img/partitions-4MB.csv`。

## 音频文件

`js/test.mp3` / `js/test.wav` 由 ffmpeg 生成，可替换为自己的音频（注意采样率/位宽/声道需与 `main.js` 中 i2s setup 一致）：

```bash
# WAV (44100Hz / 16bit / 立体声)
ffmpeg -y -f lavfi -i "sine=frequency=440:duration=2" -ar 44100 -ac 2 -c:a pcm_s16le js/test.wav

# MP3 (低码率, -q:a 建议 >=7)
ffmpeg -y -i input.mp4 -ac 2 -q:a 7 -map a js/test.mp3
```
