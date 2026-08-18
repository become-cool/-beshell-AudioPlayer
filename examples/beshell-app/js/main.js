import * as serial from "serial"
import { AudioPlayer } from "audio"

// 初始化 I2S（引脚请按实际接线修改）
serial.i2s0.setup({
    bck: 41,    // BCK 位时钟引脚
    ws: 40,     // WS(LRCK) 字时钟引脚
    dout: 42,   // 数据输出引脚（接功放/解码芯片 DIN）
    rate: 44100,
    bits: 16,
    channels: 2
})

// 挂到 globalThis 上，方便启动后在 REPL 里继续操作
const player = global.player = new AudioPlayer()
player.setVolume(100)

// 播放结束（或被停止）时触发
player.on("stop", (finished) => {
    console.log("播放停止, 是否完整播放:", !!finished)
})

console.log("播放 /test.mp3 ...")
player.playMP3("/test.mp3", true)   // sync=true, 阻塞直到播放完毕

console.log("播放 /test.wav ...")
player.playWAV("/test.wav", 44100, 16, 2)

console.log("完成。可在 REPL 中继续操作 player , 例如:")
console.log("    player.playMP3('/test.mp3')")
console.log("    player.pause() / player.resume() / player.stop()")
