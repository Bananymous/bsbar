#pragma once

#include "Block.h"

namespace bsbar
{

	struct ConfigResult
	{
		int64_t thread_pool_size = 5;

		std::vector<std::unique_ptr<Block>> blocks;
	};

	ConfigResult parse_config(std::string_view config_path);

}