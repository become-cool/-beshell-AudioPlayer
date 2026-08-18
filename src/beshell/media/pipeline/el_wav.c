#include "audio_pipeline.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>


// 解析出文件头后配置 i2s 时钟（每次播放只调用一次）
static void wav_setup_clk(audio_el_wav_t * el) {

    uint16_t bps, numch ;
    uint32_t rate ;
    memcpy(&numch, el->hbuf + offsetof(struct WavHeader, numChannels), 2) ;
    memcpy(&rate, el->hbuf + offsetof(struct WavHeader, sampleRate), 4) ;
    memcpy(&bps, el->hbuf + offsetof(struct WavHeader, bitsPerSample), 2) ;

    uint8_t ch = numch ;

    if(bps==16 && ch==1) {
        ch = 2 ;
        // bps = 32 ;
        ((audio_pipe_t *)el->base.pipe)->need_expand = true ;
    }
    else {
        ((audio_pipe_t *)el->base.pipe)->need_expand = false ;
    }

    audio_pipe_i2s_set_clk((audio_pipe_t *)el->base.pipe,
        rate,
        bps ,
        ch
    );
}


// 任务线程：从数据流解析 wav 头，透传 PCM 数据
static void task_wav(audio_el_wav_t * el) {

    EventBits_t bits ;

    size_t data_size = 0 ;
    char * pdata = NULL ;

    for(;;) {
        vTaskDelay(0) ;

        // 等待开始状态
        bits = xEventGroupWaitBits(el->base.stats, STAT_RUNNING|STAT_STOPPING, false, false, portMAX_DELAY);
        if( bits&STAT_STOPPING ) {
            audio_el_stop_when_req(el) ;

            // 复位头解析状态（phase: 0=读取文件头, 1=查找 data 块, 2=透传 PCM, 3=出错丢弃）
            el->phase = 0 ;
            el->hlen = 0 ;
            el->clen = 0 ;
            el->skip = 0 ;

            vTaskDelay(1) ;
            continue ;
        }

        if(!el->base.upstream || !el->base.upstream->ring) {
            vTaskDelay(10) ;
            continue ;
        }

        data_size = 0 ;
        pdata = xRingbufferReceiveUpTo(el->base.upstream->ring, &data_size, 20, 512);
        if ( data_size==0 || !pdata ) {

            // 确定前级已流干（ring buffer 里的可读数据可能分在头尾两端，需要两次才能读空）
            if(audio_el_is_drain(el->base.upstream)) {
                xEventGroupSetBits(el->base.upstream->stats, STAT_DRAIN) ;
                vTaskDelay(1) ;
            }
            continue ;
        }

        size_t pos = 0 ;

        // 从数据流解析 wav 头
        while( el->phase<2 && pos<data_size ) {

            // 累积固定文件头
            if(el->phase==0) {
                size_t n = sizeof(struct WavHeader) - el->hlen ;
                if(n > data_size-pos) {
                    n = data_size-pos ;
                }
                memcpy(el->hbuf+el->hlen, pdata+pos, n) ;
                el->hlen+= n ;
                pos+= n ;

                if(el->hlen == sizeof(struct WavHeader)) {
                    // 检查 RIFF 和 WAVE 标识是否合法
                    if (strncmp((char*)el->hbuf, "RIFF", 4) != 0
                        || strncmp((char*)el->hbuf + offsetof(struct WavHeader, waveHeader), "WAVE", 4) != 0)
                    {
                        printf("invalid wav file.\n") ;
                        ((audio_pipe_t *)el->base.pipe)->error = -1 ;
                        ((audio_pipe_t *)el->base.pipe)->finished = false ;
                        audio_el_set_stat( ((audio_pipe_t *)el->base.pipe)->first, STAT_STOPPING ) ;
                        // 进入丢弃状态：消化掉后续数据，等待管道停止
                        el->phase = 3 ;
                        break ;
                    }
                    wav_setup_clk(el) ;
                    el->phase = 1 ;
                }
            }

            // 查找 'data' 块
            else if(el->phase==1) {
                if(el->skip) {
                    // 跳过非 'data' 块
                    size_t n = el->skip ;
                    if(n > data_size-pos) {
                        n = data_size-pos ;
                    }
                    el->skip-= n ;
                    pos+= n ;
                }
                else {
                    size_t n = 8 - el->clen ;
                    if(n > data_size-pos) {
                        n = data_size-pos ;
                    }
                    memcpy(el->cbuf+el->clen, pdata+pos, n) ;
                    el->clen+= n ;
                    pos+= n ;

                    if(el->clen == 8) {
                        if(strncmp((char*)el->cbuf, "data", 4) == 0) {
                            // 找到 'data' 块，之后全部为 PCM 数据
                            el->phase = 2 ;
                        }
                        else {
                            memcpy(&el->skip, el->cbuf+4, 4) ;
                        }
                        el->clen = 0 ;
                    }
                }
            }
        }

        // 透传 PCM 数据（phase==2 时 pos 之后的都是 PCM 数据；phase==3 为出错丢弃）
        if(el->phase==2 && pos < data_size) {
            if(pdTRUE != xRingbufferSend(el->base.ring, pdata+pos, data_size-pos, portMAX_DELAY)) {
                printf("task wav xRingbufferSend() wrong ?????\n") ;
            }
            else {
                xEventGroupClearBits(el->base.stats, STAT_DRAIN) ;
            }
        }

        vRingbufferReturnItem(el->base.upstream->ring, pdata) ;
    }
}


audio_el_wav_t * audio_el_wav_create(audio_pipe_t * pipe, uint8_t core) {
    audio_el_wav_t * el ;
    ELEMENT_CREATE(pipe, audio_el_wav_t, el, task_wav, 1024*2, 10, core, 1024*2)
    el->base.name = "wav" ;
    return el ;
}


void audio_el_wav_delete(audio_el_wav_t * el) {
    audio_el_delete(el) ;
}
