#pragma once

#include "Block.h"

/*
see https://en.cppreference.com/w/cpp/chrono/c/strftime for formatting
*/

namespace bsbar
{

	class DateTimeBlock : public Block
	{
	public:
		virtual bool custom_update(time_point tp) override;

	};

}