#include "AudioPlayer.hpp"
#include <beshell/fs/FS.hpp>
#include "soc/soc_caps.h"

using namespace std ;

namespace be::media {
    DEFINE_NCLASS_META(AudioPlayer, EventEmitter)
    std::vector<JSCFunctionListEntry> AudioPlayer::methods = {
        JS_CFUNC_DEF("playWAV", 0, AudioPlayer::playWAV),
        JS_CFUNC_DEF("playPCM", 0, AudioPlayer::playPCM),
        JS_CFUNC_DEF("playMP3", 0, AudioPlayer::playMP3),
        JS_CFUNC_DEF("pause", 0, AudioPlayer::pause),
        JS_CFUNC_DEF("resume", 0, AudioPlayer::resume),
        JS_CFUNC_DEF("stop", 0, AudioPlayer::stop),
        JS_CFUNC_DEF("isPlaying", 0, AudioPlayer::isPlaying),
        JS_CFUNC_DEF("isPaused", 0, AudioPlayer::isPaused),
        JS_CFUNC_DEF("setVolume", 0, AudioPlayer::setVolume),
        JS_CFUNC_DEF("printStats", 0, AudioPlayer::printStats),
    } ;


    AudioPlayer::AudioPlayer(JSContext * ctx, i2s_port_t i2s_num)
        : EventEmitter(ctx,build(ctx))
    {

        memset((void*)&pipe, 0, sizeof(audio_pipe_t)) ;

        pipe.i2s = i2s_num ;
        pipe.callback = (audio_pipe_event_callback_t) pipeCallback ;
        pipe.callback_opaque = this ;

        enableNativeEvent(ctx, sizeof(std::pair<const char *, int>)) ;
    }
    JSValue AudioPlayer::constructor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        uint32_t i2s_num = 0 ;
        if(argc>0) {
            if(JS_ToUint32(ctx, &i2s_num, argv[0])) {
                JSTHROW("i2s number must be a uint32")
            }
            if(i2s_num >= SOC_I2S_NUM) {
                JSTHROW("invalid i2s number: %d (this chip has %d i2s)", i2s_num, SOC_I2S_NUM)
            }
        }
        auto obj = new AudioPlayer(ctx, (i2s_port_t)i2s_num) ;
        obj->self = std::shared_ptr<AudioPlayer> (obj) ;
        return obj->jsobj ;
    }
    AudioPlayer::~AudioPlayer() {
        if(src) {
            audio_el_src_delete(src) ;
        }
        if(raw) {
            audio_el_raw_delete(raw) ;
        }
        if(mp3) {
            audio_el_mp3_delete(mp3) ;
        }
        if(wav) {
            audio_el_wav_delete(wav) ;
        }
        if(playback) {
            audio_el_i2s_delete(playback) ;
        }
        if(!JS_IsUndefined(buffer_ref)) {
            JS_FreeValue(ctx, buffer_ref) ;
            buffer_ref = JS_UNDEFINED ;
        }

        // JS_FreeValue(pipe.ctx, pipe.jsobj) ;
        // pipe.jsobj = JS_NULL ;
    }

    void AudioPlayer::pipeCallback(const char * event, int param, AudioPlayer * player) {
        // dn3(event, param, xPortGetCoreID())
        std::pair<const char *, int> event_data(event, param) ;
        player->emitNativeEvent((void *)&event_data) ;
    }

    void AudioPlayer::onNativeEvent(JSContext *ctx, void * param) {
        std::pair<const char *, int> * event_data = (std::pair<const char *, int> *)param ;
        emitSync(event_data->first, {JS_NewInt32(ctx, event_data->second)}) ;
        // 播放结束，释放 ArrayBuffer 引用
        if(!strcmp(event_data->first, "stop") && !JS_IsUndefined(buffer_ref)) {
            JS_FreeValue(ctx, buffer_ref) ;
            buffer_ref = JS_UNDEFINED ;
        }
    }
    
    void AudioPlayer::build_el_src(int core) {
        if(!src) {
            src = audio_el_src_create(&pipe, core) ;
        }
    }
    void AudioPlayer::build_el_raw(int core) {
        if(!raw) {
            raw = audio_el_raw_create(&pipe, core) ;
        }
    }
    void AudioPlayer::build_el_mp3(int core) {
        if(!mp3) {
            mp3 = audio_el_mp3_create(&pipe,core) ;
        }
    }
    void AudioPlayer::build_el_wav(int core) {
        if(!wav) {
            wav = audio_el_wav_create(&pipe,core) ;
        }
    }
    void AudioPlayer::build_el_i2s(int core) {
        if(!playback) {
            playback = audio_el_i2s_create(&pipe, core) ;
        }
    }
    
    // ffmpeg -i input.mp4 -ac 2 -q:a 7 -map a output.mp3
    // -q:a 选项的值应该 >=7
    JSValue AudioPlayer::playMP3(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        THIS_NCLASS(AudioPlayer, player)
        if(player->pipe.running) {
            JSTHROW("player is running")
        }
        ASSERT_ARGC(1)

        player->build_el_mp3(1) ;
        player->build_el_i2s(1) ;

        bool sync = false ;
        if(argc>1) {
            sync = JS_ToBool(ctx, argv[1]);
        }

        // 数据源：ArrayBuffer 使用 raw element，否则为文件路径使用 src element
        audio_el_t * source = NULL ;
        if(JS_IsArrayBuffer(argv[0])) {
            size_t ab_len = 0 ;
            uint8_t * ab = JS_GetArrayBuffer(ctx, &ab_len, argv[0]) ;
            if(!ab) {
                return JS_EXCEPTION ;
            }
            // 跳过 mp3 开头的 ID3v2 标签（size 字段不含 10 字节头）
            if(ab_len>=10 && !memcmp(ab, "ID3", 3)) {
                size_t tag_len = ((ab[6]&0x7F)<<21)|((ab[7]&0x7F)<<14)|((ab[8]&0x7F)<<7)|(ab[9]&0x7F) ;
                size_t skip = 10 + tag_len ;
                if(skip < ab_len) {
                    ab+= skip ;
                    ab_len-= skip ;
                }
            }
            player->build_el_raw(1) ;
            audio_el_raw_set_input(player->raw, ab, ab_len) ;
            source = (audio_el_t *)player->raw ;
        }
        else {
            player->build_el_src(1) ;
            string path = be::FS::toVFSPath(ctx, argv[0]) ;
            if(path.length()>=sizeof(player->src->src_path)) {
                JSTHROW("path is too long")
            }
            strcpy(player->src->src_path, path.c_str()) ;

            if(!audio_el_src_open(player->src) || !audio_el_mp3_strip(player->src)) {
                JSTHROW("file not exists") ;
            }
            source = (audio_el_t *)player->src ;
        }

        // 重置 hexli 状态
        if(xEventGroupGetBits(player->mp3->base.stats) & STAT_RUNNING) {
            JSTHROW("decoder not close yet")
        }
        audio_el_mp3_reset(player->mp3) ;

        // 清空管道
        audio_pipe_clear(&player->pipe) ;

        audio_pipe_link( &player->pipe, 3, source, player->mp3, player->playback ) ;

        player->pipe.paused = false ;
        player->pipe.running = true ;
        player->pipe.finished = false ;
        player->pipe.error = 0 ;
        // audio_pipe_emit_js(&player->pipe, "play", JS_UNDEFINED) ;

        player->pipe.need_expand = false ;

        // 持有 ArrayBuffer 引用，防止播放期间被 GC
        if(JS_IsArrayBuffer(argv[0])) {
            if(!JS_IsUndefined(player->buffer_ref)) {
                JS_FreeValue(ctx, player->buffer_ref) ;
            }
            player->buffer_ref = JS_DupValue(ctx, argv[0]) ;
        }

        audio_pipe_set_stats(&player->pipe, STAT_RUNNING) ;

        if(sync) {
            xEventGroupWaitBits(player->playback->base.stats, STAT_STOPPED, false, false, portMAX_DELAY);
        }

        return JS_UNDEFINED ;
    }

    
    JSValue AudioPlayer::playWAV(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {

        THIS_NCLASS(AudioPlayer, player)
        if(player->pipe.running) {
            JSTHROW("player is running")
        }
        CHECK_ARGC(1)
        ARGV_TO_UINT32_OPT(1, samprate, 16000)
        ARGV_TO_UINT32_OPT(2, bits, 16)
        ARGV_TO_UINT32_OPT(3, channels, 1)
        ARGV_TO_UINT32_OPT(4, ex, 0)
    
        player->build_el_wav(1) ;
        player->build_el_i2s(1) ;

        // 数据源：ArrayBuffer 使用 raw element，否则为文件路径使用 src element
        audio_el_t * source = NULL ;
        if(JS_IsArrayBuffer(argv[0])) {
            size_t ab_len = 0 ;
            uint8_t * ab = JS_GetArrayBuffer(ctx, &ab_len, argv[0]) ;
            if(!ab) {
                return JS_EXCEPTION ;
            }
            player->build_el_raw(1) ;
            audio_el_raw_set_input(player->raw, ab, ab_len) ;
            source = (audio_el_t *)player->raw ;
        }
        else {
            player->build_el_src(1) ;
            string path = be::FS::toVFSPath(ctx, argv[0]) ;
            if(path.length()>=sizeof(player->src->src_path)) {
                JSTHROW("path is too long")
            }
            strcpy(player->src->src_path, path.c_str()) ;

            // wav 头由 wav element 在数据流中解析，这里仅检查文件是否存在
            if(!audio_el_src_open(player->src)) {
                JSTHROW("file not exists") ;
            }
            source = (audio_el_t *)player->src ;
        }

        // 清空管道
        audio_pipe_clear(&player->pipe) ;

        // src/raw -> wav -> playback
        audio_pipe_link( &player->pipe, 3, source, player->wav, player->playback ) ;

        player->pipe.paused = false ;
        player->pipe.running = true ;
        player->pipe.finished = false ;
        player->pipe.error = 0 ;

        // 持有 ArrayBuffer 引用，防止播放期间被 GC
        if(JS_IsArrayBuffer(argv[0])) {
            if(!JS_IsUndefined(player->buffer_ref)) {
                JS_FreeValue(ctx, player->buffer_ref) ;
            }
            player->buffer_ref = JS_DupValue(ctx, argv[0]) ;
        }

        audio_pipe_set_stats(&player->pipe, STAT_RUNNING) ;

        return JS_UNDEFINED ;
    }

    JSValue AudioPlayer::playPCM(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {

        THIS_NCLASS(AudioPlayer, player)
        if(player->pipe.running) {
            JSTHROW("player is running")
        }
        CHECK_ARGC(1)
        ARGV_TO_UINT32_OPT(1, samprate, 16000)
        ARGV_TO_UINT32_OPT(2, bits, 16)
        ARGV_TO_UINT32_OPT(3, channels, 1)

        player->build_el_i2s(1) ;

        // 数据源：ArrayBuffer 使用 raw element，否则为文件路径使用 src element
        audio_el_t * source = NULL ;
        if(JS_IsArrayBuffer(argv[0])) {
            size_t ab_len = 0 ;
            uint8_t * ab = JS_GetArrayBuffer(ctx, &ab_len, argv[0]) ;
            if(!ab) {
                return JS_EXCEPTION ;
            }
            player->build_el_raw(1) ;
            audio_el_raw_set_input(player->raw, ab, ab_len) ;
            source = (audio_el_t *)player->raw ;
        }
        else {
            player->build_el_src(1) ;
            string path = be::FS::toVFSPath(ctx, argv[0]) ;
            if(path.length()>=sizeof(player->src->src_path)) {
                JSTHROW("path is too long")
            }
            strcpy(player->src->src_path, path.c_str()) ;

            if(!audio_el_src_open(player->src)) {
                JSTHROW("file not exists") ;
            }
            source = (audio_el_t *)player->src ;
        }

        // 裸 PCM 无文件头，采样格式由参数指定
        uint8_t ch = channels ;
        if(bits==16 && ch==1) {
            ch = 2 ;
            player->pipe.need_expand = true ;
        }
        else {
            player->pipe.need_expand = false ;
        }

        audio_pipe_i2s_set_clk(&player->pipe, samprate, bits, ch) ;
        audio_pipe_i2s_stop(&player->pipe) ;
        audio_pipe_i2s_clear(&player->pipe) ;
        vTaskDelay(100 / portTICK_PERIOD_MS) ;
        audio_pipe_i2s_start(&player->pipe) ;

        // 清空管道
        audio_pipe_clear(&player->pipe) ;

        // src/raw -> playback
        audio_pipe_link( &player->pipe, 2, source, player->playback ) ;

        player->pipe.paused = false ;
        player->pipe.running = true ;
        player->pipe.finished = false ;
        player->pipe.error = 0 ;

        // 持有 ArrayBuffer 引用，防止播放期间被 GC
        if(JS_IsArrayBuffer(argv[0])) {
            if(!JS_IsUndefined(player->buffer_ref)) {
                JS_FreeValue(ctx, player->buffer_ref) ;
            }
            player->buffer_ref = JS_DupValue(ctx, argv[0]) ;
        }

        audio_pipe_set_stats(&player->pipe, STAT_RUNNING) ;

        return JS_UNDEFINED ;
    }

    JSValue AudioPlayer::pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        
        THIS_NCLASS(AudioPlayer, player)

        audio_pipe_clear_stats(&player->pipe, STAT_RUNNING) ;
        audio_pipe_set_stats(&player->pipe, STAT_PAUSING) ;
        player->pipe.paused = true ;
        // audio_pipe_emit_js(&player->pipe, "pause", JS_UNDEFINED) ;
        
        return JS_UNDEFINED ;
    }
    JSValue AudioPlayer::resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        THIS_NCLASS(AudioPlayer, player)
        if(!player->pipe.paused) {
            return JS_UNDEFINED ;
        }
        audio_pipe_clear_stats(&player->pipe, STAT_PAUSED) ;
        audio_pipe_clear_stats(&player->pipe, STAT_PAUSING) ;
        audio_pipe_set_stats(&player->pipe, STAT_RUNNING) ;
        player->pipe.paused = false ;
        // audio_pipe_emit_js(player, "resume", JS_UNDEFINED) ;
        return JS_UNDEFINED ;
    }
    JSValue AudioPlayer::stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        THIS_NCLASS(AudioPlayer, player)
        if(!player->pipe.running) {
            return JS_UNDEFINED ;
        }
    
        audio_pipe_set_stats(&player->pipe, STAT_RUNNING) ;
        audio_el_set_stat(player->pipe.first, STAT_STOPPING) ;
        
        bool sync = false ;
        if(argc>0) {
            sync = JS_ToBool(ctx, argv[1]);
        }
        if(sync) {
            xEventGroupWaitBits(player->playback->base.stats, STAT_STOPPED, false, false, portMAX_DELAY);
        }
        
        return JS_UNDEFINED ;
    }
    JSValue AudioPlayer::isPlaying(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        THIS_NCLASS(AudioPlayer, player)
        return player->pipe.running? JS_TRUE : JS_FALSE ;
    }
    JSValue AudioPlayer::isPaused(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        THIS_NCLASS(AudioPlayer, player)
        return player->pipe.paused? JS_TRUE : JS_FALSE ;
    }

    JSValue AudioPlayer::setVolume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        
        THIS_NCLASS(AudioPlayer, player)

        CHECK_ARGC(1)
        ARGV_TO_UINT32(0, vol)

        player->build_el_i2s(1) ;
        audio_el_i2s_set_volume(player->playback, vol) ;

        return JS_UNDEFINED ;
    }

    JSValue AudioPlayer::printStats(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        
        THIS_NCLASS(AudioPlayer, player)

        for( audio_el_t * el = player->pipe.first; el; el=el->downstream) {
            printf("\n[%s]\n", el->name? el->name: "unknow el") ;
            audio_el_print_stats(el) ;
        }

        return JS_UNDEFINED ;
    }
}