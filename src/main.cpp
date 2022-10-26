#include "Config.h"

#include "ThreadPool.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <thread>

#include <csignal>

std::string get_home_directory(char** env)
{
	char** current = env;
	while (*current)
	{
		if (strncmp(*current, "HOME", 4) == 0)
			return *current + 5;
		current++;
	}
	return "";
}

template<typename Rep = double, typename Period = std::milli>
struct ScopeTimer
{
public:
	ScopeTimer() :
		m_start(std::chrono::steady_clock::now())
	{}
	~ScopeTimer()
	{
		auto end = std::chrono::steady_clock::now();
		std::cerr << std::chrono::duration<Rep, Period>(end - m_start).count() << std::endl;
	}

private:
	std::chrono::steady_clock::time_point m_start;
};


// Should be thread safe, since vector's size and block pointers don't change
// Blocks are thread safe also!
static std::vector<std::unique_ptr<bsbar::Block>> s_blocks;


void print_blocks()
{
	static std::mutex print_mutex;

	std::scoped_lock _(print_mutex);

	static bool first_bar = true;

	if (!first_bar)
		std::printf(",");

	bool first_block = true;
	std::printf("[");
	for (const auto& block : s_blocks)
	{
		if (!first_block)
			printf(",");
		block->print();
		first_block = false;
	}
	std::printf("]\n");
	std::fflush(stdout);

	first_bar = false;
}

void handle_clicks()
{
	std::string line;
	std::getline(std::cin, line);

	while (true)
	{
		std::getline(std::cin, line);
		std::string_view line_sv = line;
		if (line_sv.front() == ',')
			line_sv = line_sv.substr(1);

		auto json = nlohmann::json::parse(line_sv);

		if (!json.contains("name") || !json.contains("instance"))
			continue;
		if (!json["name"].is_string() || !json["instance"].is_string())
			continue;

		std::string name		= json["name"].get<std::string>();
		std::string instance	= json["instance"].get<std::string>();
		bool slider = false;

		if (instance.ends_with("-slider"))
		{
			instance = instance.substr(0, instance.size() - 7);
			slider = true;
		}

		bool res = false;
		for (auto& block : s_blocks)
			if (block->get_name() == name && block->get_instance() == instance)
				res |= slider ? block->handle_slider_click(json) : block->handle_click(json);
		if (res) print_blocks();
	}
}

void signal_callback(int signal)
{
	// FIXME: thread pool?
	auto tp = std::chrono::system_clock::now();
	for (auto& block : s_blocks)
		if (block->handles_signal(signal))
			block->update(tp, true);
}

int main(int argc, char** argv, char** env)
{
	std::string config_path = get_home_directory(env) + "/.config/bsbar/config.toml";
	
	if (argc == 2)
		config_path = argv[1];
	if (argc > 2)
		return 1;

	auto config = bsbar::parse_config(config_path);
	s_blocks = std::move(config.blocks);

	for (int sig = SIGRTMIN; sig <= SIGRTMAX; sig++)
		std::signal(sig, signal_callback);

	bsbar::ThreadPool tp(config.thread_pool_size);
	std::thread t = std::thread(handle_clicks);

	std::printf("{\"version\":1,\"click_events\":true}\n[\n");

	auto update_time = std::chrono::system_clock::now();
	update_time = std::chrono::floor<std::chrono::seconds>(update_time);

	while (true)
	{
		{
			//ScopeTimer _;
			for (auto& block : s_blocks)
				tp.push_task([&block, update_time]() { block->update(update_time); });
			tp.wait_for(std::chrono::milliseconds(100));
		}

		print_blocks();

		update_time += std::chrono::seconds(1);
		std::this_thread::sleep_until(update_time);
	}

	return 0;
}