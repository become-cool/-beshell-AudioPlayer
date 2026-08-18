#include "audio_pipeline.h"
#include <stdbool.h>
#include <string.h>


audio_el_raw_t * audio_el_raw_create(audio_pipe_t * pipe, uint8_t core) {
    audio_el_raw_t * el ;
    ELEMENT_CREATE(pipe, audio_el_raw_t, el, task_raw, 1024*3, 10, core, 1024*2)
    el->base.name = "raw" ;
    return el ;
}


// 设置输入数据（不持有数据所有权，播放期间调用方需保证数据有效）
void audio_el_raw_set_input(audio_el_raw_t * el, const void * data, size_t len) {
    el->input = (const uint8_t *) data ;
    el->input_len = len ;
    el->pos = 0 ;
}


void audio_el_raw_delete(audio_el_raw_t * el) {
    audio_el_delete(el) ;
}


// 任务线程：输入数据推流
void task_raw(audio_el_raw_t * el) {

    EventBits_t bits ;
    size_t chunk ;

    while(1) {

        // 等待开始状态
        bits = xEventGroupWaitBits(el->base.stats, STAT_RUNNING|STAT_STOPPING, false, false, portMAX_DELAY);
        if( bits&STAT_STOPPING ) {
            audio_el_stop_when_req(el) ;
            vTaskDelay(1) ;
            continue ;
        }

        if(!el->input || !el->input_len) {
            printf("task_raw() no input data\n") ;
            goto finish ;
        }

        // 分块送入 ring buffer（input 长度由外部传入，与 ring buffer 大小无关）
        chunk = el->input_len - el->pos ;
        if(chunk) {
            if(chunk>512) {
                chunk = 512 ;
            }
            if(pdTRUE != xRingbufferSend(el->base.ring, el->input + el->pos, chunk, portMAX_DELAY)) {
                printf("task raw xRingbufferSend() faild ???") ;
            }
            else {
                el->pos+= chunk ;
                xEventGroupClearBits(el->base.stats, STAT_DRAIN) ;
            }
            vTaskDelay(0) ;
            continue ;
        }

        // 所有输入数据已进入 ring buffer，触发 stopping
finish:
        ((audio_pipe_t *)el->base.pipe)->finished = true ;
        audio_el_set_stat( el, STAT_STOPPING ) ;
        vTaskDelay(10) ;
        continue;

    }
}
