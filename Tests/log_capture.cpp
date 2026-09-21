#include "log_capture.h"
#include <spdlog/pattern_formatter.h>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace testlog
{

void TestLogSink::sink_it_(const spdlog::details::log_msg& msg)
{
	// base_sink already locks the mutex for us
	std::string payload(msg.payload.data(), msg.payload.size());
	const auto lvl = spdlog::level::to_string_view(msg.level);
	const std::string lvlstr(lvl.data(), lvl.size());
	m_messages.emplace_back(std::string("[") + lvlstr + "] " + payload);

	if (msg.level >= spdlog::level::err)
		++m_total_error_count;
}

void TestLogSink::flush_()
{
	// nothing to flush for in-memory sink
}

void TestLogSink::reset_total_error_count()
{
	m_total_error_count = 0;
}

std::vector<std::string> TestLogSink::get_messages()
{
	std::lock_guard<std::mutex> lk(mutex_);
	return m_messages;
}

void TestLogSink::clear()
{
	m_messages.clear();
}

void TestLogSink::save_to_file(const std::string& path)
{
	std::ofstream out(path, std::ios::binary);
	for (const auto& l : m_messages) out << l << "\n";
}

LogCapture& LogCapture::instance()
{
	static LogCapture inst;
	return inst;
}

LogCapture::LogCapture()
{
	m_sink = std::make_shared<TestLogSink>();
	m_sink->set_formatter(std::make_unique<spdlog::pattern_formatter>("[%l] %v"));
}

LogCapture::~LogCapture()
{
	if (m_capturing) stop_capture();
}

void LogCapture::start_capture()
{
	if (m_capturing) return;
	m_previous_logger = spdlog::default_logger();

	auto logger = std::make_shared<spdlog::logger>("test_capture_logger", m_sink);
	logger->set_level(spdlog::level::trace);
	spdlog::set_default_logger(logger);
	m_capturing = true;
}

void LogCapture::stop_capture()
{
	if (!m_capturing)
		return;
	if (m_previous_logger) 
		spdlog::set_default_logger(m_previous_logger);

	m_previous_logger.reset();
	m_capturing = false;
}

std::vector<std::string> LogCapture::get_messages()
{
	return m_sink->get_messages();
}

bool LogCapture::has_message_containing(const std::string& needle)
{
	auto msgs = m_sink->get_messages();

	for (const auto& m : msgs) 
		if (m.find(needle) != std::string::npos) 
			return true;

	return false;
}

void LogCapture::clear_messages()
{
	m_sink->clear();
}

void LogCapture::save_to_file(const std::string& path)
{
	m_sink->save_to_file(path);
}

void LogCapture::expect_total_error_count(size_t count)
{
	m_expected_error_count = count;
}

size_t TestLogSink::count_error_messages()
{
	std::lock_guard<std::mutex> lk(mutex_);
	return m_total_error_count;
}

bool LogCapture::check_expected_error_count_and_report(const testing::TestInfo& info)
{
	const auto actual = m_sink->count_error_messages();
	if (actual != m_expected_error_count) {
		const auto lines = m_sink->get_messages();
		if (!lines.empty()) {
			std::cerr << "---- Captured log for test " << info.test_case_name() << "." << info.name() << " ----\n";
			for (const auto& l : lines) std::cerr << l << '\n';
			const std::string filename = std::string("test_log_") + info.test_case_name() + "_" + info.name() + ".log";
			m_sink->save_to_file(filename);
			std::cerr << "Saved full log to: " << filename << "\n";
		}

		ADD_FAILURE() << "Expected error count " << m_expected_error_count << " but found " << actual;
		m_expected_error_count = 0;
		m_sink->reset_total_error_count();

		return false;
	}

	m_expected_error_count = 0;
	m_sink->reset_total_error_count();
	
	return true;
}

} // namespace testlog
