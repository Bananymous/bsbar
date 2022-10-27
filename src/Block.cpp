#include "Block.h"

#include "Common.h"

#include "Battery.h"
#include "Custom.h"
#include "DateTime.h"
#include "Network.h"
#include "Temperature.h"

#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include <unistd.h>

#define BSBAR_VERIFY_SIGNAL(node)																		\
	if (auto sig = **node.as_integer(); sig < SIGRTMIN || sig > SIGRTMAX) {								\
		std::cerr << "valid signal values are from " << SIGRTMIN << " to " << SIGRTMAX << std::endl;	\
		std::cerr << "  " << node.source() << std::endl;												\
		exit(1);																						\
	}

void print_blocks();

namespace bsbar
{

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
			return nullptr;
		}

		std::unique_ptr<Block> block = nullptr;

		if (*type == "internal/datetime")
			block = std::make_unique<DateTimeBlock>();
		else if (*type == "internal/battery")
			block = std::make_unique<BatteryBlock>();
		else if (*type == "internal/temperature")
			block = std::make_unique<TemperatureBlock>();
		else if (*type == "internal/network")
			block = std::make_unique<NetworkBlock>();
		else if (*type == "custom")
			block = std::make_unique<CustomBlock>();	

		if (!block)
		{
			std::cerr << "Unknown value for key 'type' in module '" << name << '\'' << std::endl;
			std::cerr << "  " << table["type"].node()->source() << std::endl;
			return nullptr;
		}

		block->m_name = name;
		block->m_type = *type;

		for (auto& [key, node] : table)
		{
			if (node.is_table())
				for (auto& [sub_key, sub_node] : *node.as_table())
					block->add_subconfig(key, sub_key, sub_node);
			else
				block->add_config(key, node);
		}

		if (!block->is_valid())
			exit(1);

		block->m_thread = std::thread(&Block::update_thread, block.get());

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

		return true;
	}

	void Block::update_thread()
	{
		using namespace std::chrono;

		auto tp = system_clock::now();
		while (true)
		{
			if (m_force_update)
				tp = system_clock::now();

			if (custom_update(tp))
			{
				std::scoped_lock _(m_mutex);

				if (m_text.find("%value%") != std::string::npos)
					replace_all(m_text, "%value%", value_to_string(m_value.value, m_value.precision));

				if (m_text.find("%ramp%") != std::string::npos)
					replace_all(m_text, "%ramp%", get_ramp_string(m_value.value, m_value.min, m_value.max, m_value.ramp));
			}

			m_force_update = false;
			m_update_done_cv.notify_all();

			tp = ceil<seconds>(system_clock::now());
			std::unique_lock lock(m_mutex);
			m_update_cv.wait_until(lock, tp, [this]() { return m_force_update.load(); });
		}
	}

	void Block::signal_dispatcher(int sig)
	{
		static auto update_fn = [](Block* block)
		{
			block->m_force_update = true;
			block->m_update_cv.notify_one();

			std::unique_lock lock(block->m_mutex);
			block->m_update_done_cv.wait(lock, [block]() { return !block->m_force_update; });
			
			print_blocks();
		};

		if (auto it = s_signals.find(sig); it != s_signals.end())
			if (!it->second->m_force_update)
				update_fn(it->second);
	}

	void Block::print() const
	{
		std::scoped_lock _(m_mutex);

		std::printf("{");
		std::printf( "\"name\":\"%s\"",			m_type.c_str());
		std::printf(",\"instance\":\"%s\"",		m_name.c_str());
		std::printf(",\"full_text\":\"%s\"",	m_text.c_str());

		for (auto& [key, value] : m_i3bar)
		{
			if (value.is_string)
				std::printf(",\"%s\":\"%s\"", key.c_str(), value.value.c_str());
			else
				std::printf(",\"%s\":%s", key.c_str(), value.value.c_str());
		}

		std::printf("}");
	}

	bool Block::handle_click(nlohmann::json& json)
	{
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

		if (m_on_click.command.empty())
			return true;

		if (fork() == 0)
		{
			close(STDOUT_FILENO);
			execl("/bin/sh", "sh", "-c", m_on_click.command.c_str(), NULL);
		}
		return true;
	}

	bool Block::handle_slider_click(nlohmann::json& json)
	{
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
		if (supported_i3bar.find(key) != supported_i3bar.end())
		{
			auto result = node_to_value(value);
			if (!result)
			{
				std::cerr << "Unsupported value type for key '" << key << '\'' << std::endl;
				std::cerr << "  " << value.source() << std::endl;
				exit(1);
			}
			m_i3bar[std::string(key)] = *result;
			return;
		}

		if (key == "type")
			return;

		bool custom = this->add_custom_config(key, value);
		if (custom)
			return;

		if (key == "interval")
		{
			BSBAR_VERIFY_TYPE(value, integer, key);
			m_interval = **value.as_integer();
		}
		else if (key == "signal")
		{
			if (value.is_integer())
			{
				BSBAR_VERIFY_SIGNAL(value);

				int sig = **value.as_integer();
				std::signal(sig, &Block::signal_dispatcher);
				s_signals[sig] = this;
			}
			else if (value.is_array())
			{
				for (auto& signal : *value.as_array())
				{
					BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(signal, integer, "value for key 'signal' must be a interger or array of integers");
					BSBAR_VERIFY_SIGNAL(signal);

					int sig = **value.as_integer();
					std::signal(sig, &Block::signal_dispatcher);
					s_signals[sig] = this;
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
			m_value.min = **value.as_floating_point();
		}
		else if (key == "value-max")
		{
			BSBAR_VERIFY_TYPE(value, number, key);
			m_value.max = **value.as_floating_point();
		}
		else if (key == "precision")
		{
			BSBAR_VERIFY_TYPE(value, integer, key);
			m_value.precision = **value.as_integer();
		}
		else
		{
			std::cerr << "Unknown key '" << key << "' for module '" << m_name << '\'' << std::endl;
			std::cerr << "  " << value.source() << std::endl;
			exit(1);
		}
	}

	void Block::add_subconfig(std::string_view sub, std::string_view key, toml::node& value)
	{
		if (sub == "click")
		{
			if (key == "command")
			{
				BSBAR_VERIFY_TYPE(value, string, key);
				m_on_click.command = '"' + **value.as_string() + '"';
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
			std::cerr << "Unknown submodule '" << m_name << '.' << sub << std::endl;
			std::cerr << "  " << value.source() << std::endl;
			exit(1);
		}
	}

}