#include "Network.h"

#include "Common.h"

#include <iostream>
#include <sstream>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/types.h>

namespace bsbar
{

	struct InterfaceInfo
	{
		std::string ipv4;
		std::string ipv6;
	};

	static std::optional<std::unordered_map<std::string, InterfaceInfo>> get_interface_ips()
	{
		ifaddrs* p_ifaddrs = nullptr;

		if (getifaddrs(&p_ifaddrs) == -1)
			return {};

		std::unordered_map<std::string, InterfaceInfo> result;
		for (auto ptr = p_ifaddrs; ptr; ptr = ptr->ifa_next)
		{
			if (ptr->ifa_addr->sa_family == AF_INET)
			{
				in_addr* addr = &((sockaddr_in*)ptr->ifa_addr)->sin_addr;
				char buffer[INET_ADDRSTRLEN];
				if (!inet_ntop(AF_INET, addr, buffer, INET_ADDRSTRLEN))
					continue;
				result[ptr->ifa_name].ipv4 = buffer;
			}
			else if (ptr->ifa_addr->sa_family == AF_INET6)
			{
				in6_addr* addr = &((sockaddr_in6*)ptr->ifa_addr)->sin6_addr;
				char buffer[INET6_ADDRSTRLEN];
				if (!inet_ntop(AF_INET6, addr, buffer, INET6_ADDRSTRLEN))
					continue;
				result[ptr->ifa_name].ipv6 = buffer;
			}
		}

		freeifaddrs(p_ifaddrs);

		return result;
	}

	static int get_rssi(std::string_view interface)
	{
		char buffer[128];
		if (snprintf(buffer, sizeof(buffer), "iw %.*s station dump", interface.size(), interface.data()) < 0)
			return 1;

		int rssi = 1;

		FILE* fp = popen(buffer, "r");
		while (fgets(buffer, sizeof(buffer), fp))
		{
			auto splitted = split(buffer, isspace);
			if (splitted.size() != 4 || splitted[0] != "signal" || splitted[1] != "avg:")
				continue;
			if (!string_to_value(splitted[2], rssi))
				rssi = 1;
		}

		pclose(fp);

		return rssi;
	}

	bool NetworkBlock::add_custom_config(std::string_view key, toml::node& value)
	{
		if (key == "interface")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_interface = **value.as_string();
			return true;
		}

		if (key == "format-disconnected")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_format_disconnected = **value.as_string();
			return true;
		}

		if (key == "color-auto")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_color_auto = **value.as_boolean();
			return true;
		}

		return false;
	}

	bool NetworkBlock::custom_update(time_point)
	{
		auto ips = get_interface_ips();
		if (!ips)
			return false;

		std::string color;
		if (m_color_auto)
		{
			if (int rssi = get_rssi(m_interface); rssi != 1)
			{
				int r = std::clamp<int>(map<double>(rssi,    0, -50, 0x00, 0xff), 0x00, 0xff);
				int g = std::clamp<int>(map<double>(rssi, -100, -50, 0x00, 0xff), 0x00, 0xff);
				int b = 0;

				std::stringstream ss;
				ss << std::hex << '#' << r << g << b;
				color = ss.str();
			}
		}

		std::scoped_lock _(m_mutex);

		if (!color.empty())
			m_i3bar["color"] = { .is_string = true, .value = color };

		if (auto it = ips->find(m_interface); it != ips->end() && !it->second.ipv4.empty())
		{
			auto& interface = it->second;
			m_text = m_format;
			replace_all(m_text, "%ipv4%", interface.ipv4);
			replace_all(m_text, "%ipv6%", interface.ipv6);
		}
		else
		{
			if (m_color_auto)
				m_i3bar.erase("color");
			if (m_format_disconnected)
				m_text = *m_format_disconnected;
		}

		return true;
	}

}