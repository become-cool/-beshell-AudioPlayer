#include "sdkconfig.h"
#if CONFIG_BESHELL_SERIAL_I2S_USE_LEGACY

#include "audio_pipeline.h"
#include "driver/i2s.h"
#include <beshell/qjs_utils.h>


// i2s 输出控制（legacy 驱动实现） -----------------------------------------------

void audio_pipe_i2s_set_clk(audio_pipe_t * pipe, uint32_t rate, uint8_t bits, uint8_t channels) {
    i2s_set_clk(pipe->i2s, rate, (i2s_bits_per_sample_t)bits, (i2s_channel_t)channels) ;
}

void audio_pipe_i2s_stop(audio_pipe_t * pipe) {
    i2s_stop(pipe->i2s) ;
}

void audio_pipe_i2s_start(audio_pipe_t * pipe) {
    i2s_start(pipe->i2s) ;
}

void audio_pipe_i2s_clear(audio_pipe_t * pipe) {
    i2s_zero_dma_buffer(pipe->i2s) ;
}

// ---------------------------------------------------------------------------


void audio_el_i2s_set_volume(audio_el_i2s_t * el, uint8_t volume) {
    if(volume>100) {
        volume = 100 ;
    }
    el->volume = volume/100.0f ;
    el->use_volume = volume!=100 ;
}


// 任务线程：放音
static void task_pcm_playback(audio_el_i2s_t * el) {

    // printf("task_pcm_playback()\n") ;

    EventBits_t bits ;
    int idle_ticks = 10/ portTICK_PERIOD_MS ;
    int64_t t0 = gettime(), t = 0 ;

    char buff[512] ;
    uint32_t data_size ;
    size_t data_wroten ;
    char * pwrite = NULL ;

    for(int i=0;i<10;) {
        
        vTaskDelay(0) ;

        // 等待状态
        bits = xEventGroupWaitBits(el->base.stats, STAT_RUNNING|STAT_STOPPING|STAT_PAUSING, false, false, portMAX_DELAY);

        // 停止信号
        if( bits&STAT_STOPPING ) {
            audio_el_stop_when_req(el) ;
            vTaskDelay(1) ;
            continue ;
        }
        // 暂停信号
        else if( bits&STAT_PAUSING ) {
            audio_pipe_i2s_clear((audio_pipe_t *)el->base.pipe) ;
            audio_el_set_stat(el, STAT_PAUSED) ;
            vTaskDelay(1) ;
            continue ;
        }


        // dp(el->base.upstream->ring)
        // EL_RECV_UPSTREAM(el, pwrite, buff, sizeof(buff), data_size)

        if(!el->base.upstream || !el->base.upstream->ring) {
            vTaskDelay(10) ;
            continue ;
        }
        data_size = 0 ;
        nechof_time("i2s received %d", {
            pwrite = xRingbufferReceiveUpTo(el->base.upstream->ring, &data_size, 10, sizeof(buff));
        },data_size) ;
        if(data_size==0 || !pwrite) {

            // printf("el_i2s receive empty\n") ;
            
            // 确定前级已流干（ring buffer 里的可读数据可能分在头尾两端，需要两次才能读空）
            if(audio_el_is_drain(el->base.upstream)) {
                // printf("i2s's input drain\n") ;
                audio_pipe_i2s_clear((audio_pipe_t *)el->base.pipe) ;
                xEventGroupSetBits(el->base.upstream->stats, STAT_DRAIN) ;  
                vTaskDelay(1) ;
            }

            continue ;
        }
        
        memcpy(buff, pwrite, data_size) ;
        vRingbufferReturnItem(el->base.upstream->ring, pwrite) ;
        
        pwrite = buff ;
            
        t = gettime()-t0 ;

        while(data_size) {

            data_wroten = 0 ;

            // 软件音量调节（16bit PCM 就地缩放，写入后 pwrite 前移，每个采样只缩放一次）
            if(el->use_volume) {
                int16_t * samples = (int16_t*)pwrite ;
                size_t sample_count = data_size / sizeof(int16_t) ;
                for (size_t i = 0; i < sample_count; i++) {
                    samples[i] = (int16_t)(samples[i] * el->volume) ;
                }
            }

            nechof_time("delay:%lld,%d->%d", {
                if(((audio_pipe_t*)el->base.pipe)->need_expand) {
                    // 扩展到 32 sample                
                    i2s_write_expand(((audio_pipe_t *)el->base.pipe)->i2s, pwrite, data_size, 16, 32, &data_wroten, portMAX_DELAY );
                }
                else {
                    i2s_write(((audio_pipe_t *)el->base.pipe)->i2s, pwrite, data_size, &data_wroten, portMAX_DELAY);
                }
                // dn2(data_size, data_wroten)
            }, t, data_size, data_wroten)

            data_size-= data_wroten ;
            pwrite+= data_wroten ;
        }

        t0 = gettime() ;
        // i++ ;

    }

    while(1){}
}

audio_el_i2s_t * audio_el_i2s_create(audio_pipe_t * pipe, uint8_t core) {
    audio_el_i2s_t * el = NULL ;
    ELEMENT_CREATE(pipe, audio_el_i2s_t, el, task_pcm_playback, 1024*3, 10, core, 0)
    el->base.name = "i2s" ;
    return el ;
}

        
void audio_el_i2s_delete(audio_el_i2s_t * el) {
    audio_pipe_i2s_clear((audio_pipe_t *)el->base.pipe) ;
    audio_el_delete(el) ;
}

#endif