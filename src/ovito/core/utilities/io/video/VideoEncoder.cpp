////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/core/Core.h>
#include <ovito/core/dataset/animation/TimeInterval.h>
#include "VideoEncoder.h"

extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
};

namespace Ovito {

/// The list of supported video formats.
QList<VideoEncoder::Format> VideoEncoder::_supportedFormats;

/******************************************************************************
* Constructor
******************************************************************************/
VideoEncoder::VideoEncoder(QObject* parent) : QObject(parent)
{
    initCodecs();

#ifndef OVITO_DEBUG
    ::av_log_set_level(AV_LOG_QUIET); // Set the FFMPEG logging level to quiet to avoid too verbose terminal output.
#endif
}

/******************************************************************************
* Initializes libavcodec, and register all codecs and formats.
******************************************************************************/
void VideoEncoder::initCodecs()
{
#if LIBAVFORMAT_VERSION_MAJOR < 58
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        ::avcodec_register_all();
    });
#endif
}

/******************************************************************************
* Returns the error string for the given error code.
******************************************************************************/
QString VideoEncoder::errorMessage(int errorCode)
{
    char errbuf[512];
    if(::av_strerror(errorCode, errbuf, sizeof(errbuf)) < 0) {
        return QString("Unknown FFMPEG error.");
    }
    return QString::fromLocal8Bit(errbuf);
}

/******************************************************************************
* Returns the list of supported output formats.
******************************************************************************/
QList<VideoEncoder::Format> VideoEncoder::supportedFormats()
{
    if(!_supportedFormats.empty())
        return _supportedFormats;

    initCodecs();

#if LIBAVFORMAT_VERSION_MAJOR < 58
    AVOutputFormat* fmt = nullptr;
    while((fmt = ::av_oformat_next(fmt))) {
#else
    void* opaque = nullptr;
    const AVOutputFormat* fmt;
    while((fmt = ::av_muxer_iterate(&opaque))) {
#endif
        if(fmt->flags & AVFMT_NOFILE || fmt->flags & AVFMT_NEEDNUMBER)
            continue;

        if(qstrcmp(fmt->name, "mov") != 0 && qstrcmp(fmt->name, "mp4") != 0 && qstrcmp(fmt->name, "avi") != 0 && qstrcmp(fmt->name, "gif") != 0)
            continue;

        // Skip formats whose default video codec is not part of this ffmpeg build (e.g. H.264 in an LGPL-only build).
        if(fmt->video_codec == AV_CODEC_ID_NONE || !::avcodec_find_encoder(fmt->video_codec))
            continue;

        Format format;
        format.name = fmt->name;
        format.longName = QString::fromLocal8Bit(fmt->long_name);
        format.extensions = QString::fromLocal8Bit(fmt->extensions).split(',');
        format.avformat = fmt;

        _supportedFormats.push_back(format);
    }

    return _supportedFormats;
}

/******************************************************************************
* Opens a video file for writing.
******************************************************************************/
void VideoEncoder::openFile(const QString& filename, int width, int height, float framesPerSecond, VideoEncoder::Format* format)
{
    int errCode;

    // Make sure previous file is closed.
    closeFile();

    // For unknown reasons, MPEG4 and MOV videos with frame rates 2,4,8 and 16 turn out
    // invalid and don't play in QuickTime Player on macOS. Thus, we should avoid producing videos with these FPS values.
    // As a workaround, we instead resort to one of the valid playback rates being is an integer multiple of the selected frame rate.
    // We then have to output N identicial copies of each rendered frame to mimic the desired frame rate.

    if(framesPerSecond == 2.0f) _frameDuplication = 5; // Change 2 fps to 10 fps.
    else if(framesPerSecond == 4.0f) _frameDuplication = 3; // Change 4 fps to 12 fps.
    else if(framesPerSecond == 8.0f) _frameDuplication = 3; // Change 8 fps to 24 fps.
    else if(framesPerSecond == 16.0f) _frameDuplication = 3; // Change 16 fps to 48 fps.
    else _frameDuplication = 1;
    framesPerSecond *= _frameDuplication;

    // TODO: Support more fps values <1.0. Right now we are limited to 0.1, 0.2, and 0.5 fps.
    int fpsNum = (framesPerSecond < 1.0f) ? 10 : 1;
    int fpsDen = std::max(1, (int)std::round(framesPerSecond * fpsNum));
    OVITO_ASSERT(std::abs(fpsDen - framesPerSecond * fpsNum) < 1e-6f);

    // 8-bit encoded filename.
    // Note: FFmpeg always uses UTF-8 encoding - even on Windows platform.
    QByteArray encodedFilename = filename.toUtf8();

    const AVOutputFormat* outputFormat;
    if(format == nullptr) {
        // Auto detect the output format from the file name.
        outputFormat = ::av_guess_format(nullptr, encodedFilename.constData(), nullptr);
        if(!outputFormat)
            throw Exception(tr("Could not deduce video output format from file extension."));
    }
    else outputFormat = format->avformat;

    // Odd image widths lead to artifacts when writing animated GIFs.
    // Round to nearest integer in case.
    if(outputFormat->video_codec == AV_CODEC_ID_GIF && width > 1) {
        width &= ~1;
    }

#if LIBAVFORMAT_VERSION_MAJOR >= 58
    // Allocate the output media context.
    AVFormatContext* formatContext = nullptr;
    if((errCode = ::avformat_alloc_output_context2(&formatContext, const_cast<AVOutputFormat*>(outputFormat), nullptr, encodedFilename.constData())) < 0 || !formatContext)
        throw Exception(tr("Failed to create video format context: %1").arg(errorMessage(errCode)));
    _formatContext.reset(formatContext, &av_free);
#else
    // Allocate the output media context.
    _formatContext.reset(::avformat_alloc_context(), &av_free);
    if(!_formatContext)
        throw Exception(tr("Failed to allocate output media context."));

    _formatContext->oformat = const_cast<AVOutputFormat*>(outputFormat);
    qstrncpy(_formatContext->filename, encodedFilename.constData(), sizeof(_formatContext->filename) - 1);
#endif

    if(outputFormat->video_codec == AV_CODEC_ID_NONE)
        throw Exception(tr("No video codec available."));

    // Find the video encoder.
    _codec = ::avcodec_find_encoder(outputFormat->video_codec);
    if(!_codec)
        throw Exception(tr("Video codec not found."));

    // Add the video stream using the default format codec and initialize the codec.
    _videoStream = ::avformat_new_stream(_formatContext.get(), _codec);
    if(!_videoStream)
        throw Exception(tr("Failed to create video stream."));
    _videoStream->id = 0;

    // Create the codec context.
#if LIBAVCODEC_VERSION_MAJOR >= 57
    _codecContext.reset(::avcodec_alloc_context3(_codec), [](AVCodecContext* ctxt) { ::avcodec_free_context(&ctxt); });
    if(!_codecContext)
        throw Exception(tr("Failed to allocate a video encoding context."));
#else
    _codecContext.reset(_videoStream->codec, [](AVCodecContext*) {});
#endif

    _codecContext->codec_id = outputFormat->video_codec;
    _codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    _codecContext->qmin = 3;
    _codecContext->qmax = 3;
    _codecContext->bit_rate = 0;
    _codecContext->width = width;
    _codecContext->height = height;
    _codecContext->time_base.num = _videoStream->time_base.num = fpsNum;
    _codecContext->time_base.den = _videoStream->time_base.den = fpsDen;
    _codecContext->gop_size = 12;   // Emit one intra frame every twelve frames at most.
    _codecContext->framerate = av_inv_q(_codecContext->time_base);
    _videoStream->avg_frame_rate = av_inv_q(_codecContext->time_base);

    // Be sure to use the correct pixel format (e.g. RGB, YUV).
    if(outputFormat->video_codec == AV_CODEC_ID_GIF)
        _codecContext->pix_fmt = AV_PIX_FMT_PAL8;
    else {
        _codecContext->pix_fmt = AV_PIX_FMT_YUV422P;
#if LIBAVCODEC_VERSION_MAJOR >= 62
        // FFmpeg 8 removed AVCodec::pix_fmts.
        const void* configs = nullptr;
        int numConfigs = 0;
        if(::avcodec_get_supported_config(nullptr, _codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &numConfigs) >= 0 && configs && numConfigs > 0)
            _codecContext->pix_fmt = static_cast<const AVPixelFormat*>(configs)[0];
#else
        if(_codec->pix_fmts)
            _codecContext->pix_fmt = _codec->pix_fmts[0];
#endif
    }

    // Some formats want stream headers to be separate.
    if(_formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
#ifdef AV_CODEC_FLAG_GLOBAL_HEADER
        _codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
#else
        _codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
#endif
    }

    // Open the codec.
    if((errCode = ::avcodec_open2(_codecContext.get(), _codec, nullptr)) < 0)
        throw Exception(tr("Could not open video codec: %1").arg(errorMessage(errCode)));

#if LIBAVCODEC_VERSION_MAJOR >= 57
    // Copy the stream parameters to the muxer.
    if((errCode = ::avcodec_parameters_from_context(_videoStream->codecpar, _codecContext.get())) < 0)
        throw Exception(tr("Could not copy the video stream parameters: %1").arg(errorMessage(errCode)));
#endif

    // Allocate and init a video frame data structure.
#if LIBAVCODEC_VERSION_MAJOR >= 56
    _frame.reset(::av_frame_alloc(), [](AVFrame* frame) { ::av_frame_free(&frame); });
#else
    _frame.reset(::avcodec_alloc_frame(), &::av_free);
#endif
    if(!_frame)
        throw Exception(tr("Could not allocate video frame buffer."));

#if LIBAVCODEC_VERSION_MAJOR >= 57
    _frame->format = (outputFormat->video_codec == AV_CODEC_ID_GIF) ? AV_PIX_FMT_BGRA : _codecContext->pix_fmt;
    _frame->width  = _codecContext->width;
    _frame->height = _codecContext->height;

    // Allocate the buffers for the frame data.
    if((errCode = ::av_frame_get_buffer(_frame.get(), 32)) < 0)
        throw Exception(tr("Could not allocate video frame encoding buffer: %1").arg(errorMessage(errCode)));
#else
    // Allocate memory.
    int size = ::avpicture_get_size(_codecContext->pix_fmt, _codecContext->width, _codecContext->height);
    _pictureBuf = std::make_unique<quint8[]>(size);

    // Set up the planes.
    ::avpicture_fill(reinterpret_cast<AVPicture*>(_frame.get()), _pictureBuf.get(), _codecContext->pix_fmt, _codecContext->width, _codecContext->height);

    // Allocate memory for encoded frame.
    _outputBuf.resize(width * height * 3);
#endif

    // Open output file (if needed).
    if(!(outputFormat->flags & AVFMT_NOFILE)) {
        if((errCode = ::avio_open(&_formatContext->pb, encodedFilename.constData(), AVIO_FLAG_WRITE)) < 0)
            throw Exception(tr("Failed to open output video file '%1': %2").arg(filename).arg(errorMessage(errCode)));
    }

    // Write stream header, if any.
    if((errCode = ::avformat_write_header(_formatContext.get(), nullptr)) < 0)
        throw Exception(tr("Failed to write video file header: %1").arg(errorMessage(errCode)));

    ::av_dump_format(_formatContext.get(), 0, encodedFilename.constData(), 1);

    if(outputFormat->video_codec == AV_CODEC_ID_GIF) {

        const AVFilter* buffersrc = ::avfilter_get_by_name("buffer");
        const AVFilter* buffersink = ::avfilter_get_by_name("buffersink");

        AVRational time_base = { fpsNum, fpsDen };
        AVRational aspect_pixel = { 1, 1 };

        AVFilterInOut* inputs = ::avfilter_inout_alloc();
        AVFilterInOut* outputs = ::avfilter_inout_alloc();
        OVITO_ASSERT(inputs && outputs);

        _filterGraph.reset(::avfilter_graph_alloc(), [](AVFilterGraph* graph) { ::avfilter_graph_free(&graph); });
        OVITO_ASSERT(_filterGraph);

        char args[512];
        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                width, height,
                AV_PIX_FMT_BGRA,
                time_base.num, time_base.den,
                aspect_pixel.num, aspect_pixel.den);

        if((errCode = ::avfilter_graph_create_filter(&_bufferSourceCtx, buffersrc, "in", args, nullptr, _filterGraph.get())) < 0)
            throw Exception(tr("Failed to create the 'source buffer' for animated GIF encoding: %1").arg(errorMessage(errCode)));

        // GIF has possible output of PAL8.
#if LIBAVFILTER_VERSION_MAJOR >= 11
        // FFmpeg 8 removed the "pix_fmts" option of the buffer sink in favor of "pixel_formats", which
        // can only be set before the filter is initialized, so allocate, configure and then initialize it.
        _bufferSinkCtx = ::avfilter_graph_alloc_filter(_filterGraph.get(), buffersink, "out");
        if(!_bufferSinkCtx)
            throw Exception(tr("Failed to create the 'sink buffer' for animated GIF encoding."));
        if((errCode = av_opt_set(_bufferSinkCtx, "pixel_formats", "pal8", AV_OPT_SEARCH_CHILDREN)) < 0)
            throw Exception(tr("Failed to set the output pixel format for animated GIF encoding: %1").arg(errorMessage(errCode)));
        if((errCode = ::avfilter_init_str(_bufferSinkCtx, nullptr)) < 0)
            throw Exception(tr("Failed to create the 'sink buffer' for animated GIF encoding: %1").arg(errorMessage(errCode)));
#else
        if((errCode = ::avfilter_graph_create_filter(&_bufferSinkCtx, buffersink, "out", nullptr, nullptr, _filterGraph.get())) < 0)
            throw Exception(tr("Failed to create the 'sink buffer' for animated GIF encoding: %1").arg(errorMessage(errCode)));

        const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_PAL8 };
        errCode = av_opt_set_bin(_bufferSinkCtx, "pix_fmts", reinterpret_cast<const uint8_t*>(pix_fmts), sizeof(pix_fmts), AV_OPT_SEARCH_CHILDREN);
        if(errCode < 0)
            throw Exception(tr("Failed to set the output pixel format for animated GIF encoding: %1").arg(errorMessage(errCode)));
#endif

        outputs->name = av_strdup("in");
        outputs->filter_ctx = _bufferSourceCtx;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = _bufferSinkCtx;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        // bgra preserves alpha channel during palette generation.
        static constexpr char filter_desc[] = "format=pix_fmts=bgra,split [a][b];[a]palettegen[p];[b][p]paletteuse";

        // GIF has possible output of PAL8.
        if((errCode = ::avfilter_graph_parse_ptr(_filterGraph.get(), filter_desc, &inputs, &outputs, nullptr)) < 0)
            throw Exception(tr("Failed to parse the filter graph (bad string) for animated GIF encoding: %1").arg(errorMessage(errCode)));

        if((errCode = ::avfilter_graph_config(_filterGraph.get(), nullptr)) < 0)
            throw Exception(tr("Failed to configure the filter graph (bad string) for animated GIF encoding: %1").arg(errorMessage(errCode)));

        ::avfilter_inout_free(&inputs);
        ::avfilter_inout_free(&outputs);
    }

    // Success.
    _isOpen = true;
    _numFrames = 0;
}

/******************************************************************************
* This closes the written video file.
******************************************************************************/
void VideoEncoder::closeFile()
{
    if(!_formatContext) {
        OVITO_ASSERT(!_isOpen);
        return;
    }

    // Write stream trailer.
    if(_isOpen) {
        int errCode;

        if(_codecContext->codec_id == AV_CODEC_ID_GIF) {
            // End of buffer.
            if((errCode = ::av_buffersrc_add_frame_flags(_bufferSourceCtx, nullptr, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0)
                throw Exception(tr("Failed to add final GIF frame to global buffer: %1").arg(errorMessage(errCode)));

            do {
                AVFrame* filter_frame = ::av_frame_alloc();
                errCode = ::av_buffersink_get_frame(_bufferSinkCtx, filter_frame);
                if(errCode == AVERROR(EAGAIN) || errCode == AVERROR_EOF) {
                    ::av_frame_unref(filter_frame);
                    break;
                }

                // Write the filter frame to output file
                int ret = ::avcodec_send_frame(_codecContext.get(), filter_frame);
                AVPacket* pkt = ::av_packet_alloc();
#if LIBAVCODEC_VERSION_MAJOR < 58
                ::av_init_packet(pkt);
#endif
                while(ret >= 0) {
                    ret = ::avcodec_receive_packet(_codecContext.get(), pkt);
                    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    ::av_write_frame(_formatContext.get(), pkt);
                }
                ::av_packet_unref(pkt);
                ::av_frame_unref(filter_frame);
            }
            while(errCode >= 0);
        }
        else {
#if LIBAVCODEC_VERSION_MAJOR >= 57
            // Flush encoder.
            if((errCode = ::avcodec_send_frame(_codecContext.get(), nullptr)) < 0)
                qWarning() << "Error while submitting an image frame for video encoding:" << errorMessage(errCode);

            AVPacket* pkt = ::av_packet_alloc();
#if LIBAVCODEC_VERSION_MAJOR < 58
            ::av_init_packet(pkt);
#endif
            do {
                errCode = ::avcodec_receive_packet(_codecContext.get(), pkt);
                if(errCode < 0 && errCode != AVERROR(EAGAIN) && errCode != AVERROR_EOF) {
                    qWarning() << "Error while encoding video frame:" << errorMessage(errCode);
                    break;
                }

                if(errCode >= 0) {
                    ::av_packet_rescale_ts(pkt, _codecContext->time_base, _videoStream->time_base);
                    pkt->stream_index = _videoStream->index;
                    // Write the compressed frame to the media file.
                    if((errCode = ::av_interleaved_write_frame(_formatContext.get(), pkt)) < 0) {
                        qWarning() << "Error while writing encoded video frame:" << errorMessage(errCode);
                    }
                }
            }
            while(errCode >= 0);
            ::av_packet_unref(pkt);
#endif
        }
        if(::av_codec_is_encoder(_codecContext->codec)) {
#if LIBAVCODEC_VERSION_MAJOR >= 59
            if(_codecContext->codec->capabilities & AV_CODEC_CAP_ENCODER_FLUSH)
#endif
                ::avcodec_flush_buffers(_codecContext.get());
        }
        ::av_write_trailer(_formatContext.get());
    }

#if LIBAVCODEC_VERSION_MAJOR < 61 // ffmpeg>=7: avcodec_close() deprecated - Use avcodec_free_context() to destroy a codec context (either open or closed).
    // Close codec.
    if(_codecContext)
        ::avcodec_close(_codecContext.get());
#endif
    _codecContext.reset();

#if LIBAVCODEC_VERSION_MAJOR < 57
    // Free streams.
    if(_formatContext) {
        for(size_t i = 0; i < _formatContext->nb_streams; i++) {
            ::av_freep(&_formatContext->streams[i]->codec);
            ::av_freep(&_formatContext->streams[i]);
        }
    }
#endif

    // Close the output file.
    if(_formatContext->pb)
        ::avio_close(_formatContext->pb);

    // Cleanup.
    if(_bufferSourceCtx) ::avfilter_free(_bufferSourceCtx);
    if(_bufferSinkCtx) ::avfilter_free(_bufferSinkCtx);
    _bufferSourceCtx = nullptr;
    _bufferSinkCtx = nullptr;
    _filterGraph.reset();
    _pictureBuf.reset();
    _frame.reset();
    if(_imgConvertCtx)
        ::sws_freeContext(_imgConvertCtx);
    _imgConvertCtx = nullptr;
    _videoStream = nullptr;
    _outputBuf.clear();
    _formatContext.reset();
    _isOpen = false;
}

/******************************************************************************
* Writes a single frame into the video file.
******************************************************************************/
void VideoEncoder::writeFrame(const QImage& image)
{
    OVITO_ASSERT(_isOpen);
    if(!_isOpen)
        return;

    int videoWidth = _codecContext->width;
    int videoHeight = _codecContext->height;

    // Make sure bit format of image is correct.
    // ARGB (0xAARRGGBB) is used for gifs to allow for a transparent background. Based on testing ffmpeg seems ot expect non-premultiplied
    // alpha. Other video formats get a default black background (RGB32 -> 0xffRRGGBB)
    QImage finalImage = (_codecContext->codec_id == AV_CODEC_ID_GIF) ? image.convertToFormat(QImage::Format_ARGB32)
                                                                     : image.convertToFormat(QImage::Format_RGB32);

    for(int frameCopy = 0; frameCopy < _frameDuplication; frameCopy++) {

        // Make sure the frame data is writable.
        int errCode;
        if((errCode = ::av_frame_make_writable(_frame.get())) < 0)
            throw Exception(tr("Ffmpeg error: Making video frame buffer writable failed: %1").arg(errorMessage(errCode)));
        _frame->pts = _numFrames++;

        // Convert image to codec pixel format.
        uint8_t *srcplanes[3];
        srcplanes[0] = (uint8_t*)finalImage.bits();
        srcplanes[1] = nullptr;
        srcplanes[2] = nullptr;

        int srcstride[3];
        srcstride[0] = finalImage.bytesPerLine();
        srcstride[1] = 0;
        srcstride[2] = 0;

        if(_codecContext->codec_id == AV_CODEC_ID_GIF) {

            if(_formatContext->nb_streams > 0)
                _frame->pts *= av_rescale_q(1, _codecContext->time_base, _formatContext->streams[0]->time_base);
            else
                _frame->pts *= av_rescale_q(1, _codecContext->time_base, { 1, 100 });

            // Create conversion context.
            _imgConvertCtx = ::sws_getCachedContext(_imgConvertCtx, image.width(), image.height(), AV_PIX_FMT_BGRA,
                    videoWidth, videoHeight, (AVPixelFormat)_frame->format, SWS_BICUBIC, nullptr, nullptr, nullptr);
            if(!_imgConvertCtx)
                throw Exception(tr("Cannot initialize SWS conversion context to convert video frame."));

            ::sws_scale(_imgConvertCtx, srcplanes, srcstride, 0, image.height(), _frame->data, _frame->linesize);

            // "palettegen" filter needs a whole stream, just add frame to buffer.
            if((errCode = ::av_buffersrc_add_frame_flags(_bufferSourceCtx, _frame.get(), AV_BUFFERSRC_FLAG_KEEP_REF)) < 0)
                throw Exception(tr("Ffmpeg error: Failed to add GIF frame to animation in-memory buffer: %1").arg(errorMessage(errCode)));
        }
        else {
            // Create conversion context.
            _imgConvertCtx = ::sws_getCachedContext(_imgConvertCtx, image.width(), image.height(), AV_PIX_FMT_BGRA,
                    videoWidth, videoHeight, _codecContext->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
            if(!_imgConvertCtx)
                throw Exception(tr("Cannot initialize SWS conversion context to convert video frame."));

            ::sws_scale(_imgConvertCtx, srcplanes, srcstride, 0, image.height(), _frame->data, _frame->linesize);

#if LIBAVCODEC_VERSION_MAJOR >= 57

            if((errCode = ::avcodec_send_frame(_codecContext.get(), _frame.get())) < 0)
                throw Exception(tr("Error while submitting an image frame for video encoding: %1").arg(errorMessage(errCode)));

            AVPacket* pkt = ::av_packet_alloc();
#if LIBAVCODEC_VERSION_MAJOR < 58
            ::av_init_packet(pkt);
#endif
            do {
                errCode = ::avcodec_receive_packet(_codecContext.get(), pkt);
                if(errCode < 0 && errCode != AVERROR(EAGAIN) && errCode != AVERROR_EOF)
                    throw Exception(tr("Error while encoding video frame: %1").arg(errorMessage(errCode)));

                if(errCode >= 0) {
                    ::av_packet_rescale_ts(pkt, _codecContext->time_base, _videoStream->time_base);
                    pkt->stream_index = _videoStream->index;
                    // Write the compressed frame to the media file.
                    if((errCode = ::av_interleaved_write_frame(_formatContext.get(), pkt)) < 0) {
                        throw Exception(tr("Error while writing encoded video frame: %1").arg(errorMessage(errCode)));
                    }
                }
            }
            while(errCode >= 0);
            ::av_packet_unref(pkt);

#elif !defined(FF_API_OLD_ENCODE_VIDEO) && LIBAVCODEC_VERSION_MAJOR < 55
            int out_size = ::avcodec_encode_video(_codecContext.get(), _outputBuf.data(), _outputBuf.size(), _frame.get());
            // If zero size, it means the image was buffered.
            if(out_size > 0) {
                AVPacket pkt;
                ::av_init_packet(&pkt);
                if(_codecContext->coded_frame->pts != AV_NOPTS_VALUE)
                    pkt.pts = ::av_rescale_q(_codecContext->coded_frame->pts, _codecContext->time_base, _videoStream->time_base);
                if(_codecContext->coded_frame->key_frame)
                    pkt.flags |= AV_PKT_FLAG_KEY;

                pkt.stream_index = _videoStream->index;
                pkt.data = _outputBuf.data();
                pkt.size = out_size;
                if(::av_interleaved_write_frame(_formatContext.get(), &pkt) < 0) {
                    ::av_free_packet(&pkt);
                    throw Exception(tr("Error while writing video frame."));
                }
                ::av_free_packet(&pkt);
            }
#else
            AVPacket pkt = {0};
            ::av_init_packet(&pkt);

            int got_packet_ptr;
            if((errCode = ::avcodec_encode_video2(_codecContext.get(), &pkt, _frame.get(), &got_packet_ptr)) < 0)
                throw Exception(tr("Error while encoding video frame: %1").arg(errorMessage(errCode)));

            if(got_packet_ptr && pkt.size) {
                pkt.stream_index = _videoStream->index;
                if((errCode = ::av_write_frame(_formatContext.get(), &pkt)) < 0) {
                    ::av_free_packet(&pkt);
                    throw Exception(tr("Error while writing video frame: %1").arg(errorMessage(errCode)));
                }
                ::av_free_packet(&pkt);
            }
#endif
        }
    }
}

}   // End of namespace
