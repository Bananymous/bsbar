#include "Config.h"

#include <nlohmann/json.hpp>

#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

static std::string get_home_directory(char** env)
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

static void signal_handler(int signal)
{
	for (auto& block : s_blocks)
		if (block->handles_signal(signal))
			block->request_update(true);
}

static void handle_clicks()
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

int main(int argc, char** argv, char** env)
{
	std::string config_path = get_home_directory(env) + "/.config/bsbar/config.toml";
	
	if (argc == 2)
		config_path = argv[1];
	if (argc > 2)
		return 1;

	auto config = bsbar::parse_config(config_path);
	s_blocks = std::move(config.blocks);

	// Assing signal handler for all signals between SIGRTMIN and SIGRTMAX
	for (int sig = SIGRTMIN; sig <= SIGRTMAX; sig++)
		std::signal(sig, signal_handler);

	std::thread t = std::thread(handle_clicks);

	std::printf("{\"version\":1,\"click_events\":true}\n[\n");

	while (true)
	{
		using namespace std::chrono;

		for (auto& block : s_blocks)
			block->request_update();

		// FIXME: hacky way to wait for updates :D
		std::this_thread::sleep_for(milliseconds(10));
		print_blocks();

		auto tp = system_clock::now();
		tp = ceil<seconds>(tp);
		std::this_thread::sleep_until(tp);
	}
	
	return 0;
}