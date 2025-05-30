//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_decoder.h"

class DecoderHEVC : public TranscodeDecoder
{
public:
    DecoderHEVC(const info::Stream &stream_info)
        : TranscodeDecoder(stream_info)
    {

    }

    cmn::MediaCodecId GetCodecID() const noexcept override
    {
        return cmn::MediaCodecId::H265;
    }

	bool InitCodec();
	void UninitCodec();
	bool ReinitCodecIfNeed();

    void CodecThread() override;
};
