//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/rtmp_v2/rtmp.h>

#include "../rtmp_definitions.h"

namespace pvd::rtmp
{
	class RtmpStreamV2;

	// This is a partial class of `RtmpStream`, which is separated to prevent
	// the `RtmpStream` class from becoming too large.
	// It is separated to prevent the `RtmpStream` class from becoming too large.
	class RtmpChunkHandler
	{
	public:
		struct Stats
		{
			int64_t last_stream_check_time = 0;

			uint32_t key_frame_interval = 0;
			uint32_t previous_key_frame_timestamp = 0;
			uint32_t last_audio_timestamp = 0;
			uint32_t last_video_timestamp = 0;
			uint32_t previous_last_audio_timestamp = 0;
			uint32_t previous_last_video_timestamp = 0;
			uint32_t audio_frame_count = 0;
			uint32_t video_frame_count = 0;

			Stats();
			int64_t GetElapsedInMs() const;
			void ResetCheckTime();
			int32_t GetVADelta() const;
			ov::String GetStatsString(int64_t elapsed_ms) const;
		};

		struct MediaInfo
		{
		public:
			bool ignore_audio_packets = false;
			bool ignore_video_packets = false;

			// If `audio_codec_id` is `std::nullopt`, it means that the codec ID was not found in the metadata or other sources
			std::optional<cmn::MediaCodecId> audio_codec_id;
			ov::String audio_codec_raw;
			std::optional<cmn::AudioChannel::Layout> audio_channel_layout;
			std::optional<int> audio_bits;
			std::optional<int> audio_samplerate;
			std::optional<int> audio_sampleindex;
			std::optional<int> audio_bitrate;

			// If `video_codec_id` is `std::nullopt`, it means that the codec ID was not found in the metadata or other sources
			std::optional<cmn::MediaCodecId> video_codec_id;
			ov::String video_codec_raw;
			int video_width = 0;
			int video_height = 0;
			float video_framerate = 0;
			int video_bitrate = 0;

			uint32_t timestamp_scale = modules::rtmp::TIME_SCALE;
			EncoderType encoder_type = EncoderType::Custom;
			ov::String encoder_name;

			bool HasAudio() const
			{
				return audio_codec_id.has_value();
			}
			bool IsSupportedAudio() const
			{
				return audio_codec_id.has_value() && (audio_codec_id.value() != cmn::MediaCodecId::None);
			}

			bool HasVideo() const
			{
				return video_codec_id.has_value();
			}
			bool IsSupportedVideo() const
			{
				return video_codec_id.has_value() && (video_codec_id.value() != cmn::MediaCodecId::None);
			}

			bool IsNoAudioVideo() const
			{
				return (HasAudio() == false) && (HasVideo() == false);
			}

			ov::String ToString() const;
		};

	public:
		RtmpChunkHandler(RtmpStreamV2 *stream);

		int32_t HandleData(const std::shared_ptr<const ov::Data> &data);

		void SetVhostAppName(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);

		int GetWaitingTrackCount() const;

		void AccumulateAcknowledgementSize(size_t data_size);

		void SetEventGeneratorConfig(const cfg::vhost::app::pvd::EventGenerator &event_generator_config)
		{
			_event_generator_config = event_generator_config;
		}

	private:
		std::shared_ptr<modules::rtmp::ChunkWriteInfo> CreateUserControlMessage(modules::rtmp::UserControlEventType message_id, size_t payload_length = 0);

		bool SendMessage(const std::shared_ptr<const modules::rtmp::ChunkWriteInfo> &chunk_write_info);
		bool SendAmfCommand(const std::shared_ptr<modules::rtmp::ChunkWriteInfo> &message_header, const modules::rtmp::AmfDocument &document);
		bool SendWindowAcknowledgementSize(uint32_t size);
		bool SendSetPeerBandwidth(uint32_t bandwidth);
		bool SendStreamBegin(uint32_t stream_id);
		bool SendStreamEnd();
		bool SendAmfConnectSuccess(uint32_t chunk_stream_id, double transaction_id, double object_encoding);
		bool SendAmfAckResult(uint32_t chunk_stream_id, double transaction_id);
		bool SendAmfOnFCPublish(uint32_t chunk_stream_id, uint32_t stream_id, double client_id);
		bool SendAmfOnStatus(uint32_t chunk_stream_id, uint32_t stream_id, const char *level, const char *code, const char *description, double client_id);

		// The `OnAmf*` functions are called by the `HandleAmf0*` functions
		bool OnAmfConnect(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, modules::rtmp::AmfDocument &document, double transaction_id);
		bool OnAmfCreateStream(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, modules::rtmp::AmfDocument &document, double transaction_id);
		bool OnAmfDeleteStream(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, modules::rtmp::AmfDocument &document, double transaction_id);
		bool OnAmfPublish(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, modules::rtmp::AmfDocument &document, double transaction_id);
		bool OnAmfFCPublish(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, modules::rtmp::AmfDocument &document, double transaction_id);
		bool OnAmfMetadata(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, const modules::rtmp::AmfProperty *property);

		void GenerateEvent(const cfg::vhost::app::pvd::Event &event, const ov::String &value);
		bool CheckEvent(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, modules::rtmp::AmfDocument &document);

		bool SendAcknowledgementSize(uint32_t acknowledgement_traffic);

		bool HandleAmf0Command(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleSetChunkSize(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleAcknowledgement(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleUserControl(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleWindowAcknowledgementSize(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleAmf0Data(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleAudio(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleVideo(const std::shared_ptr<const modules::rtmp::Message> &message);
		bool HandleChunkMessage();

		// Unused
		bool OnTextData(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, const modules::rtmp::AmfDocument &document);
		bool OnCuePoint(const std::shared_ptr<const modules::rtmp::ChunkHeader> &header, const modules::rtmp::AmfDocument &document);

	private:
		RtmpStreamV2 *_stream;

		modules::rtmp::ChunkParser _chunk_parser;
		modules::rtmp::ChunkWriter _chunk_writer;

		uint32_t _acknowledgement_size = DEFAULT_ACKNOWNLEDGEMENT_SIZE / 2;
		// Accumulated amount of traffic since the stream started
		uint32_t _acknowledgement_traffic = 0;
		// The accumulated amount of traffic since the last ACK was sent
		uint32_t _acknowledgement_traffic_after_last_acked = 0;

		// If we count only the track count without distinguishing between audio and video,
		// in a situation where V+V+A comes in without metadata,
		// if V+V comes in, the stream can start, which is an issue.
		// Therefore, we count A/V separately.
		// This is used to determine whether the stream is ready to publish.
		int _waiting_audio_track_count = 0;
		int _waiting_video_track_count = 0;

		MediaInfo _media_info;

		// Data frame
		int64_t _last_video_pts = 0;
		ov::StopWatch _last_video_pts_clock;
		int64_t _last_audio_pts = 0;
		ov::StopWatch _last_audio_pts_clock;

		// For statistics
		Stats _stats;

		std::vector<std::shared_ptr<const modules::rtmp::Message>> _message_buffer;
		size_t _audio_buffer_count = 0;
		size_t _video_buffer_count = 0;

		cfg::vhost::app::pvd::EventGenerator _event_generator_config;
	};
}  // namespace pvd::rtmp
