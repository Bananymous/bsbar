#include "PulseAudio.h"

#include <pulse/pulseaudio.h>

#include <iostream>

namespace bsbar
{

	struct PaData
	{
		std::atomic<bool>	initialized		= false;

		pa_mainloop*		mainloop		= NULL;
		pa_mainloop_api*	mainloop_api	= NULL;
		pa_context*			context			= NULL;
	};
	static PaData		s_pa_data;
	static std::thread	s_thread;

	struct VolumeInfo
	{
		double		volume	= 0.0;
		bool		muted	= false;
		std::string	sink;
	};
	static VolumeInfo s_volume_info;

	static std::mutex s_mutex;


	static int pa_run()
	{
		int ret = 1;
		if (pa_mainloop_run(s_pa_data.mainloop, &ret) < 0)
			return ret;
		return ret;
	}

	static void pa_quit(int ret)
	{
		if (s_pa_data.mainloop_api)
			s_pa_data.mainloop_api->quit(s_pa_data.mainloop_api, ret);
	}


	static void sink_info_callback(pa_context* c, const pa_sink_info* info, int eol, void*)
	{
		if (!info)
			return;
		double volume = (double)pa_cvolume_avg(&(info->volume)) / (double)PA_VOLUME_NORM;

		std::scoped_lock _(s_mutex);
		s_volume_info.volume = volume * 100.0;
		s_volume_info.muted = info->mute;
	}

	static void server_info_callback(pa_context *c, const pa_server_info *i, void*)
	{
		pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_callback, NULL);

		std::scoped_lock _(s_mutex);
		s_volume_info.sink = i->default_sink_name;
	}

	static void subscribe_callback(pa_context* c, pa_subscription_event_type_t type, uint32_t idx, void*)
	{
		unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
		//type &= PA_SUBSCRIPTION_EVENT_TYPE_MASK;

		pa_operation *op = NULL;

		switch (facility)
		{
			case PA_SUBSCRIPTION_EVENT_SINK:
				op = pa_context_get_sink_info_by_index(c, idx, sink_info_callback, NULL);
				break;

			default:
				assert(false);
				break;
		}

		if (op)
			pa_operation_unref(op);
	}

	static void context_state_callback(pa_context* context, void*)
	{
		switch (pa_context_get_state(context))
		{
			case PA_CONTEXT_CONNECTING:
			case PA_CONTEXT_AUTHORIZING:
			case PA_CONTEXT_SETTING_NAME:
				break;

			case PA_CONTEXT_READY:
				pa_context_get_server_info(context, server_info_callback, NULL);

				pa_context_set_subscribe_callback(context, subscribe_callback, NULL);
				pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
				break;

			case PA_CONTEXT_TERMINATED:
				pa_quit(0);
				break;

			case PA_CONTEXT_FAILED:
			default:
				pa_quit(1);
				break;
		}
	}

	static bool pa_initialize()
	{
		s_pa_data.mainloop = pa_mainloop_new();
		if (!s_pa_data.mainloop)
		{
			std::cerr << "pa_mainloop_new()" << std::endl;
			return false;
		}

		s_pa_data.mainloop_api = pa_mainloop_get_api(s_pa_data.mainloop);

		s_pa_data.context = pa_context_new(s_pa_data.mainloop_api, "PulseAudio Test");
		if (!s_pa_data.context)
		{
			std::cerr << "pa_context_new()" << std::endl;
			return false;
		}
		
		if (pa_context_connect(s_pa_data.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
		{
			std::cerr << "pa_context_connect()" << std::endl;
			return false;
		}

		pa_context_set_state_callback(s_pa_data.context, context_state_callback, NULL);

		return true;
	}

	static void pa_cleanup()
	{
		if (s_pa_data.context)
		{
			pa_context_unref(s_pa_data.context);
			s_pa_data.context = NULL;
		}

		if (s_pa_data.mainloop)
		{
			pa_mainloop_free(s_pa_data.mainloop);
			s_pa_data.mainloop = NULL;
			s_pa_data.mainloop_api = NULL;
		}
	}












	bool PulseAudioBlock::custom_update(time_point tp)
	{
		std::scoped_lock _1(s_mutex);
		if (!s_pa_data.initialized)
		{
			if (!pa_initialize())
				exit(1);
			s_pa_data.initialized = true;
			s_thread = std::thread(&pa_run);
		}

		std::scoped_lock _2(m_mutex);
		m_text = m_format;
		m_value.value = s_volume_info.volume;

		return true;
	}

	bool PulseAudioBlock::add_custom_config(std::string_view key, toml::node& value)
	{

		return false;
	}

	bool PulseAudioBlock::handle_custom_scroll(const MouseInfo& mouse)
	{
		switch (mouse.type)
		{
			case MouseType::ScrollUp:
				system("pactl set-sink-volume @DEFAULT_SINK@ +5%");
				break;
			case MouseType::ScrollDown:
				system("pactl set-sink-volume @DEFAULT_SINK@ -5%");
				break;
		}

		return true;
	}
	
}