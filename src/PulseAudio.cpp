#include "PulseAudio.h"

#include "Common.h"

#include <pulse/pulseaudio.h>

#include <iostream>

namespace bsbar
{
	using namespace std::chrono_literals;

	// Does not need mutex since it's readonly after initialization?
	struct ServerInfo
	{
		bool				initialized		= false;

		pa_mainloop*		mainloop		= NULL;
		pa_mainloop_api*	mainloop_api	= NULL;
		pa_context*			context			= NULL;
	};
	static ServerInfo	s_server_info;
	static std::thread	s_thread;

	struct VolumeInfo
	{
		pa_cvolume				volume	= {};
		bool					muted	= false;
		uint32_t				index	= 0;

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
	static VolumeInfo s_sink_info;
	static VolumeInfo s_source_info;

	template<typename T>
	static T pa_cvolume_to_percentage(const pa_cvolume* volume)
	{
		if (!pa_cvolume_valid(volume))
			return T(0);
		return (T)pa_cvolume_avg(volume) / T(PA_VOLUME_NORM) * T(100);
	}

	template<typename T>
	static constexpr pa_volume_t percentage_to_pa_volume_t(T percentage)
	{
		return (pa_volume_t)(percentage * T(PA_VOLUME_NORM) / T(100));
	}


	static int pa_run()
	{
		int ret = 1;
		if (pa_mainloop_run(s_server_info.mainloop, &ret) < 0)
			return ret;
		return ret;
	}

	static void pa_quit(int ret)
	{
		if (s_server_info.mainloop_api)
			s_server_info.mainloop_api->quit(s_server_info.mainloop_api, ret);
	}


	static void sink_info_callback(pa_context* context, const pa_sink_info* info, int eol, void*)
	{
		if (!info)
			return;

		std::scoped_lock _(s_sink_info.mutex);

		s_sink_info.volume	= info->volume;
		s_sink_info.muted	= info->mute;
		s_sink_info.index	= info->index;

		s_sink_info.update();
	}

	static void source_info_callback(pa_context* context, const pa_source_info* info, int eol, void*)
	{
		if (!info)
			return;

		std::scoped_lock _(s_source_info.mutex);

		s_source_info.volume	= info->volume;
		s_source_info.muted		= info->mute;
		s_source_info.index		= info->index;

		s_source_info.update();
	}

	static void server_info_callback(pa_context* context, const pa_server_info* info, void*)
	{
		pa_context_get_sink_info_by_name(context, info->default_sink_name, sink_info_callback, NULL);
		pa_context_get_source_info_by_name(context, info->default_source_name, source_info_callback, NULL);
	}

	static void subscribe_callback(pa_context* context, pa_subscription_event_type_t type, uint32_t index, void*)
	{
		unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

		pa_operation *op = NULL;

		switch (facility)
		{
			case PA_SUBSCRIPTION_EVENT_SINK:
				op = pa_context_get_sink_info_by_index(context, index, sink_info_callback, NULL);
				break;

			case PA_SUBSCRIPTION_EVENT_SOURCE:
				op = pa_context_get_source_info_by_index(context, index, source_info_callback, NULL);
				break;

			case PA_SUBSCRIPTION_EVENT_SERVER:
				op = pa_context_get_server_info(context, server_info_callback, NULL);
				break;

			default:
				fprintf(stderr, "pa_event %u\n", facility);
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
				pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_ALL, NULL, NULL);
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
		if (s_server_info.initialized)
			return true;

		s_server_info.mainloop = pa_mainloop_new();
		if (!s_server_info.mainloop)
		{
			std::cerr << "pa_mainloop_new()" << std::endl;
			return false;
		}

		s_server_info.mainloop_api = pa_mainloop_get_api(s_server_info.mainloop);

		s_server_info.context = pa_context_new(s_server_info.mainloop_api, "PulseAudio Test");
		if (!s_server_info.context)
		{
			std::cerr << "pa_context_new()" << std::endl;
			return false;
		}
		
		if (pa_context_connect(s_server_info.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
		{
			std::cerr << "pa_context_connect()" << std::endl;
			return false;
		}

		pa_context_set_state_callback(s_server_info.context, context_state_callback, NULL);

		s_server_info.initialized = true;
		s_thread = std::thread(&pa_run);

		return true;
	}

	static void pa_cleanup()
	{
		if (s_server_info.context)
		{
			pa_context_unref(s_server_info.context);
			s_server_info.context = NULL;
		}

		if (s_server_info.mainloop)
		{
			pa_mainloop_free(s_server_info.mainloop);
			s_server_info.mainloop = NULL;
			s_server_info.mainloop_api = NULL;
		}
	}



	PulseAudioBlock::PulseAudioBlock()
	{
		if (!pa_initialize())
			exit(1);
		
		m_max_volume	= percentage_to_pa_volume_t<uint32_t>(100);
		m_volume_step	= percentage_to_pa_volume_t<uint32_t>(5);
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
				m_max_volume = percentage_to_pa_volume_t(**value.as_integer());
			else
				m_max_volume = percentage_to_pa_volume_t(**value.as_floating_point());
			return true;
		}
		else if (key == "volume-step")
		{
			BSBAR_VERIFY_TYPE(value, number, key);
			if (value.is_integer())
				m_volume_step = percentage_to_pa_volume_t(**value.as_integer());
			else
				m_volume_step = percentage_to_pa_volume_t(**value.as_floating_point());
			return true;
		}
		else if (key == "global-volume-cap")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_verify_volume = **value.as_boolean();
			return true;
		}
		else if (key == "enable-scroll")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_enable_scroll = **value.as_boolean();
			return true;
		}
		else if (key == "click-to-mute")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_click_to_mute = **value.as_boolean();
			return true;
		}

		return false;
	}

	bool PulseAudioBlock::custom_update(time_point tp)
	{
		std::scoped_lock _(s_sink_info.mutex, m_mutex);

		if (m_format_muted && s_sink_info.muted)
			m_text = *m_format_muted;
		else
			m_text = m_format;

		if (m_color_muted && s_sink_info.muted)
			m_i3bar["color"] = { .is_string = true, .value = *m_color_muted };
		else
			m_i3bar.erase("color");

		if (m_verify_volume && pa_cvolume_max(&s_sink_info.volume) > m_max_volume)
		{
			if (!pa_cvolume_set(&s_sink_info.volume, s_sink_info.volume.channels, m_max_volume))
				return false;
			auto op = pa_context_set_sink_volume_by_index(s_server_info.context, s_sink_info.index, &s_sink_info.volume, NULL, NULL);
			pa_operation_unref(op);
		}

		m_value.value = pa_cvolume_to_percentage<double>(&s_sink_info.volume);

		return true;
	}

	bool PulseAudioBlock::handle_custom_click(const MouseInfo& mouse)
	{
		if (!m_click_to_mute)
			return true;

		if (mouse.type != MouseType::Left)
			return true;

		std::unique_lock lock(s_sink_info.mutex);

		auto op = pa_context_set_sink_mute_by_index(s_server_info.context, s_sink_info.index, !s_sink_info.muted, NULL, NULL);
		s_sink_info.wait_update_for(lock, 100ms);
		pa_operation_unref(op);

		return true;
	}

	bool PulseAudioBlock::handle_custom_scroll(const MouseInfo& mouse)
	{
		if (!m_enable_scroll)
			return true;

		pa_cvolume temp;
		{
			std::scoped_lock _(s_sink_info.mutex);
			temp = s_sink_info.volume;
		}

		switch (mouse.type)
		{
			case MouseType::ScrollUp:
				if (!pa_cvolume_inc_clamp(&temp, m_volume_step, m_max_volume))
					return false;
				break;
			case MouseType::ScrollDown:
				if (!pa_cvolume_dec(&temp, m_volume_step))
					return false;
				break;
		}

		std::unique_lock lock(s_sink_info.mutex);

		if (!pa_cvolume_equal(&temp, &s_sink_info.volume))
		{
			auto op = pa_context_set_sink_volume_by_index(s_server_info.context, s_sink_info.index, &temp, NULL, NULL);
			s_sink_info.wait_update_for(lock, 100ms);
			pa_operation_unref(op);
		}

		return true;
	}



	PulseAudioInputBlock::PulseAudioInputBlock()
	{
		if (!pa_initialize())
			exit(1);
	}

	bool PulseAudioInputBlock::add_custom_config(std::string_view key, toml::node& value)
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
		else if (key == "click-to-mute")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_click_to_mute = **value.as_boolean();
			return true;
		}

		return false;
	}

	bool PulseAudioInputBlock::custom_update(time_point tp)
	{
		std::scoped_lock _(s_source_info.mutex, m_mutex);

		if (m_format_muted && s_source_info.muted)
			m_text = *m_format_muted;
		else
			m_text = m_format;

		if (m_color_muted && s_source_info.muted)
			m_i3bar["color"] = { .is_string = true, .value = *m_color_muted };
		else
			m_i3bar.erase("color");

		m_value.value = pa_cvolume_to_percentage<double>(&s_source_info.volume);

		return true;
	}

	bool PulseAudioInputBlock::handle_custom_click(const MouseInfo& mouse)
	{
		if (!m_click_to_mute)
			return true;

		if (mouse.type != MouseType::Left)
			return true;

		std::unique_lock lock(s_source_info.mutex);

		auto op = pa_context_set_source_mute_by_index(s_server_info.context, s_source_info.index, !s_source_info.muted, NULL, NULL);
		s_source_info.wait_update_for(lock, 100ms);
		pa_operation_unref(op);

		return true;
	}
	
}
