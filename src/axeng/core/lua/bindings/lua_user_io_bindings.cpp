#include "axeng/core/lua/bindings/lua_user_io_bindings.h"
#include "axeng/core/application.h"

void ax::lua::bindings::setup_user_io_bindings(ax::Application& app, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };
	auto user_io_table{ state.create_table() };
	user_io_table["open"] =
		[&app](const std::string& filename, const char* mode) -> UserFileHandle*
		{
			return app.m_userFileManager.open_file(filename, mode);
		};
	app_table["user_io"] = user_io_table;

	state.new_usertype<UserFileHandle>
	(
		"UserFileHandle",
		"read_all", &ax::UserFileHandle::read_all,
		"write", &ax::UserFileHandle::write,
		"close", &ax::UserFileHandle::close
	);
}
