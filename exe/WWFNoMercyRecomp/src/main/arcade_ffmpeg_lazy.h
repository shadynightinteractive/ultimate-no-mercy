#pragma once
// [xplat 2026-11-12] FFmpeg loaded on first use, on Linux, the Steam Deck and the Mac (PLAN.md decision D3): the same rule the
// Windows build keeps with delay-loaded DLLs (CMakeLists.txt, arcade_media_decode.cpp ffmpeg_ready): the game starts, and
// plays every entrance the old way, without FFmpeg's libraries beside it.
//
// Windows can ask the linker to do this. Linux and the Mac can't, so it is done by hand and it is small: arcade_media_decode.cpp
// is the only file that calls FFmpeg and uses 50 of its functions. The program is linked WITHOUT the libraries (no
// DT_NEEDED for them, so the loader never looks for them at start-up); the first time a video is asked for, load() opens the five
// libraries and looks each of those 50 functions up by name. Then every call in arcade_media_decode.cpp, written as it always
// was (av_frame_alloc(...)), goes through the table below: the macros at the bottom rename the function to the table's pointer.
// If a library is missing, won't load, or lacks a function (an FFmpeg of another version), load() says which, and nothing is called.
//
// Include this AFTER FFmpeg's headers and before any use of its functions. To add a function the game starts to use: its name in
// ARCADE_FFMPEG_FUNCTIONS and one #define at the bottom. Forgetting either fails to LINK ("undefined reference"), never at run time.
#if defined(ARCADE_FFMPEG_LAZY) && !defined(_WIN32)
#include <string>

#define ARCADE_FFMPEG_FUNCTIONS(X) \
    X(av_buffer_ref) X(av_channel_layout_compare) X(av_channel_layout_copy) X(av_channel_layout_uninit) X(av_codec_is_decoder) \
    X(av_codec_iterate) X(av_find_best_stream) X(av_frame_alloc) X(av_frame_copy_props) X(av_frame_free) X(av_frame_unref) \
    X(av_get_pix_fmt_name) X(av_hwdevice_ctx_create) X(av_hwdevice_get_type_name) X(av_hwframe_transfer_data) \
    X(av_index_search_timestamp) X(av_log_set_callback) X(av_packet_alloc) X(av_packet_free) X(av_packet_unref) X(av_read_frame) \
    X(av_seek_frame) X(av_strerror) X(av_version_info) \
    X(avcodec_alloc_context3) X(avcodec_default_get_format) X(avcodec_find_decoder) X(avcodec_flush_buffers) \
    X(avcodec_free_context) X(avcodec_get_hw_config) X(avcodec_get_name) X(avcodec_open2) X(avcodec_parameters_to_context) \
    X(avcodec_receive_frame) X(avcodec_send_packet) \
    X(avformat_close_input) X(avformat_find_stream_info) X(avformat_index_get_entry) X(avformat_license) X(avformat_open_input) \
    X(swr_alloc_set_opts2) X(swr_convert) X(swr_free) X(swr_get_out_samples) X(swr_init) \
    X(sws_freeContext) X(sws_getCoefficients) X(sws_getContext) X(sws_scale) X(sws_setColorspaceDetails)

namespace arcade_ffmpeg_lazy {
// One pointer per function, typed from FFmpeg's own declaration, so a call is checked exactly as before.
struct Table {
#define ARCADE_FFMPEG_MEMBER(name) decltype(&::name) name = nullptr;
    ARCADE_FFMPEG_FUNCTIONS(ARCADE_FFMPEG_MEMBER)
#undef ARCADE_FFMPEG_MEMBER
};
extern Table table;
// Opens the five libraries (beside the game in lib/, else wherever the system finds them) and fills the table. false: `why` says what is missing.
bool load(std::string& why);
}

// The calls in arcade_media_decode.cpp keep their names; each now reads its pointer from the table.
// (arcade_ffmpeg_lazy.cpp, which looks the names up, defines ARCADE_FFMPEG_NO_REDIRECT to see the real ones.)
#ifndef ARCADE_FFMPEG_NO_REDIRECT
#define av_buffer_ref arcade_ffmpeg_lazy::table.av_buffer_ref
#define av_channel_layout_compare arcade_ffmpeg_lazy::table.av_channel_layout_compare
#define av_channel_layout_copy arcade_ffmpeg_lazy::table.av_channel_layout_copy
#define av_channel_layout_uninit arcade_ffmpeg_lazy::table.av_channel_layout_uninit
#define av_codec_is_decoder arcade_ffmpeg_lazy::table.av_codec_is_decoder
#define av_codec_iterate arcade_ffmpeg_lazy::table.av_codec_iterate
#define av_find_best_stream arcade_ffmpeg_lazy::table.av_find_best_stream
#define av_frame_alloc arcade_ffmpeg_lazy::table.av_frame_alloc
#define av_frame_copy_props arcade_ffmpeg_lazy::table.av_frame_copy_props
#define av_frame_free arcade_ffmpeg_lazy::table.av_frame_free
#define av_frame_unref arcade_ffmpeg_lazy::table.av_frame_unref
#define av_get_pix_fmt_name arcade_ffmpeg_lazy::table.av_get_pix_fmt_name
#define av_hwdevice_ctx_create arcade_ffmpeg_lazy::table.av_hwdevice_ctx_create
#define av_hwdevice_get_type_name arcade_ffmpeg_lazy::table.av_hwdevice_get_type_name
#define av_hwframe_transfer_data arcade_ffmpeg_lazy::table.av_hwframe_transfer_data
#define av_index_search_timestamp arcade_ffmpeg_lazy::table.av_index_search_timestamp
#define av_log_set_callback arcade_ffmpeg_lazy::table.av_log_set_callback
#define av_packet_alloc arcade_ffmpeg_lazy::table.av_packet_alloc
#define av_packet_free arcade_ffmpeg_lazy::table.av_packet_free
#define av_packet_unref arcade_ffmpeg_lazy::table.av_packet_unref
#define av_read_frame arcade_ffmpeg_lazy::table.av_read_frame
#define av_seek_frame arcade_ffmpeg_lazy::table.av_seek_frame
#define av_strerror arcade_ffmpeg_lazy::table.av_strerror
#define av_version_info arcade_ffmpeg_lazy::table.av_version_info
#define avcodec_alloc_context3 arcade_ffmpeg_lazy::table.avcodec_alloc_context3
#define avcodec_default_get_format arcade_ffmpeg_lazy::table.avcodec_default_get_format
#define avcodec_find_decoder arcade_ffmpeg_lazy::table.avcodec_find_decoder
#define avcodec_flush_buffers arcade_ffmpeg_lazy::table.avcodec_flush_buffers
#define avcodec_free_context arcade_ffmpeg_lazy::table.avcodec_free_context
#define avcodec_get_hw_config arcade_ffmpeg_lazy::table.avcodec_get_hw_config
#define avcodec_get_name arcade_ffmpeg_lazy::table.avcodec_get_name
#define avcodec_open2 arcade_ffmpeg_lazy::table.avcodec_open2
#define avcodec_parameters_to_context arcade_ffmpeg_lazy::table.avcodec_parameters_to_context
#define avcodec_receive_frame arcade_ffmpeg_lazy::table.avcodec_receive_frame
#define avcodec_send_packet arcade_ffmpeg_lazy::table.avcodec_send_packet
#define avformat_close_input arcade_ffmpeg_lazy::table.avformat_close_input
#define avformat_find_stream_info arcade_ffmpeg_lazy::table.avformat_find_stream_info
#define avformat_index_get_entry arcade_ffmpeg_lazy::table.avformat_index_get_entry
#define avformat_license arcade_ffmpeg_lazy::table.avformat_license
#define avformat_open_input arcade_ffmpeg_lazy::table.avformat_open_input
#define swr_alloc_set_opts2 arcade_ffmpeg_lazy::table.swr_alloc_set_opts2
#define swr_convert arcade_ffmpeg_lazy::table.swr_convert
#define swr_free arcade_ffmpeg_lazy::table.swr_free
#define swr_get_out_samples arcade_ffmpeg_lazy::table.swr_get_out_samples
#define swr_init arcade_ffmpeg_lazy::table.swr_init
#define sws_freeContext arcade_ffmpeg_lazy::table.sws_freeContext
#define sws_getCoefficients arcade_ffmpeg_lazy::table.sws_getCoefficients
#define sws_getContext arcade_ffmpeg_lazy::table.sws_getContext
#define sws_scale arcade_ffmpeg_lazy::table.sws_scale
#define sws_setColorspaceDetails arcade_ffmpeg_lazy::table.sws_setColorspaceDetails
#endif  // ARCADE_FFMPEG_NO_REDIRECT
#endif  // ARCADE_FFMPEG_LAZY
