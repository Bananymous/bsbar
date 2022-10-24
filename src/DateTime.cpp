#include "DateTime.h"

#include <ctime>

namespace bsbar
{

	bool DateTimeBlock::custom_update(time_point tp)
	{
		char buffer[128];

		
		std::time_t t = std::chrono::system_clock::to_time_t(tp);
		if (!std::strftime(buffer, sizeof(buffer), m_format.c_str(), std::localtime(&t)))
			return false;

		std::scoped_lock _(m_mutex);
		
		m_text = buffer;
		return true;
	}

}