#define TOML_IMPLEMENTATION
#include "Config.h"

#include "Common.h"

#include <iostream>

namespace bsbar
{

	ConfigResult parse_config(std::string_view config_path)
	{
		auto parse_result = toml::parse_file(config_path);
		if (!parse_result)
		{
			std::cerr << "Error while parsing file:" << std::endl;
			std::cerr << parse_result.error() << std::endl;
			return {};
		}

		auto config = std::move(parse_result).table();

		auto order_or_error = config["order"];
		if (!order_or_error)
		{
			std::cerr << "global key 'order' not defined" << std::endl;
			exit(1);
		}
		
		auto& order = *order_or_error.node();
		BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(order, array, "value for key 'order' must be an array of strings");

		ConfigResult config_result;

		auto thread_pool_size = config["thread-pool-size"];
		if (thread_pool_size)
		{
			BSBAR_VERIFY_TYPE((*thread_pool_size.node()), integer, "thread-pool-size");
			config_result.thread_pool_size = thread_pool_size.as_integer()->get();
			if (config_result.thread_pool_size < 1)
			{
				std::cerr << "value for global key 'thread-pool-size' must be a positive integer" << std::endl;
				std::cerr << "  " << thread_pool_size.node()->source() << std::endl;
				exit(1);
			}
		}

		for (auto& node : *order_or_error.as_array())
		{
			BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(node, string, "value for key 'order' must be an array of strings");

			auto module = *node.value<std::string_view>();

			auto module_config = config[module];
			if (!module_config || !module_config.is_table())
			{
				std::cerr << "key 'order' uses module '" << module << "', but it is not found from config" << std::endl;
				exit(1);
			}

			auto block = Block::create(module, *module_config.as_table());
			config_result.blocks.push_back(std::move(block));
		}

		return config_result;
	}

}