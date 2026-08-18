import * as serial from "serial"
import * as fs from "fs"
import { AudioPlayer } from "audio"

// 初始化 I2S（引脚请按实际接线修改）
serial.i2s0.setup({
    bck: 4,    // BCK 位时钟引脚
    ws: 5,     // WS(LRCK) 字时钟引脚
    dout: 6,   // 数据输出引脚（接功放/解码芯片 DIN）
    rate: 44100,
    bits: 16,
    channels: 2
})

// 挂到 globalThis 上，方便启动后在 REPL 里继续操作
const player = globalThis.player = new AudioPlayer()
globalThis.fs = fs
player.setVolume(100)

// once() 只接受回调，包一层 Promise 以便 await
function waitStop() {
    return new Promise(resolve => player.once("stop", resolve))
}

async function main() {

    console.log("播放 /test1.mp3 ...")
    player.playMP3("/test1.mp3")
    console.log("播放停止, 是否完整播放:", !!await waitStop())

    // fs.readFileSync 一次读完整个文件，返回 ArrayBuffer
    // 播放期间 player 内部会持有该 buffer 的引用，播放结束后自动释放
    console.log("播放 /test2.mp3 (ArrayBuffer) ...")
    const mp3data = fs.readFileSync("/test2.mp3")
    console.log("mp3 数据长度:", mp3data.byteLength)
    player.playMP3(mp3data)
    console.log("播放停止, 是否完整播放:", !!await waitStop())

    console.log("播放 /test.wav ...")
    player.playWAV("/test.wav")
    console.log("播放停止, 是否完整播放:", !!await waitStop())

    console.log("全部播放结束")
}

main()

console.log("可在 REPL 中操作 player , 例如:")
console.log("    player.playMP3('/test1.mp3')")
console.log("    player.playMP3(fs.readFileSync('/test2.mp3'))   // ArrayBuffer 方式")
console.log("    player.pause() / player.resume() / player.stop()")
console.log("    player.setVolume(50)")
