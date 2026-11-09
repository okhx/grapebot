#ifndef FFMPEG_HPP
#define FFMPEG_HPP

#pragma once
#include "util/storage.hpp"

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#endif
