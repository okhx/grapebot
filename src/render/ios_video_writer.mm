#include "ios_video_writer.hpp"

#ifdef GEODE_IS_IOS

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>

namespace {
std::string errorText(NSError* error, const char* fallback) {
    if (!error) return fallback;
    const char* text = error.localizedDescription.UTF8String;
    return text ? std::string(text) : std::string(fallback);
}
}

std::filesystem::path iosVisibleVideosDirectory() {
    @autoreleasepool {
        NSArray<NSString*>* directories =
            NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* documents = directories.firstObject;
        if (!documents) return {};
        NSString* videos = [[documents stringByAppendingPathComponent:@"Grape"]
            stringByAppendingPathComponent:@"videos"];
        NSError* error = nil;
        if (![[NSFileManager defaultManager]
                createDirectoryAtPath:videos
          withIntermediateDirectories:YES attributes:nil error:&error]) {
            return {};
        }
        return std::filesystem::path(videos.UTF8String);
    }
}

struct IOSVideoWriter::Impl {
    struct AudioChunk {
        std::vector<float> pcm;
        int64_t pts;
    };

    __strong AVAssetWriter* writer = nil;
    __strong AVAssetWriterInput* input = nil;
    __strong AVAssetWriterInput* audioInput = nil;
    __strong AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    int width = 0;
    int height = 0;
    int fps = 60;
    int sampleRate = 48000;
    int channels = 2;
    int64_t frame = 0;
    int64_t audioFrame = 0;
    std::vector<float> pendingAudio;
    std::deque<AudioChunk> queuedAudio;
    dispatch_queue_t queue = nullptr;
    std::atomic_int pendingVideoFrames = 0;
    std::atomic_uint64_t droppedVideoFrames = 0;
    int maxPendingVideoFrames = 3;
    std::atomic_bool audioDisabled = false;
    std::atomic_bool failed = false;
    std::string asyncError;
    std::filesystem::path outputPath;
    bool finished = false;

    geode::Result<> writeAudio(const std::vector<float>& pcm, int64_t pts);
    geode::Result<> drainReadyAudio();
};

geode::Result<> IOSVideoWriter::Impl::writeAudio(
    const std::vector<float>& pcm, int64_t pts) {
    if (!audioInput || pcm.empty()) return geode::Ok();
    const size_t frames = pcm.size() / static_cast<size_t>(channels);
    if (frames == 0) return geode::Ok();
    const size_t sampleCount = frames * static_cast<size_t>(channels);
    const size_t bytes = sampleCount * sizeof(float);

    AudioStreamBasicDescription format{};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat |
                          kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(float) * channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float) * channels;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;

    CMAudioFormatDescriptionRef description = nullptr;
    if (CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault, &format, 0, nullptr, 0, nullptr, nullptr,
            &description) != noErr) {
        return geode::Err("Unable to describe captured iOS audio");
    }

    CMBlockBufferRef block = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr,
        0, bytes, 0, &block);
    if (status == kCMBlockBufferNoErr) {
        status = CMBlockBufferReplaceDataBytes(
            pcm.data(), block, 0, bytes);
    }
    if (status != kCMBlockBufferNoErr) {
        if (block) CFRelease(block);
        CFRelease(description);
        return geode::Err("Unable to allocate captured iOS audio");
    }

    CMSampleTimingInfo timing{
        .duration = CMTimeMake(1, sampleRate),
        .presentationTimeStamp = CMTimeMake(pts, sampleRate),
        .decodeTimeStamp = kCMTimeInvalid,
    };
    CMSampleBufferRef sample = nullptr;
    status = CMSampleBufferCreateReady(
        kCFAllocatorDefault, block, description,
        static_cast<CMItemCount>(frames), 1, &timing, 0,
        nullptr, &sample);
    CFRelease(block);
    CFRelease(description);
    if (status != noErr || !sample)
        return geode::Err("Unable to create an iOS audio sample");

    const BOOL appended = [audioInput appendSampleBuffer:sample];
    CFRelease(sample);
    if (!appended)
        return geode::Err(errorText(writer.error,
                                    "Unable to append iOS audio"));
    return geode::Ok();
}

geode::Result<> IOSVideoWriter::Impl::drainReadyAudio() {
    if (audioDisabled.load(std::memory_order_acquire)) return geode::Ok();
    while (audioInput && audioInput.readyForMoreMediaData &&
           !queuedAudio.empty()) {
        auto& chunk = queuedAudio.front();
        auto result = writeAudio(chunk.pcm, chunk.pts);
        if (result.isErr()) return result;
        queuedAudio.pop_front();
    }
    return geode::Ok();
}

IOSVideoWriter::IOSVideoWriter() : m_impl(std::make_unique<Impl>()) {}

IOSVideoWriter::~IOSVideoWriter() {
    if (m_impl && m_impl->writer && !m_impl->finished) {
        if (m_impl->queue)
            dispatch_sync(m_impl->queue, ^{});
        [m_impl->input markAsFinished];
        if (m_impl->audioInput) [m_impl->audioInput markAsFinished];
        [m_impl->writer cancelWriting];
    }
}

geode::Result<> IOSVideoWriter::open(const std::filesystem::path& output,
                                     int width, int height, int fps,
                                     uint32_t bitrate, int sampleRate,
                                     int channels, bool includeAudio) {
    if (width <= 0 || height <= 0 || fps <= 0)
        return geode::Err("Invalid iOS render settings");

    @autoreleasepool {
        NSString* path = [NSString
            stringWithUTF8String:output.string().c_str()];
        if (!path) return geode::Err("Invalid iOS render output path");
        NSURL* outputURL = [NSURL fileURLWithPath:path];
        NSError* directoryError = nil;
        if (![[NSFileManager defaultManager]
                createDirectoryAtURL:[outputURL URLByDeletingLastPathComponent]
          withIntermediateDirectories:YES attributes:nil
                               error:&directoryError]) {
            return geode::Err(errorText(
                directoryError, "Unable to create the iOS videos folder"));
        }
        [[NSFileManager defaultManager] removeItemAtURL:outputURL
                                                  error:nullptr];

        NSError* error = nil;
        m_impl->writer = [[AVAssetWriter alloc]
            initWithURL:outputURL fileType:AVFileTypeMPEG4 error:&error];
        if (!m_impl->writer)
            return geode::Err(errorText(error, "Unable to create MP4 writer"));

        const int encoderFps = width >= 3800 ? std::min(fps, 60) : fps;
        NSDictionary* compression = @{
            AVVideoAverageBitRateKey : @(bitrate),
            AVVideoExpectedSourceFrameRateKey : @(encoderFps),
            AVVideoMaxKeyFrameIntervalKey : @(encoderFps),
            AVVideoMaxKeyFrameIntervalDurationKey : @1.0,
            AVVideoAllowFrameReorderingKey : @NO
        };
        NSDictionary* settings = @{
            AVVideoCodecKey : AVVideoCodecTypeH264,
            AVVideoWidthKey : @(width),
            AVVideoHeightKey : @(height),
            AVVideoCompressionPropertiesKey : compression
        };
        m_impl->input = [AVAssetWriterInput
            assetWriterInputWithMediaType:AVMediaTypeVideo
                            outputSettings:settings];
        m_impl->input.expectsMediaDataInRealTime = YES;

        NSDictionary* attributes = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey :
                @(kCVPixelFormatType_32BGRA),
            (__bridge NSString*)kCVPixelBufferWidthKey : @(width),
            (__bridge NSString*)kCVPixelBufferHeightKey : @(height),
            (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{}
        };
        m_impl->adaptor = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:m_impl->input
                                       sourcePixelBufferAttributes:attributes];
        if (![m_impl->writer canAddInput:m_impl->input])
            return geode::Err("iOS encoder rejected the video settings");
        [m_impl->writer addInput:m_impl->input];

        if (includeAudio && sampleRate > 0 && channels > 0) {
            NSDictionary* audioSettings = @{
                AVFormatIDKey : @(kAudioFormatMPEG4AAC),
                AVSampleRateKey : @(sampleRate),
                AVNumberOfChannelsKey : @(channels),
                AVEncoderBitRateKey : @(192000)
            };
            m_impl->audioInput = [AVAssetWriterInput
                assetWriterInputWithMediaType:AVMediaTypeAudio
                                outputSettings:audioSettings];
            m_impl->audioInput.expectsMediaDataInRealTime = YES;
            if (![m_impl->writer canAddInput:m_impl->audioInput])
                return geode::Err("iOS encoder rejected the audio settings");
            [m_impl->writer addInput:m_impl->audioInput];
        } else {
            m_impl->audioInput = nil;
        }
        if (![m_impl->writer startWriting]) {
            return geode::Err(errorText(m_impl->writer.error,
                                        "Unable to start iOS video encoder"));
        }
        [m_impl->writer startSessionAtSourceTime:kCMTimeZero];
        m_impl->width = width;
        m_impl->height = height;
        m_impl->fps = fps;
        m_impl->sampleRate = sampleRate;
        m_impl->channels = channels;
        m_impl->frame = 0;
        m_impl->audioFrame = 0;
        m_impl->pendingAudio.clear();
        m_impl->queuedAudio.clear();
        m_impl->queue = dispatch_queue_create(
            "dev.okhx.grape.ios-video-writer",
            DISPATCH_QUEUE_SERIAL);
        const size_t frameBytes = static_cast<size_t>(width) * height * 4;
        const size_t targetQueueBytes = 64 * 1024 * 1024;
        m_impl->maxPendingVideoFrames = static_cast<int>(std::clamp<size_t>(
            targetQueueBytes / frameBytes, 2, 12));
        m_impl->pendingVideoFrames.store(0, std::memory_order_release);
        m_impl->droppedVideoFrames.store(0, std::memory_order_release);
        m_impl->audioDisabled.store(false, std::memory_order_release);
        m_impl->failed.store(false, std::memory_order_release);
        m_impl->asyncError.clear();
        m_impl->outputPath = output;
        m_impl->finished = false;
    }
    return geode::Ok();
}

geode::Result<bool> IOSVideoWriter::appendAudio(
    const std::vector<float>& pcm) {
    if (!m_impl->audioInput || pcm.empty()) return geode::Ok(true);
    if (m_impl->audioDisabled.load(std::memory_order_acquire))
        return geode::Ok(true);
    if (!m_impl->writer || m_impl->finished)
        return geode::Err("iOS video writer is not active");
    if (m_impl->failed.load(std::memory_order_acquire))
        return geode::Err(m_impl->asyncError.empty()
                              ? "iOS audio encoder failed asynchronously"
                              : m_impl->asyncError);
    m_impl->pendingAudio.insert(m_impl->pendingAudio.end(), pcm.begin(),
                                pcm.end());
    return geode::Ok(true);
}

geode::Result<bool> IOSVideoWriter::appendRGBA(
    const std::vector<uint8_t>& rgba, int repeat) {
    if (!m_impl->writer || m_impl->finished)
        return geode::Err("iOS video writer is not active");
    if (m_impl->failed.load(std::memory_order_acquire))
        return geode::Err(m_impl->asyncError.empty()
                              ? "iOS video encoder failed asynchronously"
                              : m_impl->asyncError);
    const size_t expected = static_cast<size_t>(m_impl->width) *
                            static_cast<size_t>(m_impl->height) * 4;
    if (rgba.size() != expected)
        return geode::Err("Captured frame has an invalid size");
    repeat = std::clamp(repeat, 1, 32);
    const int64_t frameIndex = m_impl->frame;
    m_impl->frame += repeat;
    const int sampleStep = m_impl->width >= 3800
        ? std::max(1, m_impl->fps / 60) : 1;
    const int64_t firstSampleFrame =
        (frameIndex + sampleStep - 1) / sampleStep * sampleStep;
    const int64_t frameEnd = frameIndex + repeat;
    if (firstSampleFrame >= frameEnd) return geode::Ok(true);
    const int sampleCount = static_cast<int>(
        (frameEnd - 1 - firstSampleFrame) / sampleStep + 1);
    if (m_impl->pendingVideoFrames.load(std::memory_order_acquire) >=
        m_impl->maxPendingVideoFrames) {
        return geode::Ok(false);
    }

    const int64_t nextVideoAudioFrame =
        (frameIndex + repeat) * m_impl->sampleRate / m_impl->fps;
    const int64_t allowedAudioFrames =
        std::max<int64_t>(0, nextVideoAudioFrame - m_impl->audioFrame);
    const size_t availableAudioFrames =
        m_impl->pendingAudio.size() /
        static_cast<size_t>(m_impl->channels);
    size_t audioFrames = std::min(
        availableAudioFrames, static_cast<size_t>(allowedAudioFrames));
    audioFrames = audioFrames / 1024 * 1024;
    const size_t audioSamples =
        audioFrames * static_cast<size_t>(m_impl->channels);
    std::vector<float> audio(
        m_impl->pendingAudio.begin(),
        m_impl->pendingAudio.begin() + audioSamples);
    m_impl->pendingAudio.erase(
        m_impl->pendingAudio.begin(),
        m_impl->pendingAudio.begin() + audioSamples);
    const int64_t audioPts = m_impl->audioFrame;
    m_impl->audioFrame += static_cast<int64_t>(audioFrames);

    auto frameData = std::make_shared<std::vector<uint8_t>>(rgba);
    m_impl->pendingVideoFrames.fetch_add(1, std::memory_order_acq_rel);
    auto* impl = m_impl.get();
    dispatch_async(impl->queue, ^{
        @autoreleasepool {
            if (impl->failed.load(std::memory_order_acquire)) {
                impl->pendingVideoFrames.fetch_sub(
                    1, std::memory_order_acq_rel);
                return;
            }

            if (!audio.empty()) {
                impl->queuedAudio.push_back({audio, audioPts});
            }

            CVPixelBufferRef pixel = nullptr;
            const CVReturn created = CVPixelBufferPoolCreatePixelBuffer(
                kCFAllocatorDefault, impl->adaptor.pixelBufferPool, &pixel);
            if (created != kCVReturnSuccess || !pixel) {
                impl->asyncError = "Unable to allocate an iOS video frame";
                impl->failed.store(true, std::memory_order_release);
                impl->pendingVideoFrames.fetch_sub(
                    1, std::memory_order_acq_rel);
                return;
            }

            const CVReturn locked = CVPixelBufferLockBaseAddress(pixel, 0);
            auto* destination = locked == kCVReturnSuccess
                ? static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixel))
                : nullptr;
            if (locked != kCVReturnSuccess || !destination) {
                if (locked == kCVReturnSuccess)
                    CVPixelBufferUnlockBaseAddress(pixel, 0);
                CVPixelBufferRelease(pixel);
                impl->asyncError = "Unable to access the iOS video frame";
                impl->failed.store(true, std::memory_order_release);
                impl->pendingVideoFrames.fetch_sub(
                    1, std::memory_order_acq_rel);
                return;
            }
            const size_t stride = CVPixelBufferGetBytesPerRow(pixel);
            for (int y = 0; y < impl->height; ++y) {
                const uint8_t* source = frameData->data() +
                    static_cast<size_t>(impl->height - 1 - y) *
                    impl->width * 4;
                uint8_t* row = destination + static_cast<size_t>(y) * stride;
                for (int x = 0; x < impl->width; ++x) {
                    row[x * 4 + 0] = source[x * 4 + 2];
                    row[x * 4 + 1] = source[x * 4 + 1];
                    row[x * 4 + 2] = source[x * 4 + 0];
                    row[x * 4 + 3] = 255;
                }
            }
            CVPixelBufferUnlockBaseAddress(pixel, 0);

            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(impl->width >= 3800 ? 50 : 20);
            int appendedFrames = 0;
            while (appendedFrames < sampleCount) {
                bool progressed = false;
                if (impl->input.readyForMoreMediaData) {
                    const CMTime time = CMTimeMake(
                        firstSampleFrame + appendedFrames * sampleStep,
                        impl->fps);
                    const BOOL appended = [impl->adaptor
                        appendPixelBuffer:pixel
                        withPresentationTime:time];
                    if (!appended) {
                        impl->asyncError = errorText(
                            impl->writer.error,
                            "Unable to append iOS video frame");
                        impl->failed.store(true, std::memory_order_release);
                        break;
                    }
                    ++appendedFrames;
                    progressed = true;
                }
                const auto status = impl->writer.status;
                if (status == AVAssetWriterStatusFailed ||
                    status == AVAssetWriterStatusCancelled) {
                    impl->asyncError = errorText(
                        impl->writer.error,
                        "iOS video encoder stopped unexpectedly");
                    impl->failed.store(true, std::memory_order_release);
                    break;
                }
                if (!progressed) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        const auto droppedNow = static_cast<uint64_t>(
                            sampleCount - appendedFrames);
                        const auto dropped =
                            impl->droppedVideoFrames.fetch_add(
                                droppedNow, std::memory_order_acq_rel) +
                            droppedNow;
                        if (dropped == droppedNow || dropped % 240 < droppedNow) {
                            NSLog(@"[Grape] iOS encoder overloaded; dropped %llu video frames",
                                  static_cast<unsigned long long>(dropped));
                        }
                        break;
                    }
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                }
            }
            if (!impl->failed.load(std::memory_order_acquire)) {
                auto audioResult = impl->drainReadyAudio();
                if (audioResult.isErr()) {
                    const auto error = audioResult.unwrapErr();
                    NSLog(@"[Grape] iOS render audio disabled: %s",
                          error.c_str());
                    impl->queuedAudio.clear();
                    impl->audioDisabled.store(
                        true, std::memory_order_release);
                }
            }
            CVPixelBufferRelease(pixel);
            impl->pendingVideoFrames.fetch_sub(
                1, std::memory_order_acq_rel);
        }
    });
    return geode::Ok(true);
}

geode::Result<> IOSVideoWriter::finish() {
    if (!m_impl->writer || m_impl->finished) return geode::Ok();
    const int64_t videoAudioFrames =
        m_impl->frame * m_impl->sampleRate / m_impl->fps;
    const int64_t allowedAudioFrames =
        std::max<int64_t>(0, videoAudioFrames - m_impl->audioFrame);
    const size_t availableAudioFrames =
        m_impl->pendingAudio.size() /
        static_cast<size_t>(m_impl->channels);
    const size_t audioFrames = std::min(
        availableAudioFrames, static_cast<size_t>(allowedAudioFrames));
    const size_t audioSamples =
        audioFrames * static_cast<size_t>(m_impl->channels);
    std::vector<float> finalAudio(
        m_impl->pendingAudio.begin(),
        m_impl->pendingAudio.begin() + audioSamples);
    const int64_t finalAudioPts = m_impl->audioFrame;
    m_impl->audioFrame += static_cast<int64_t>(audioFrames);
    auto* impl = m_impl.get();
    if (impl->audioInput &&
        !impl->audioDisabled.load(std::memory_order_acquire) &&
        !impl->failed.load(std::memory_order_acquire)) {
        dispatch_async(impl->queue, ^{
            @autoreleasepool {
                if (!finalAudio.empty()) {
                    impl->queuedAudio.push_back(
                        {finalAudio, finalAudioPts});
                }
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(60);
                while (!impl->queuedAudio.empty()) {
                    if (impl->writer.status == AVAssetWriterStatusFailed) {
                        impl->asyncError = errorText(
                            impl->writer.error,
                            "iOS writer failed while finalizing audio");
                        impl->failed.store(true, std::memory_order_release);
                        return;
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        NSLog(@"[Grape] Dropping delayed iOS render audio to save video");
                        impl->queuedAudio.clear();
                        impl->audioDisabled.store(
                            true, std::memory_order_release);
                        return;
                    }
                    auto result = impl->drainReadyAudio();
                    if (result.isErr()) {
                        const auto error = result.unwrapErr();
                        NSLog(@"[Grape] Dropping iOS render audio: %s",
                              error.c_str());
                        impl->queuedAudio.clear();
                        impl->audioDisabled.store(
                            true, std::memory_order_release);
                        return;
                    }
                    if (impl->queuedAudio.empty()) break;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                }
            }
        });
    }
    dispatch_sync(impl->queue, ^{});
    m_impl->pendingAudio.clear();

    if (m_impl->failed.load(std::memory_order_acquire)) {
        m_impl->finished = true;
        [m_impl->input markAsFinished];
        if (m_impl->audioInput) [m_impl->audioInput markAsFinished];
        [m_impl->writer cancelWriting];
        return geode::Err(m_impl->asyncError.empty()
                              ? "iOS video encoder failed"
                              : m_impl->asyncError);
    }

    m_impl->finished = true;
    CMTime endTime = CMTimeMake(m_impl->frame, m_impl->fps);
    if (m_impl->audioFrame > 0) {
        const CMTime audioEnd =
            CMTimeMake(m_impl->audioFrame, m_impl->sampleRate);
        if (CMTimeCompare(audioEnd, endTime) > 0) endTime = audioEnd;
    }
    [m_impl->writer endSessionAtSourceTime:endTime];
    [m_impl->input markAsFinished];
    if (m_impl->audioInput) [m_impl->audioInput markAsFinished];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const BOOL completed = [m_impl->writer finishWriting];
#pragma clang diagnostic pop
    if (!completed) {
        return geode::Err(errorText(m_impl->writer.error,
                                    "Failed to finish the iOS video"));
    }
    if (m_impl->writer.status != AVAssetWriterStatusCompleted) {
        return geode::Err(errorText(m_impl->writer.error,
                                    "Failed to finalize the iOS video"));
    }
    std::error_code fileError;
    const auto fileSize = std::filesystem::file_size(
        m_impl->outputPath, fileError);
    if (fileError || fileSize == 0) {
        return geode::Err("iOS encoder completed without saving an MP4 file");
    }
    return geode::Ok();
}

#endif
