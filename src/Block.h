#pragma once

#include "toml_include.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace bsbar
{

	class Block
	{
	public:
		using time_point = std::chrono::system_clock::time_point;

		struct Value {
			bool		is_string;
			std::string	value;
		};

	public:
		static std::unique_ptr<Block> create(std::string_view name, toml::table& table);
		bool is_valid() const;

		bool update(time_point tp, bool force = false);
		void print() const;

		std::string_view get_name() const		{ return m_type; }
		std::string_view get_instance() const	{ return m_name; }

		bool handles_signal(int signal);

		bool handle_click(nlohmann::json& json);
		bool handle_slider_click(nlohmann::json& json);

	protected:
		virtual bool custom_update(time_point tp) = 0;

		virtual bool add_custom_config(std::string_view key, toml::node& value) { return false; }
		virtual bool add_custom_subconfig(std::string_view sub, std::string_view key, toml::node& value) { return false; }
		void add_config(std::string_view key, toml::node& value);
		void add_subconfig(std::string_view sub, std::string_view key, toml::node& value);

	protected:
		std::string									m_type;
		std::string									m_name;
		std::string									m_format;

		std::atomic<int64_t>						m_interval			= 1;
		std::atomic<int64_t>						m_update_counter	= 0;

		std::unordered_map<std::string, Value>		m_i3bar;

		std::unordered_set<int>						m_signals;

		struct
		{
			double						min			=   0.0;
			double						max			= 100.0;
			double						value		=   0.0;
			int							percision	=   0;
			std::vector<std::string>	ramp;
		} m_value;

		enum class SliderOptions { None, On, Off, Toggle };
		struct
		{
			std::string		command;
			SliderOptions	slider_options;
		} m_on_click;
		struct
		{
			std::string		command;
		} m_on_slider_click;
		std::atomic<bool>							m_show_slider = false;

		std::string									m_text;
		mutable std::recursive_mutex				m_mutex;
	};

}