#include "audio_pipeline.h"
#include <stdbool.h>
#include <string.h>


audio_el_src_t *  audio_el_src_create(audio_pipe_t * pipe, uint8_t core) {
    audio_el_src_t * el ;
    ELEMENT_CREATE(pipe, audio_el_src_t, el, task_src, 1024*5, 10, core, 1024*2)
    el->base.name = "src" ;
    return el ;
}


// 打开源文件（不做任何格式解析）
bool audio_el_src_open(audio_el_src_t * el) {

    if(el->file){
        fclose(el->file) ;
        el->file = NULL ;
    }
    el->file = fopen(el->src_path,"rb") ;
    if(!el->file) {
        printf("can not open file: %s", el->src_path) ;
        return false ;
    }

    return true ;
}


void audio_el_src_delete(audio_el_src_t * el) {
    if(el->file) {
        fclose(el->file) ;
        el->file = NULL ;
    }
    audio_el_delete(el) ;
}


// 任务线程：文件读入
void task_src(audio_el_src_t * el) {
    // printf("task_src()\n") ;
    // int cmd ;

    char buff[512] ;
    size_t read_bytes ;
    EventBits_t bits ;

    while(1) {

        // 等待开始状态
        bits = xEventGroupWaitBits(el->base.stats, STAT_RUNNING|STAT_STOPPING, false, false, portMAX_DELAY);
        if( bits&STAT_STOPPING ) {
            audio_el_stop_when_req(el) ;

            if(el->file) {
                fclose(el->file) ;
                el->file = NULL ;
            }

            vTaskDelay(1) ;
            continue ;
        }

        if(!el->file) {
            printf("task_src() open file: %s\n", el->src_path) ;
            el->file = fopen(el->src_path,"rb") ;
            if(!el->file) {
                printf("can not open file: %s", el->src_path) ;
                goto finish ;
            }
                printf("file opened: %s", el->src_path) ;
        }

        nechof_time("fs read, bytes %d", {
            read_bytes = fread(buff, 1, sizeof(buff), el->file) ;
        }, read_bytes)
        if(!read_bytes) {
            goto finish ;
            continue ;
        }

        if(pdTRUE != xRingbufferSend(el->base.ring, buff, read_bytes, portMAX_DELAY)) {
            printf("task src xRingbufferSend() faild ???") ;
        }
        else {
            xEventGroupClearBits(el->base.stats, STAT_DRAIN) ;
        }
        vTaskDelay(0) ;
        continue;
finish:
        ((audio_pipe_t *)el->base.pipe)->finished = true ;
        audio_el_set_stat( el, STAT_STOPPING ) ;
        vTaskDelay(10) ;
        continue;

    }
}
