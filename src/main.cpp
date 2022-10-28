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

static bool parse_mouse_info(const nlohmann::json& json, bsbar::Block::MouseInfo& out)
{
	if (!json.contains("button") || !json["button"].is_number())
		return false;

	switch ((int)json["button"])
	{
		case 1: out.type = bsbar::Block::MouseType::Left;		break;
		case 2: out.type = bsbar::Block::MouseType::Middle;		break;
		case 3: out.type = bsbar::Block::MouseType::Right;		break;
		case 4: out.type = bsbar::Block::MouseType::ScrollUp;	break;
		case 5: out.type = bsbar::Block::MouseType::ScrollDown;	break;
		default:
			return false;
	}

	if (!json.contains("relative_x") || !json["relative_x"].is_number())
		return false;
	out.pos[0] = json["relative_x"];

	if (!json.contains("relative_y") || !json["relative_y"].is_number())
		return false;
	out.pos[1] = json["relative_y"];

	if (!json.contains("width") || !json["width"].is_number())
		return false;
	out.size[0] = json["width"];

	if (!json.contains("height") || !json["height"].is_number())
		return false;
	out.size[1] = json["height"];

	return true;
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

		std::string name		= json["name"];
		std::string instance	= json["instance"];

		bsbar::Block::MouseInfo mouse;
		if (!parse_mouse_info(json, mouse))
			exit(2);

		bool slider = false;

		if (instance.ends_with("-slider"))
		{
			instance = instance.substr(0, instance.size() - 7);
			slider = true;
		}

		bool res = false;
		for (auto& block : s_blocks)
		{
			if (block->get_name() == name && block->get_instance() == instance)
			{
				switch (mouse.type)
				{
					case bsbar::Block::MouseType::Left:
					case bsbar::Block::MouseType::Middle:
					case bsbar::Block::MouseType::Right:
						res |= slider ? block->handle_slider_click(mouse) : block->handle_click(mouse);
						break;
					case bsbar::Block::MouseType::ScrollDown:
					case bsbar::Block::MouseType::ScrollUp:
						res |= slider ? block->handle_slider_scroll(mouse) : block->handle_scroll(mouse);
						break;
				}
			}
		}
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
			block->update_clock_tick();

		// FIXME: hacky way to wait for updates :D
		std::this_thread::sleep_for(milliseconds(10));
		print_blocks();

		auto tp = system_clock::now();
		tp = ceil<seconds>(tp);
		std::this_thread::sleep_until(tp);
	}
	
	return 0;
}