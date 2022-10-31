#include "PulseAudio.h"

#include "Common.h"

#include <pulse/pulseaudio.h>

#include <iostream>

namespace bsbar
{
	using namespace std::chrono_literals;

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

		template<typename Rep, typename Period>
		void wait_update_for(std::unique_lock<std::mutex>& lock, const std::chrono::duration<Rep, Period>& duration)
		{
			updated = false;
			cv.wait_for(lock, duration, [this]() { return updated; });
		}

		void update()
		{
			updated = true;
			cv.notify_all();
		}
	};
	static VolumeInfo s_volume_info;

	template<typename T>
	static T pa_cvolume_to_percentage(const pa_cvolume* volume)
	{
		if (!pa_cvolume_valid(volume))
			return 0.0;
		return (T)pa_cvolume_avg(volume) / (T)PA_VOLUME_NORM * T(100);
	}

	template<typename T>
	static constexpr pa_volume_t percentage_to_pa_volume_t(T percentage)
	{
		return (pa_volume_t)(percentage * T(PA_VOLUME_NORM) / T(100));
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



	PulseAudioBlock::PulseAudioBlock()
	{
		if (!s_pa_data.initialized)
		{
			if (!pa_initialize())
				exit(1);
			atexit(pa_cleanup);
			s_pa_data.initialized = true;
			s_thread = std::thread(&pa_run);
		}
	}

	bool PulseAudioBlock::add_custom_config(std::string_view key, toml::node& value)
	{
		if (key == "format-muted")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_format_muted = **value.as_string();
			return true;
		}
		else if (key == "color-muted")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_color_muted = **value.as_string();
			return true;
		}
		else if (key == "max-volume")
		{
			BSBAR_VERIFY_TYPE(value, number, key);
			if (value.is_integer())
				m_max_volume = **value.as_integer();
			else
				m_max_volume = **value.as_floating_point();
			return true;
		}
		else if (key == "volume-step")
		{
			BSBAR_VERIFY_TYPE(value, number, key);
			if (value.is_integer())
				m_volume_step = **value.as_integer();
			else
				m_volume_step = **value.as_floating_point();
			return true;
		}

		return false;
	}

	bool PulseAudioBlock::custom_update(time_point tp)
	{
		std::scoped_lock _(s_volume_info.mutex, m_mutex);

		if (m_format_muted && s_volume_info.muted)
			m_text = *m_format_muted;
		else
			m_text = m_format;

		if (m_color_muted && s_volume_info.muted)
			m_i3bar["color"] = { .is_string = true, .value = *m_color_muted };
		else
			m_i3bar.erase("color");

		auto max_volume = percentage_to_pa_volume_t(m_max_volume);
		if (pa_cvolume_max(&s_volume_info.volume) > max_volume)
		{
			if (!pa_cvolume_set(&s_volume_info.volume, s_volume_info.volume.channels, max_volume))
				return false;
			auto op = pa_context_set_sink_volume_by_index(s_pa_data.context, s_volume_info.index, &s_volume_info.volume, NULL, NULL);
			pa_operation_unref(op);
		}

		m_value.value = pa_cvolume_to_percentage<double>(&s_volume_info.volume);

		return true;
	}

	bool PulseAudioBlock::handle_custom_click(const MouseInfo& mouse)
	{
		std::unique_lock lock(s_volume_info.mutex);

		auto op = pa_context_set_sink_mute_by_index(s_pa_data.context, s_volume_info.index, !s_volume_info.muted, NULL, NULL);
		s_volume_info.wait_update_for(lock, 100ms);
		pa_operation_unref(op);

		return true;
	}

	bool PulseAudioBlock::handle_custom_scroll(const MouseInfo& mouse)
	{
		pa_cvolume temp;
		{
			std::scoped_lock _(s_volume_info.mutex);
			temp = s_volume_info.volume;
		}

		auto step		= percentage_to_pa_volume_t(m_volume_step);
		auto max_volume	= percentage_to_pa_volume_t(m_max_volume);

		switch (mouse.type)
		{
			case MouseType::ScrollUp:
				if (!pa_cvolume_inc_clamp(&temp, step, max_volume))
					return false;
				break;
			case MouseType::ScrollDown:
				if (!pa_cvolume_dec(&temp, step))
					return false;
				break;
		}

		std::unique_lock lock(s_volume_info.mutex);

		if (!pa_cvolume_equal(&temp, &s_volume_info.volume))
		{
			auto op = pa_context_set_sink_volume_by_index(s_pa_data.context, s_volume_info.index, &temp, NULL, NULL);
			s_volume_info.wait_update_for(lock, 100ms);
			pa_operation_unref(op);
		}

		return true;
	}
	
}
