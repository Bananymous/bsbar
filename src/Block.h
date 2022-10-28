#pragma once

#include "toml_include.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <thread>

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

		enum class MouseType {
			Left, Right, Middle,
			ScrollUp, ScrollDown,
		};

		struct MouseInfo {
			MouseType	type;
			int			pos[2];
			int			size[2];
		};

	public:
		static std::unique_ptr<Block> create(std::string_view name, toml::table& table);
		bool is_valid() const;

		void print() const;

		std::string_view get_name() const		{ return m_type; }
		std::string_view get_instance() const	{ return m_name; }

		void update_clock_tick();
		void request_update(bool print = false);

		bool handles_signal(int signal) const;

		bool handle_click(const MouseInfo& mouse);
		bool handle_scroll(const MouseInfo& mouse);
		bool handle_slider_click(const MouseInfo& mouse);
		bool handle_slider_scroll(const MouseInfo& mouse);

	protected:
		virtual bool custom_update(time_point tp) = 0;

		virtual bool handle_custom_click(const MouseInfo& mouse) { return true; }
		virtual bool handle_custom_scroll(const MouseInfo& mouse) { return true; }
		virtual bool handle_custom_slider_click(const MouseInfo& mouse) { return true; }
		virtual bool handle_custom_slider_scroll(const MouseInfo& mouse) { return true; }

		virtual bool add_custom_config(std::string_view key, toml::node& value) { return false; }
		virtual bool add_custom_subconfig(std::string_view sub, std::string_view key, toml::node& value) { return false; }
		void add_config(std::string_view key, toml::node& value);
		void add_subconfig(std::string_view sub, std::string_view key, toml::node& value);

	private:
		void update_thread();

	protected:
		std::string									m_type;
		std::string									m_name;
		std::string									m_format;

		std::atomic<int64_t>						m_interval			= 1;
		std::atomic<int64_t>						m_update_counter	= 0;

		std::unordered_map<std::string, Value>		m_i3bar;

		std::string									m_text;

		std::unordered_set<int>						m_signals;

		struct
		{
			double						min			=   0.0;
			double						max			= 100.0;
			double						value		=   0.0;
			int							precision	=   0;
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

		std::atomic<bool>							m_print		= false;
		std::atomic<bool>							m_update	= false;
		std::condition_variable						m_update_cv;
		std::thread 								m_thread;
		mutable std::mutex							m_mutex;
	};

}