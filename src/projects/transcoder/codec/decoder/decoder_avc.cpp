//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "decoder_avc.h"

#include "../../transcoder_private.h"
#include "base/info/application.h"


bool DecoderAVC::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_decoder(ffmpeg::compat::ToAVCodecId(GetCodecID()));
	if (codec == nullptr)
	{
		logte("Codec not found: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_codec_context = ::avcodec_alloc_context3(codec);
	if (_codec_context == nullptr)
	{
		logte("Could not allocate codec context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_codec_context->time_base = ffmpeg::compat::TimebaseToAVRational(GetTimebase());
	_codec_context->thread_count = GetRefTrack()->GetThreadCount();
	_codec_context->thread_type = FF_THREAD_FRAME;

	// Set the number of b frames for compatibility with specific encoders.
	auto bframes = GetRefTrack()->HasBframes()?1:0;
	if (bframes > 0)
	{
		_codec_context->has_b_frames = bframes;
	}

	if (::avcodec_open2(_codec_context, nullptr, nullptr) < 0)
	{
		logte("Could not open codec: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_parser = ::av_parser_init(ffmpeg::compat::ToAVCodecId(GetCodecID()));
	if (_parser == nullptr)
	{
		logte("Parser not found");
		return false;
	}
	_parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;


	_change_format = false;

	return true;
}

void DecoderAVC::UninitCodec()
{
	if (_codec_context != nullptr)
	{
		::avcodec_free_context(&_codec_context);
	}
	_codec_context = nullptr;

	if (_parser != nullptr)
	{
		::av_parser_close(_parser);
	}
	_parser = nullptr;
}

bool DecoderAVC::ReinitCodecIfNeed()
{
	if (_codec_context->width != 0 && _codec_context->height != 0 && (_parser->width != _codec_context->width || _parser->height != _codec_context->height))
	{
		logti("Changed input resolution of %u track. (%dx%d -> %dx%d)", GetRefTrack()->GetId(), _codec_context->width, _codec_context->height, _parser->width, _parser->height);

		UninitCodec();

		if (InitCodec() == false)
		{
			return false;
		}
	}

	return true;
}

void DecoderAVC::CodecThread()
{
	// Initialize the codec and notify the main thread.
	if(_codec_init_event.Submit(InitCodec()) == false)
	{
		return;
	}

	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
		{
			// logte("An error occurred while dequeue : no data");
			continue;
		}

		auto buffer = std::move(obj.value());

		auto packet_data = buffer->GetData();

		int64_t remained_size = packet_data->GetLength();
		off_t offset = 0LL;
		int64_t pts = (buffer->GetPts() == -1LL) ? AV_NOPTS_VALUE : buffer->GetPts();
		int64_t dts = (buffer->GetDts() == -1LL) ? AV_NOPTS_VALUE : buffer->GetDts();
		[[maybe_unused]] int64_t duration = (buffer->GetDuration() == -1LL) ? AV_NOPTS_VALUE : buffer->GetDuration();
		auto data = packet_data->GetDataAs<uint8_t>();

		///////////////////////////////
		// Send to decoder
		///////////////////////////////
		while (remained_size > 0)
		{
			::av_packet_unref(_pkt);

			int parsed_size = ::av_parser_parse2(_parser, _codec_context, &_pkt->data, &_pkt->size,
												 data + offset, static_cast<int>(remained_size), pts, dts, 0);
			if (parsed_size < 0)
			{
				logte("An error occurred while parsing: %d", parsed_size);
				break;
			}

			if(ReinitCodecIfNeed() == false)
			{
				logte("An error occurred while reinit codec");
				break;
			}

			if (_pkt->size > 0)
			{
				_pkt->pts = _parser->pts;
				_pkt->dts = _parser->dts;
				_pkt->flags = (_parser->key_frame == 1) ? AV_PKT_FLAG_KEY : 0;
				_pkt->duration = _pkt->dts - _parser->last_dts;
				if (_pkt->duration <= 0LL)
				{
					// It may not be the exact packet duration.
					// However, in general, this method is applied under the assumption that the duration of all packets is similar.
					_pkt->duration = duration;
				}

				// Keyframe Decode Only
				// If set to decode only key frames, non-keyframe packets are dropped.
				if(GetRefTrack()->IsKeyframeDecodeOnly() == true)
				{
					// Drop non-keyframe packets
					if (!(_pkt->flags & AV_PKT_FLAG_KEY))
					{
						break;
					}
				}

				int ret = ::avcodec_send_packet(_codec_context, _pkt);
				if (ret == AVERROR(EAGAIN))
				{
					// Nothing to do here, just continue
				}
				else if (ret == AVERROR_INVALIDDATA)
				{
					// If only SPS/PPS Nalunit is entered in the decoder, an invalid data error occurs.
					// There is no particular problem.
					auto empty_frame = std::make_shared<MediaFrame>();
					empty_frame->SetPts(dts);
					empty_frame->SetMediaType(cmn::MediaType::Video);

					Complete(TranscodeResult::NoData, std::move(empty_frame));

					break;
				}
				else if (ret < 0)
				{
					logte("Error error occurred while sending a packet for decoding. reason(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());
					break;
				}
			}

			OV_ASSERT(remained_size >= parsed_size, "Current data size MUST greater than parsed_size, but data size: %ld, parsed_size: %ld",
					  remained_size, parsed_size);

			offset += parsed_size;
			remained_size -= parsed_size;
		}

		///////////////////////////////
		// Receive from decoder
		///////////////////////////////
		while (!_kill_flag)
		{
			// Check the decoded frame is available
			int ret = ::avcodec_receive_frame(_codec_context, _frame);
			if (ret == AVERROR(EAGAIN))
			{
				break;
			}
			else if (ret < 0)
			{
				logte("Error receiving a packet for decoding. reason(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());
				continue;
			}
			else
			{
				// Update codec information if needed
				if (_change_format == false)
				{
					auto codec_info = ffmpeg::compat::CodecInfoToString(_codec_context);

					logti("[%s/%s(%u)] input track information: %s",
						  _stream_info.GetApplicationInfo().GetVHostAppName().CStr(), _stream_info.GetName().CStr(), _stream_info.GetId(), codec_info.CStr());
				}

				// If there is no duration, the duration is calculated by framerate and timebase.
				if(_frame->pkt_duration <= 0LL && _codec_context->framerate.num > 0 && _codec_context->framerate.den > 0)
				{
					_frame->pkt_duration = (int64_t)(((double)_codec_context->framerate.den / (double)_codec_context->framerate.num) / (double)GetRefTrack()->GetTimeBase().GetExpr());
				}
				
				auto decoded_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Video, _frame);
				::av_frame_unref(_frame);
				if (decoded_frame == nullptr)
				{
					continue;
				}

				Complete(!_change_format ? TranscodeResult::FormatChanged : TranscodeResult::DataReady, std::move(decoded_frame));
				_change_format = true;
			}
		}
	}
}
