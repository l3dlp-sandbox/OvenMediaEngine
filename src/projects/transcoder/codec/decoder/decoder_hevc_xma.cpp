//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "decoder_hevc_xma.h"

#include "../../transcoder_gpu.h"
#include "../../transcoder_private.h"
#include "base/info/application.h"

// #include <modules/bitstream/h264/h264_converter.h>
#include <modules/bitstream/h265/h265_decoder_configuration_record.h>
#include <modules/bitstream/nalu/nal_stream_converter.h>

// The Xilinx Video SDK decoder uses a 32-bit PTS. At some point, it wrap around and becomes negative. 
// To fix this, we changed it to calculate the PTS outside the codec.
#define USE_EXTERNAL_TIMESTAMP 	true

bool DecoderHEVCxXMA::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_decoder_by_name("mpsoc_vcu_hevc");
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

	_codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
	_codec_context->time_base = ffmpeg::compat::TimebaseToAVRational(GetTimebase());
	_codec_context->pkt_timebase = ffmpeg::compat::TimebaseToAVRational(GetTimebase());

	auto decoder_config = std::static_pointer_cast<HEVCDecoderConfigurationRecord>(GetRefTrack()->GetDecoderConfigurationRecord());

	if (decoder_config != nullptr)
	{
		_codec_context->profile = decoder_config->GeneralProfileIDC();
		_codec_context->level = decoder_config->GeneralLevelIDC();
		_codec_context->width = decoder_config->GetWidth();
		_codec_context->height = decoder_config->GetHeight();
	}

	// Set the SPS/PPS to extradata
	std::shared_ptr<const ov::Data> extra_data = nullptr;
	extra_data = decoder_config != nullptr ? decoder_config->GetData() : nullptr;
	if (extra_data != nullptr)
	{
		_codec_context->extradata_size = extra_data->GetLength();
		_codec_context->extradata = (uint8_t *)::av_malloc(_codec_context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
		::memset(_codec_context->extradata, 0, _codec_context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
		::memcpy(_codec_context->extradata, extra_data->GetData(), _codec_context->extradata_size);
	}

	::av_opt_set_int(_codec_context->priv_data, "lxlnx_hwdev", _track->GetCodecDeviceId(), 0);

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

void DecoderHEVCxXMA::UninitCodec()
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

#if USE_EXTERNAL_TIMESTAMP
	_pts_reorder_list.clear();
#endif

}

bool DecoderHEVCxXMA::ReinitCodecIfNeed()
{
	// Xilinx H.265 decoder does not support dynamic resolution streams. (e.g. WebRTC)
	// So, when a resolution change is detected, the codec is reset and recreated.
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

void DecoderHEVCxXMA::CodecThread()
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

		// auto packet_data = H264Converter::ConvertAnnexbToAvcc(buffer->GetData());
		auto packet_data = NalStreamConverter::ConvertAnnexbToXvcc(buffer->GetData(), buffer->GetFragHeader());
		if (packet_data == nullptr)
		{
			logtw("An error occurred while converting annexb to avcc");
			continue;
		}

		off_t offset = 0LL;
		int64_t remained = packet_data->GetLength();

		int64_t pts = (buffer->GetPts() == -1LL) ? AV_NOPTS_VALUE : buffer->GetPts();
		int64_t dts = (buffer->GetDts() == -1LL) ? AV_NOPTS_VALUE : buffer->GetDts();
		[[maybe_unused]] int64_t duration = (buffer->GetDuration() == -1LL) ? AV_NOPTS_VALUE : buffer->GetDuration();
		auto data = packet_data->GetDataAs<uint8_t>();

		while (remained > 0)
		{
			::av_packet_unref(_pkt);

			int parsed_size = ::av_parser_parse2(_parser, _codec_context, &_pkt->data, &_pkt->size, data + offset, static_cast<int>(remained), pts, dts, 0);
			if (parsed_size < 0)
			{
				logte("An error occurred while parsing: %d", parsed_size);
				break;
			}

			if (ReinitCodecIfNeed() == false)
			{
				break;
			}

			///////////////////////////////
			// Send to decoder
			///////////////////////////////
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

#if USE_EXTERNAL_TIMESTAMP
				_pts_reorder_list.push_back(_pkt->pts);
#endif				
			}

			OV_ASSERT(
				remained >= parsed_size,
				"Current data size MUST greater than parsed_size, but data size: %ld, parsed_size: %ld",
				remained, parsed_size);

			offset += parsed_size;
			remained -= parsed_size;
		}

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
				if (_frame->pkt_duration <= 0LL && _codec_context->framerate.num > 0 && _codec_context->framerate.den > 0)
				{
					_frame->pkt_duration = (int64_t)(((double)_codec_context->framerate.den / (double)_codec_context->framerate.num) / (double)GetRefTrack()->GetTimeBase().GetExpr());
				}

#if USE_EXTERNAL_TIMESTAMP
				_pts_reorder_list.sort();
				auto ordered_pts = _pts_reorder_list.front();
				// logtd("in: %lld, out: %lld (%s), list: %d", ordered_pts, _frame->pts, (ordered_pts == _frame->pts) ? "match" : "No match", _pts_reorder_list.size());
				_frame->pts = ordered_pts;
				_pts_reorder_list.pop_front();
#endif

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
