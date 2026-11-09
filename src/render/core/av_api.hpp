#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

#include "util/storage.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace grape::render {

#define GRAPE_AV_FUNCTIONS(X)        \
    X(avformat_alloc_output_context2) \
    X(avcodec_find_encoder_by_name)   \
    X(avcodec_alloc_context3)         \
    X(avcodec_get_supported_config)   \
    X(av_dict_set)                    \
    X(avcodec_open2)                  \
    X(av_dict_free)                   \
    X(avformat_new_stream)            \
    X(avcodec_parameters_from_context) \
    X(avio_open)                      \
    X(avformat_write_header)          \
    X(av_frame_alloc)                 \
    X(av_frame_get_buffer)            \
    X(av_packet_alloc)                \
    X(av_frame_unref)                 \
    X(av_frame_make_writable)         \
    X(av_image_fill_linesizes)        \
    X(avcodec_send_frame)             \
    X(avcodec_receive_packet)         \
    X(av_packet_rescale_ts)           \
    X(av_interleaved_write_frame)     \
    X(av_packet_unref)                \
    X(av_channel_layout_default)      \
    X(swr_alloc_set_opts2)            \
    X(swr_init)                       \
    X(swr_convert)                    \
    X(av_write_trailer)               \
    X(avio_close)                     \
    X(swr_free)                       \
    X(avcodec_free_context)           \
    X(av_packet_free)                 \
    X(av_frame_free)                  \
    X(avformat_free_context)

class AvApi {
   public:
#define GRAPE_AV_DECLARE_FIELD(name) decltype(::name)* name = nullptr;
    GRAPE_AV_FUNCTIONS(GRAPE_AV_DECLARE_FIELD)
#undef GRAPE_AV_DECLARE_FIELD

    geode::Result<> load();

    void unload();

    bool loaded() const { return m_loaded; }

    ~AvApi() { unload(); }

   private:
    void* resolve(char const* name) const;

    std::vector<void*> m_modules;
    bool m_loaded = false;
};

}
