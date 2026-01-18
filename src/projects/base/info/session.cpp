#include "session.h"

#include <base/ovlibrary/ovlibrary.h>

#include <random>

#include "stream.h"

namespace info
{
	Session::Session(const info::Stream &stream)
	{
		_id			  = ov::Random::GenerateUInt32();
		_stream_info  = std::make_shared<info::Stream>(stream);
		_created_time = std::chrono::system_clock::now();
		SetIds(stream);
	}

	Session::Session(const info::Stream &stream, session_id_t session_id)
	{
		_id			  = session_id;
		_stream_info  = std::make_shared<info::Stream>(stream);
		_created_time = std::chrono::system_clock::now();
		SetIds(stream);
	}

	Session::Session(const info::Stream &stream, const Session &T)
	{
		_id			  = T._id;
		_stream_info  = std::make_shared<info::Stream>(stream);
		_created_time = std::chrono::system_clock::now();
		SetIds(stream);
	}

	session_id_t Session::GetId() const
	{
		return _id;
	}

	ov::String Session::GetUUID() const
	{
		if (_stream_info == nullptr)
		{
			return "";
		}

		return ov::String::FormatString("%s/%d", _stream_info->GetUUID().CStr(), GetId());
	}

	void Session::SetName(const ov::String &name)
	{
		_name = name;
	}

	const std::optional<ov::String> &Session::GetName() const
	{
		return _name;
	}

	const std::chrono::system_clock::time_point &Session::GetCreatedTime() const
	{
		return _created_time;
	}
	uint64_t Session::GetSentBytes()
	{
		return _sent_bytes;
	}

	void Session::SetIds(const info::Stream &stream)
	{
		_host_id		= stream.GetApplicationInfo().GetHostInfo().GetId();
		_application_id = stream.GetApplicationInfo().GetId();
		_stream_id		= stream.GetId();
	}

	info::host_id_t Session::GetHostId() const
	{
		return _host_id;
	}

	info::application_id_t Session::GetApplicationId() const
	{
		return _application_id;
	}

	info::stream_id_t Session::GetStreamId() const
	{
		return _stream_id;
	}

}  // namespace info