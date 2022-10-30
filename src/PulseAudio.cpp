#include "PulseAudio.h"

#include <pulse/pulseaudio.h>

#include <iostream>

namespace bsbar
{

	struct PaData
	{
		bool				initialized		= false;

		pa_mainloop*		mainloop		= NULL;
		pa_mainloop_api*	mainloop_api	= NULL;
		pa_context*			context			= NULL;
	};
	static PaData		s_pa_data;
	static std::thread	s_thread;

	struct VolumeInfo
	{
		pa_cvolume	volume	= {};
		bool		muted	= false;
		uint32_t	index	= 0;
		std::string	sink;



		bool					updated	= false;
		std::mutex				mutex;
		std::condition_variable	cv;

		void wait_update(std::unique_lock<decltype(mutex)>& lock)
		{
			updated = false;
			cv.wait(lock, [this]() { return updated; });
		}

		void update()
		{
			updated = true;
			cv.notify_all();
		}
	};
	static VolumeInfo s_volume_info;


	static double pa_cvolume_to_percentage(const pa_cvolume* volume)
	{
		if (!pa_cvolume_valid(volume))
			return 0.0;
		return (double)pa_cvolume_avg(volume) / (double)PA_VOLUME_NORM * 100.0;
	}

	static pa_volume_t percentage_to_pa_volume_t(double percentage)
	{
		return (pa_volume_t)(percentage * (double)PA_VOLUME_NORM / 100.0);
	}


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


		std::scoped_lock _(s_volume_info.mutex);

		s_volume_info.volume	= info->volume;
		s_volume_info.muted		= info->mute;
		s_volume_info.index		= info->index;

		s_volume_info.update();
	}

	static void server_info_callback(pa_context* c, const pa_server_info *i, void*)
	{
		pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_callback, NULL);

		std::scoped_lock _(s_volume_info.mutex);
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
			case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
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
		std::scoped_lock _(s_volume_info.mutex, m_mutex);
		if (!s_pa_data.initialized)
		{
			if (!pa_initialize())
				exit(1);
			atexit(pa_cleanup);
			s_pa_data.initialized = true;
			s_thread = std::thread(&pa_run);
		}

		m_text = m_format;
		m_value.value = pa_cvolume_to_percentage(&s_volume_info.volume);

		if (s_volume_info.muted)
			m_text = "mute";
		
		return true;
	}

	bool PulseAudioBlock::add_custom_config(std::string_view key, toml::node& value)
	{

		return false;
	}


	bool PulseAudioBlock::handle_custom_click(const MouseInfo& mouse)
	{
		std::unique_lock lock(s_volume_info.mutex);

		pa_context_set_sink_mute_by_index(s_pa_data.context, s_volume_info.index, !s_volume_info.muted, NULL, NULL);
		s_volume_info.wait_update(lock);

		return true;
	}

	bool PulseAudioBlock::handle_custom_scroll(const MouseInfo& mouse)
	{
		std::unique_lock lock(s_volume_info.mutex);

		if (s_volume_info.sink.empty())
			return false;

		auto diff = percentage_to_pa_volume_t(5.0);

		switch (mouse.type)
		{
			case MouseType::ScrollUp:
				if (!pa_cvolume_inc(&s_volume_info.volume, diff))
					return false;
				break;
			case MouseType::ScrollDown:
				if (!pa_cvolume_dec(&s_volume_info.volume, diff))
					return false;
				break;
		}

		pa_context_set_sink_volume_by_index(s_pa_data.context, s_volume_info.index, &s_volume_info.volume, NULL, NULL);
		s_volume_info.wait_update(lock);

		return true;
	}
	
}