#pragma once

#include <string>

std::string makePasswordHash(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& storedHash);
