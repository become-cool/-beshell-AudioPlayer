#include "sdkconfig.h"
#if !CONFIG_BESHELL_SERIAL_I2S_USE_LEGACY

#include "audio_pipeline.h"
#include "driver/i2s_std.h"
#include <beshell/qjs_utils.h>
#include <beshell/module/serial/i2s_chan_share.h>


// pipe 对应 I2S 端口的 TX 通道句柄
// （通道由 serial 模块 i2sX.setup() 时创建，未初始化返回 NULL）
static i2s_chan_handle_t pipe_tx_chan(audio_pipe_t * pipe) {
    if(!beshell_i2s_std_tx_handle) {
        return NULL ;
    }
    return (i2s_chan_handle_t) beshell_i2s_std_tx_handle(pipe->i2s) ;
}


// 向 TX 通道 DMA 缓冲区填满静音（要求通道处于 disable 状态）
static void tx_chan_preload_zeros(i2s_chan_handle_t tx) {
    uint8_t zeros[128] ;
    memset(zeros, 0, sizeof(zeros)) ;
    size_t loaded = 0 ;
    do {
        loaded = 0 ;
        if(i2s_channel_preload_data(tx, zeros, sizeof(zeros), &loaded)!=ESP_OK) {
            break ;
        }
    } while(loaded>0) ;
}


// i2s 输出控制（ng 驱动实现） -----------------------------------------------

void audio_pipe_i2s_set_clk(audio_pipe_t * pipe, uint32_t rate, uint8_t bits, uint8_t channels) {
    i2s_chan_handle_t tx = pipe_tx_chan(pipe) ;
    if(!tx) {
        return ;
    }
    if(!pipe->i2s_stopped) {
        i2s_channel_disable(tx) ;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate) ;
    i2s_channel_reconfig_std_clock(tx, &clk_cfg) ;

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        (i2s_data_bit_width_t)bits, channels==1? I2S_SLOT_MODE_MONO: I2S_SLOT_MODE_STEREO) ;
    i2s_channel_reconfig_std_slot(tx, &slot_cfg) ;

    if(!pipe->i2s_stopped) {
        i2s_channel_enable(tx) ;
    }
}

void audio_pipe_i2s_stop(audio_pipe_t * pipe) {
    if(pipe->i2s_stopped) {
        return ;
    }
    i2s_chan_handle_t tx = pipe_tx_chan(pipe) ;
    if(tx) {
        i2s_channel_disable(tx) ;
    }
    pipe->i2s_stopped = true ;
}

void audio_pipe_i2s_start(audio_pipe_t * pipe) {
    if(!pipe->i2s_stopped) {
        return ;
    }
    i2s_chan_handle_t tx = pipe_tx_chan(pipe) ;
    if(tx) {
        i2s_channel_enable(tx) ;
    }
    pipe->i2s_stopped = false ;
}

void audio_pipe_i2s_clear(audio_pipe_t * pipe) {
    i2s_chan_handle_t tx = pipe_tx_chan(pipe) ;
    if(!tx) {
        return ;
    }
    if(!pipe->i2s_stopped) {
        i2s_channel_disable(tx) ;
        tx_chan_preload_zeros(tx) ;
        i2s_channel_enable(tx) ;
    }
    else {
        tx_chan_preload_zeros(tx) ;
    }
}

// ---------------------------------------------------------------------------


void audio_el_i2s_set_volume(audio_el_i2s_t * el, uint8_t volume) {
    if(volume>100) {
        volume = 100 ;
    }
    el->volume = volume/100.0f ;
    el->use_volume = volume!=100 ;
}


// 16bit 采样扩展到 32bit 容器输出（有的芯片不支持 16bit 音源，例如 ES8156）
// 返回消耗掉的源数据字节数
static size_t el_i2s_write_expand(i2s_chan_handle_t tx, const char * data, size_t size, TickType_t ticks) {
    int16_t * samples = (int16_t *) data ;
    size_t sample_cnt = size/ sizeof(int16_t) ;
    size_t consumed = 0 ;

    int32_t buff[128] ;
    while(sample_cnt) {
        size_t chunk = sample_cnt>128? 128: sample_cnt ;
        for(size_t i=0;i<chunk;i++) {
            buff[i] = ((int32_t)samples[i]) << 16 ;
        }
        size_t written = 0 ;
        i2s_channel_write(tx, buff, chunk*sizeof(int32_t), &written, ticks) ;
        size_t chunk_consumed = written/ sizeof(int32_t) ;
        samples+= chunk_consumed ;
        sample_cnt-= chunk_consumed ;
        consumed+= chunk_consumed * sizeof(int16_t) ;
        if(chunk_consumed<chunk) {
            break ;
        }
    }
    return consumed ;
}


// 任务线程：放音
static void task_pcm_playback(audio_el_i2s_t * el) {

    EventBits_t bits ;
    int64_t t0 = gettime(), t = 0 ;

    char buff[512] ;
    uint32_t data_size ;
    size_t data_wroten ;
    char * pwrite = NULL ;

    i2s_chan_handle_t tx = NULL ;

    for(;;) {

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


        if(!el->base.upstream || !el->base.upstream->ring) {
            vTaskDelay(10) ;
            continue ;
        }

        tx = pipe_tx_chan((audio_pipe_t *)el->base.pipe) ;
        if(!tx) {
            // i2s 通道未初始化（serial.i2sX.setup() 未调用）
            vTaskDelay(10) ;
            continue ;
        }

        data_size = 0 ;
        nechof_time("i2s received %d", {
            pwrite = xRingbufferReceiveUpTo(el->base.upstream->ring, &data_size, 10, sizeof(buff));
        },data_size) ;
        if(data_size==0 || !pwrite) {

            // 确定前级已流干（ring buffer 里的可读数据可能分在头尾两端，需要两次才能读空）
            if(audio_el_is_drain(el->base.upstream)) {
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
                    // 扩展到 32bit sample
                    data_wroten = el_i2s_write_expand(tx, pwrite, data_size, portMAX_DELAY) ;
                }
                else {
                    i2s_channel_write(tx, pwrite, data_size, &data_wroten, portMAX_DELAY);
                }
            }, t, data_size, data_wroten)

            data_size-= data_wroten ;
            pwrite+= data_wroten ;
        }

        t0 = gettime() ;
    }
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
