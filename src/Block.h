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

		void initialize();

		const std::string& get_name() const		{ return m_type; }
		const std::string& get_instance() const	{ return m_name; }

		void update_clock_tick(time_point tp);
		void request_update(bool should_block, time_point tp = time_point::clock::now());
		void wait_if_needed(time_point tp) const;
		void block_until_updated(time_point tp) const;

		bool handles_signal(int signal) const;

		bool handle_click(const MouseInfo& mouse, std::string_view sub);
		bool handle_scroll(const MouseInfo& mouse, std::string_view sub);

		void add_config(std::string_view key, toml::node& value);
		void add_subconfig(std::string_view sub, toml::table& table);

	protected:
		virtual void custom_initialize() {};

		virtual void custom_tick() {};

		virtual bool custom_is_valid() const { return true; }
		virtual void custom_config_done() {}

		virtual bool custom_update(time_point tp) = 0;
		virtual bool custom_print() const { return true; }

		virtual bool handle_custom_click(const MouseInfo& mouse, std::string_view sub) { return true; }
		virtual bool handle_custom_scroll(const MouseInfo& mouse, std::string_view sub) { return true; }

		virtual bool add_custom_config(std::string_view key, toml::node& value) { return false; }
		virtual bool add_custom_subconfig(std::string_view sub, toml::table& table) { return false; }

	private:
		void update_thread();

	protected:
		std::string									m_type;
		std::string									m_name;
		std::string									m_format;

		std::optional<std::string>					m_color;

		std::atomic<uint64_t>						m_interval			= 1;
		std::atomic<uint64_t>						m_update_counter	= 0;

	public:
		std::unordered_map<std::string, Value>		m_i3bar;

	protected:
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
			std::string			command;
			std::atomic<bool>	blocking		= false;
			std::atomic<bool>	is_running		= false;
			std::atomic<bool>	single_instance	= false;
			SliderOptions		slider_options;
		} m_on_click;
		struct
		{
			std::string		command;
		} m_on_slider_click;
		std::atomic<bool>							m_show_slider = false;

		std::atomic<bool>							m_is_needed			= false;
		time_point									m_last_update		= time_point::clock::now();
		time_point									m_request_update	= time_point::clock::now();
		mutable std::condition_variable				m_wait_cv;
		std::condition_variable						m_update_cv;

		std::thread 								m_thread;
		mutable std::mutex							m_mutex;
	};

}