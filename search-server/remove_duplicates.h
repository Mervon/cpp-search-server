#pragma once

#include "search_server.h"

bool MyCompare (const std::map<std::string, double>& lhs, const std::map<std::string, double>& rhs);

void RemoveDuplicates(SearchServer& search_server);
