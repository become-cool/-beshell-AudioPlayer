#pragma once

#include <beshell/EventEmitter.hpp>
#include "pipeline/audio_pipeline.h"

namespace be::media {
    class AudioPlayer: public be::EventEmitter {
        DECLARE_NCLASS_META
    private:
        static std::vector<JSCFunctionListEntry> methods ;
        // static std::vector<JSCFunctionListEntry> staticMethods ;

        audio_pipe_t pipe ;

        audio_el_src_t * src = nullptr ;
        audio_el_raw_t * raw = nullptr ;
        audio_el_mp3_t * mp3 = nullptr ;
        audio_el_wav_t * wav = nullptr ;
        audio_el_i2s_t * playback = nullptr ;

        JSValue buffer_ref = JS_UNDEFINED ;  // 传入 ArrayBuffer 播放时持有引用，防止播放期间被 GC

        void build_el_mp3(int core) ;
        void build_el_wav(int core) ;
        void build_el_src(int core) ;
        void build_el_raw(int core) ;
        void build_el_i2s(int core) ;

        static void pipeCallback(const char * event, int param, AudioPlayer * player) ;
    
    protected:
        void onNativeEvent(JSContext *ctx, void * param) ;

    public:
        AudioPlayer(JSContext * ctx, i2s_port_t i2s_num=I2S_NUM_0) ;
        static JSValue constructor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;

        ~AudioPlayer() ;

        static JSValue playWAV(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue playPCM(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue playMP3(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue isPlaying(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue isPaused(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue detach(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue setVolume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
        static JSValue printStats(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) ;
    
    } ;
}