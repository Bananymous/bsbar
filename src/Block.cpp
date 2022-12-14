#include "Block.h"

#include "Common.h"

#include "Battery.h"
#include "Custom.h"
#include "DateTime.h"
#include "Menu.h"
#include "Network.h"
#include "PulseAudio.h"
#include "Temperature.h"

#include <csignal>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>

#define BSBAR_VERIFY_SIGNAL(node)																		\
	if (auto sig = **node.as_integer(); sig < 0 || sig > SIGRTMAX - SIGRTMIN) {							\
		std::cerr << "valid signal values are from " << SIGRTMIN << " to " << SIGRTMAX << std::endl;	\
		std::cerr << "  " << node.source() << std::endl;												\
		exit(1);																						\
	}

namespace bsbar
{

	// If block is false, the child process is left as a zombie
	static pid_t execute_command(const std::string& command)
	{
		pid_t pid = fork();
		if (pid == -1)
		{
			std::cerr << "fork()\n  " << strerror(errno) << std::endl;
			return -1;
		}

		if (pid == 0)
		{
			if (int fd = open("/dev/null", O_WRONLY); fd != -1)
			{
				dup2(fd, STDOUT_FILENO);
				close(fd);
			}

			execl("/bin/sh", "sh", "-c", command.c_str(), (char*)NULL);

			std::cerr << "execl()\n  " << strerror(errno) << std::endl;
			exit(1);
		}

		return pid;
	}

	static std::unordered_map<int, Block*> s_signals;

	static std::optional<Block::Value> node_to_value(toml::node& node)
	{
		Block::Value result;
		std::stringstream ss;
		switch (node.type())
		{
			case toml::node_type::boolean:
				result.is_string = false;
				ss << (*node.value<bool>() ? "true" : "false");
				break;
			case toml::node_type::floating_point:
				result.is_string = false;
				ss << *node.value<double>();
				break;
			case toml::node_type::integer:
				result.is_string = false;
				ss << *node.value<int64_t>();
				break;
			case toml::node_type::string:
				result.is_string = true;
				ss << *node.value<std::string_view>();
				break;
			default:
				return std::nullopt;
		}
		result.value = ss.str();
		return result;
	}

	std::unique_ptr<Block> Block::create(std::string_view name, toml::table& table)
	{
		auto type = table["type"].value<std::string_view>();
		if (!type)
		{
			std::cerr << "key 'type' not defined for module '" << name << '\'' << std::endl;
			std::cerr << "  " << table.source() << std::endl;
			exit(1);
		}

		std::unique_ptr<Block> block = nullptr;

		if (*type == "internal/battery")
			block = std::make_unique<BatteryBlock>();
		else if (*type == "internal/datetime")
			block = std::make_unique<DateTimeBlock>();
		else if (*type == "internal/menu")
			block = std::make_unique<MenuBlock>();
		else if (*type == "internal/network")
			block = std::make_unique<NetworkBlock>();
		else if (*type == "internal/pulseaudio.output")
			block = std::make_unique<PulseAudioBlock>();
		else if (*type == "internal/pulseaudio.input")
			block = std::make_unique<PulseAudioInputBlock>();
		else if (*type == "internal/temperature")
			block = std::make_unique<TemperatureBlock>();
		else if (*type == "custom")
			block = std::make_unique<CustomBlock>();

		if (!block)
		{
			std::cerr << "Unknown value for key 'type' in module '" << name << '\'' << std::endl;
			std::cerr << "  " << table["type"].node()->source() << std::endl;
			exit(1);
		}

		block->m_name = name;
		block->m_type = *type;

		for (auto& [key, node] : table)
		{
			if (node.is_table())
				block->add_subconfig(key, *node.as_table());
			else
				block->add_config(key, node);
		}

		block->custom_config_done();
		if (!block->is_valid())
			exit(1);

		return block;
	}

	bool Block::is_valid() const
	{
		if (m_format.empty())
		{
			std::cerr << "No format specified for module '" << m_name << '\'' << std::endl;
			return false;
		}

		if (m_format.find("%ramp%") != std::string::npos && m_value.ramp.empty())
		{
			std::cerr << "No ramp strings specified for module '" << m_name << "', but %ramp% used in format" << std::endl;
			return false;
		}

		if (!custom_is_valid())
			return false;

		return true;
	}

	void Block::update_thread()
	{		
		while (true)
		{
			auto tp = time_point::clock::now();

			if (custom_update(tp))
			{
				std::scoped_lock _(m_mutex);

				if (m_text.find("%value%") != std::string::npos)
					replace_all(m_text, "%value%", value_to_string(m_value.value, m_value.precision));

				if (m_text.find("%ramp%") != std::string::npos)
					replace_all(m_text, "%ramp%", get_ramp_string(m_value.value, m_value.min, m_value.max, m_value.ramp));

				if (m_color && m_i3bar.find("color") == m_i3bar.end())
					m_i3bar["color"] = { .is_string = true, .value = *m_color };
			}

			{
				std::scoped_lock _(m_mutex);
				m_last_update = tp;
				m_wait_cv.notify_all();
			}

			std::unique_lock lock(m_mutex);
			m_update_cv.wait(lock, [this]() { return m_request_update > m_last_update; });
		}
	}

	bool Block::handles_signal(int sig) const
	{
		return m_signals.find(sig) != m_signals.end();
	}

	void Block::update_clock_tick(time_point tp)
	{
		custom_tick();
		if (m_update_counter++ % m_interval)
			return;
		request_update(false, tp);
	}

	void Block::request_update(bool should_block, time_point tp)
	{
		m_request_update = tp;
		m_update_cv.notify_all();

		if (should_block)
			block_until_updated(tp);
	}

	void Block::wait_if_needed(time_point tp) const
	{
		if (!m_is_needed)
			return;
		block_until_updated(tp);
	}

	void Block::block_until_updated(time_point tp) const
	{
		std::unique_lock lock(m_mutex);
		m_wait_cv.wait(lock, [&]() { return m_last_update >= tp; });
	}

	void Block::print() const
	{
		std::scoped_lock _(m_mutex);

		if (!custom_print())
			return;

		std::printf("{");
		std::printf( "\"name\":\"%s\"",			m_type.c_str());
		std::printf(",\"instance\":\"%s\"",		m_name.c_str());
		std::printf(",\"full_text\":\"%s\"",	m_text.c_str());

		for (const auto& [key, value] : m_i3bar)
		{
			if (value.is_string)
				std::printf(",\"%s\":\"%s\"", key.c_str(), value.value.c_str());
			else
				std::printf(",\"%s\":%s", key.c_str(), value.value.c_str());
		}

		std::printf("}");
	}

	void Block::initialize()
	{
		custom_initialize();
		m_thread = std::thread(&Block::update_thread, this);
	}

	bool Block::handle_click(const MouseInfo& mouse, std::string_view sub)
	{
		if (!handle_custom_click(mouse, sub))
			return false;

		switch (m_on_click.slider_options)
		{
			case SliderOptions::On:
				m_show_slider = true;
				break;
			case SliderOptions::Off:
				m_show_slider = false;
				break;
			case SliderOptions::Toggle:
				m_show_slider = !m_show_slider;
				break;
		}

		if (!m_on_click.command.empty())
		{
			if (!m_on_click.is_running || !m_on_click.single_instance)
			{
				pid_t pid = execute_command(m_on_click.command);

				if (pid == -1)
					return false;

				std::scoped_lock _(m_mutex);
				if (m_on_click.blocking)
					waitpid(pid, NULL, 0);
				else
				{
					m_on_click.is_running = true;
					std::thread temp = std::thread([this, pid]() {
						waitpid(pid, NULL, 0);
						m_on_click.is_running = false;
					});
					temp.detach();
				}
			}
		}

		return true;
	}


	bool Block::handle_scroll(const MouseInfo& mouse, std::string_view sub)
	{
		if (!handle_custom_scroll(mouse, sub))
			return false;

		return true;
	}

	void Block::add_config(std::string_view key, toml::node& value)
	{
		static std::unordered_set<std::string_view> supported_i3bar = {
			"color",
			"background",
			"border",
			"border_top",
			"border_right",
			"border_bottom",
			"border_left",
			"min_width",
			"align",
			"urgent",
			"separator",
			"separator_block_width",
			"markup",
		};

		if (key == "type")
			return;

		bool custom = this->add_custom_config(key, value);
		if (custom)
			return;

		if (key == "interval")
		{
			BSBAR_VERIFY_TYPE(value, integer, key);
			int64_t interval = **value.as_integer();
			if (interval == 0)
				m_interval = UINT64_MAX;
			else if (interval > 0)
				m_interval = interval;
			else
			{
				std::cerr << "Value for key 'interval' must be positive integer or 0 to disable automatic updates" << std::endl;
				std::cerr << "  " << value.source() << std::endl;
				exit(1);
			}
		}
		else if (key == "color")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_color = **value.as_string();
		}
		else if (key == "signal")
		{
			if (value.is_integer())
			{
				BSBAR_VERIFY_SIGNAL(value);
				int sig = **value.as_integer() + SIGRTMIN;
				m_signals.insert(sig);
			}
			else if (value.is_array())
			{
				for (auto& signal : *value.as_array())
				{
					BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(signal, integer, "value for key 'signal' must be a interger or array of integers");
					BSBAR_VERIFY_SIGNAL(signal);
					int sig = **value.as_integer() + SIGRTMIN;
					m_signals.insert(sig);
				}
			}
			else
			{
				std::cerr << "value for key 'signal' must be a interger or array of integers" << std::endl;
				std::cerr << "  " << value.source() << std::endl;
				exit(1);
			}
		}
		else if (key == "format")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_format = **value.as_string();
		}
		else if (key == "needed")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_is_needed = **value.as_boolean();
		}
		else if (key == "ramp")
		{
			BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(value, array, "value for key 'ramp' must be an array of strings");

			m_value.ramp.clear();
			for (auto&& elem : *value.as_array())
			{
				BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(elem, string, "value for key 'ramp' must be an array of strings");
				m_value.ramp.push_back(**elem.as_string());
			}
		}
		else if (key == "value-min")
		{
			BSBAR_VERIFY_TYPE(value, number, key);
			if (value.is_integer())
				m_value.min = **value.as_integer();
			else
				m_value.min = **value.as_floating_point();
		}
		else if (key == "value-max")
		{
			BSBAR_VERIFY_TYPE(value, number, key);
			if (value.is_integer())
				m_value.min = **value.as_integer();
			else
				m_value.max = **value.as_floating_point();
		}
		else if (key == "precision")
		{
			BSBAR_VERIFY_TYPE(value, integer, key);
			m_value.precision = **value.as_integer();
		}
		else if (supported_i3bar.find(key) != supported_i3bar.end())
		{
			auto result = node_to_value(value);
			if (!result)
			{
				std::cerr << "Unsupported value type for key '" << key << '\'' << std::endl;
				std::cerr << "  " << value.source() << std::endl;
				exit(1);
			}
			m_i3bar[std::string(key)] = *result;
		}
		else
		{
			std::cerr << "Unknown key '" << key << "' for module '" << m_name << '\'' << std::endl;
			std::cerr << "  " << value.source() << std::endl;
			exit(1);
		}
	}

	void Block::add_subconfig(std::string_view sub, toml::table& table)
	{
		if (add_custom_subconfig(sub, table))
			return;

		for (auto& [key, value] : table)
		{
			if (sub == "click")
			{
				if (key == "command")
				{
					BSBAR_VERIFY_TYPE(value, string, key);
					m_on_click.command = **value.as_string();
				}
				else if (key == "blocking")
				{
					BSBAR_VERIFY_TYPE(value, boolean, key);
					m_on_click.blocking = **value.as_boolean();
				}
				else if (key == "single-instance")
				{
					BSBAR_VERIFY_TYPE(value, boolean, key);
					m_on_click.single_instance = **value.as_boolean();
				}
				else if (key == "slider-show")
				{
					BSBAR_VERIFY_TYPE(value, string, key);
					
					std::string_view option = *value.value<std::string_view>();
					if (option == "on")
						m_on_click.slider_options = SliderOptions::On;
					else if (option == "off")
						m_on_click.slider_options = SliderOptions::Off;
					else if (option == "toggle")
						m_on_click.slider_options = SliderOptions::Toggle;
					else
					{
						std::cerr << "unrecognized value for key '" << key << "'. valid keys are \"on\", \"off\", \"toggle\"" << std::endl;
						std::cerr << "  " << value.source() << std::endl;
						exit(1);
					}
				}
				else
				{
					std::cerr << "Unknown key '" << key << "' for module '" << m_name << '.' << sub << '\'' << std::endl;
					std::cerr << "  " << value.source() << std::endl;
					exit(1);	
				}
			}
			else
			{
				std::cerr << "Unknown submodule '" << m_name << '.' << sub << '\'' << std::endl;
				std::cerr << "  " << value.source() << std::endl;
				exit(1);
			}
		}
	}

}