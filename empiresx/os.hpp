#pragma once

#include <string>

class OS final {
public:
	/**
	 * The UTF-8 representations for computer system name and current logged in username.
	 * Linux provides UTF-8 automagically, but on windows, it is converted from UTF16-LE.
	 */
	std::string compname, username;

	OS();
};