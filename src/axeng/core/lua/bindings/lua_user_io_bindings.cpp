#include "axeng/core/lua/bindings/lua_user_io_bindings.h"
#include "axeng/core/application.h"

#include <memory>

void ax::lua::bindings::setup_user_io_bindings(ax::Application& app, sol::state& state, bool mainThread)
{
	auto app_table{ state["app"].get<sol::table>() };
	auto user_io_table{ state.create_table() };
	user_io_table["open"] =
		[&app](const std::string& filename, const char* mode) -> UserFileHandle*
		{
			return app.m_userFileManager.open_file(filename, mode);
		};
	user_io_table["remove"] =
		[&app](const std::string& filename) -> bool
		{
			return app.m_userFileManager.remove_file(filename);
		};
	user_io_table["read_zip_entry"] =
		[&app](const std::string& zip_filename, const std::string& entry_filename) -> std::optional<std::string>
		{
			return app.m_userFileManager.read_zip_entry(zip_filename, entry_filename);
		};

	if (mainThread && app.has_window())
	{
		user_io_table["zip_async"] =
			[&app](
				const sol::table& files_table,
				const std::string& output_filename,
				sol::protected_function callback,
				sol::optional<bool> remove_sources) -> bool
			{
				if (!callback.valid())
					return false;

				std::vector<std::string> filenames;
				const auto count{ files_table.size() };
				if (count == 0)
					return false;
				filenames.reserve(count);
				for (std::size_t i = 1; i <= count; ++i)
				{
					const auto entry{ files_table.get<sol::object>(i) };
					if (!entry.is<std::string>())
						return false;
					filenames.push_back(entry.as<std::string>());
				}

				auto failure_message{ std::make_shared<std::string>() };
				auto callback_handle{ std::make_shared<sol::protected_function>(std::move(callback)) };
				auto* task{ app.m_backgroundWorker.create() };
				task->set_task
				(
					[&app, filenames = std::move(filenames), output_filename,
					 remove_sources = remove_sources.value_or(false), failure_message]()
					{
						const auto result{ app.m_userFileManager.create_zip(filenames, output_filename, remove_sources) };
						if (!result)
						{
							*failure_message = result.error();
							return ax::Error::IO;
						}
						return ax::Error::Success;
					}
				);

				auto dispatch{ [&app, callback_handle](bool success, std::string message)
				{
					app.call_deferred
					(
						[callback_handle, success, message = std::move(message)]()
						{
							const auto result{ (*callback_handle)(success, message) };
							if (!result.valid())
							{
								const sol::error error = result;
								spdlog::error("Failed to run ZIP completion callback: {}", error.what());
							}
						}
					);
				} };
				task->set_success
				(
					[dispatch, output_filename](std::unique_ptr<ax::BackgroundTaskResult>)
					{
						dispatch(true, output_filename);
					}
				);
				task->set_failure
				(
					[dispatch, failure_message](std::unique_ptr<ax::BackgroundTaskResult>)
					{
						dispatch(false, failure_message->empty() ? "ZIP task failed" : *failure_message);
					}
				);
				app.m_backgroundWorker.enqueue(task);
				return true;
			};
	}

	app_table["user_io"] = user_io_table;

	state.new_usertype<UserFileHandle>
	(
		"UserFileHandle",
		"read_all", &ax::UserFileHandle::read_all,
		"write", &ax::UserFileHandle::write,
		"close", &ax::UserFileHandle::close
	);
}
