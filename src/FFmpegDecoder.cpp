// Copyright � 2014 Mikko Ronkainen <firstname@mikkoronkainen.com>
// License: GPLv3, see the LICENSE file.

#include <windows.h>

#include <QtGlobal>

extern "C"
{
#define __STDC_CONSTANT_MACROS
#include <libavutil/imgutils.h>
}

#include "FFmpegDecoder.h"

using namespace OrientView;

bool FFmpegDecoder::isRegistered = false;

namespace
{
	void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl)
	{
		if (level <= AV_LOG_WARNING)
		{
			int print_prefix = 0;
			char line[1024] = { 0 };
			av_log_format_line(ptr, level, fmt, vl, line, 1024, &print_prefix);
			qWarning(line);
		}
	}

	int openCodecContext(int* streamIndex, AVFormatContext* formatContext, enum AVMediaType mediaType)
	{
		int result = 0;
		AVStream* stream = nullptr;
		AVCodecContext* codecContext = nullptr;
		AVCodec* codec = nullptr;
		AVDictionary* opts = nullptr;

		if ((result = av_find_best_stream(formatContext, mediaType, -1, -1, nullptr, 0)) < 0)
		{
			qWarning("Could not find %s stream in input file", av_get_media_type_string(mediaType));
			return result;
		}
		else
		{
			*streamIndex = result;
			stream = formatContext->streams[*streamIndex];
			codecContext = stream->codec;
			codec = avcodec_find_decoder(codecContext->codec_id);

			if (!codec)
			{
				qWarning("Failed to find %s codec", av_get_media_type_string(mediaType));
				return AVERROR(EINVAL);
			}

			if ((result = avcodec_open2(codecContext, codec, &opts)) < 0)
			{
				qWarning("Failed to open %s codec", av_get_media_type_string(mediaType));
				return result;
			}
		}

		return 0;
	}
}

FFmpegDecoder::FFmpegDecoder()
{
	for (int i = 0; i < 8; ++i)
	{
		resizedPicture.data[i] = nullptr;
		resizedPicture.linesize[i] = 0;
	}
}

FFmpegDecoder::~FFmpegDecoder()
{
	shutdown();
}

bool FFmpegDecoder::initialize(const std::string& fileName)
{
	qDebug("Initializing FFmpegDecoder (%s)", fileName.c_str());

	if (!isRegistered)
	{
		av_log_set_callback(ffmpegLogCallback);
		av_register_all();

		isRegistered = true;
	}

	int result = 0;

	try
	{
		if ((result = avformat_open_input(&formatContext, fileName.c_str(), nullptr, nullptr)) < 0)
			throw std::runtime_error("Could not open source file");

		if ((result = avformat_find_stream_info(formatContext, nullptr)) < 0)
			throw std::runtime_error("Could not find stream information");

		if ((result = openCodecContext(&videoStreamIndex, formatContext, AVMEDIA_TYPE_VIDEO)) < 0)
			throw std::runtime_error("Could not open video codec context");
		else
		{
			videoStream = formatContext->streams[videoStreamIndex];
			videoCodecContext = videoStream->codec;

			if ((result = av_image_alloc(videoData, videoLineSize, videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt, 1)) < 0)
				throw std::runtime_error("Could not allocate raw video buffer");

			videoBufferSize = result;
		}

		if (!(frame = av_frame_alloc()))
		{
			result = AVERROR(ENOMEM);
			throw std::runtime_error("Could not allocate frame");
		}

		av_init_packet(&packet);
		packet.data = nullptr;
		packet.size = 0;

		resizeContext = sws_getContext(videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt, videoCodecContext->width, videoCodecContext->height, PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);

		if (!resizeContext)
			throw std::runtime_error("Could not get resize context");

		if (avpicture_alloc(&resizedPicture, PIX_FMT_RGBA, videoCodecContext->width, videoCodecContext->height) < 0)
			throw std::runtime_error("Could not allocate picture");

		preCalculatedFrameDuration = av_rescale_q(videoCodecContext->ticks_per_frame, videoCodecContext->time_base, AVRational{ 1, AV_TIME_BASE });
	}
	catch (const std::exception& ex)
	{
		char message[64] = { 0 };
		av_strerror(result, message, 64);
		qWarning("Could not open FFmpeg decoder: %s: %s", ex.what(), message);
		shutdown();

		return false;
	}

	qDebug("File opened successfully");

	isInitialized = true;
	return true;
}

void FFmpegDecoder::shutdown()
{
	qDebug("Shutting down FFmpegDecoder");

	avcodec_close(videoCodecContext);
	avformat_close_input(&formatContext);
	av_free(videoData[0]);
	av_frame_free(&frame);
	sws_freeContext(resizeContext);
	avpicture_free(&resizedPicture);

	formatContext = nullptr;
	videoCodecContext = nullptr;
	videoStream = nullptr;
	videoStreamIndex = -1;
	videoData[0] = nullptr;
	videoData[1] = nullptr;
	videoData[2] = nullptr;
	videoData[3] = nullptr;
	videoLineSize[0] = 0;
	videoLineSize[1] = 0;
	videoLineSize[2] = 0;
	videoLineSize[3] = 0;
	videoBufferSize = 0;
	frame = nullptr;
	resizeContext = nullptr;
	lastFrameTimestamp = 0;

	for (int i = 0; i < 8; ++i)
	{
		resizedPicture.data[i] = nullptr;
		resizedPicture.linesize[i] = 0;
	}

	isInitialized = false;
}

bool FFmpegDecoder::getNextFrame(DecodedFrame* decodedFrame)
{
	if (!isInitialized)
		return false;

	while (true)
	{
		if (av_read_frame(formatContext, &packet) >= 0)
		{
			if (packet.stream_index == videoStreamIndex)
			{
				int gotPicture = 0;
				int decodedBytes = avcodec_decode_video2(videoCodecContext, frame, &gotPicture, &packet);

				if (decodedBytes < 0)
				{
					qWarning("Could not decode video frame");
					av_free_packet(&packet);
					return false;
				}

				if (gotPicture)
				{
					sws_scale(resizeContext, frame->data, frame->linesize, 0, frame->height, resizedPicture.data, resizedPicture.linesize);
					
					int j = av_frame_get_best_effort_timestamp(frame);

					decodedFrame->data = resizedPicture.data[0];
					decodedFrame->dataLength = frame->height * resizedPicture.linesize[0];
					decodedFrame->stride = resizedPicture.linesize[0];
					decodedFrame->width = videoCodecContext->width;
					decodedFrame->height = frame->height;
					decodedFrame->duration = av_rescale_q(frame->best_effort_timestamp - lastFrameTimestamp, videoStream->time_base, AVRational{ 1, AV_TIME_BASE });
					lastFrameTimestamp = frame->best_effort_timestamp;

					if (decodedFrame->duration < 0 || decodedFrame->duration > 1000000)
					{
						qWarning("Could not calculate correct frame duration");
						decodedFrame->duration = preCalculatedFrameDuration;
					}

					av_free_packet(&packet);
					return true;
				}
			}
			
			av_free_packet(&packet);
		}
		else
			return false;
	}
}