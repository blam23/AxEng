#include "error.h"
#include "error_def.h"

#define ERR(n, i) { ax::Error::n, #n },
const std::map<ax::Error, std::string> s_error_name_map
{
	ERRORS
};
#undef ERR

std::string_view ax::get_error_name(Error e)
{
	auto it = s_error_name_map.find(e);
	if (it != s_error_name_map.end())
		return std::string_view(it->second);
	return "UnknownError";
}

#undef ERRORS
