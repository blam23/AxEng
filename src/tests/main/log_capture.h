#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <gtest/gtest.h>
#include <iostream>

namespace testlog
{

class TestLogSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
	TestLogSink() = default;

	void sink_it_(const spdlog::details::log_msg& msg) override;
	void flush_() override;

	void reset_total_error_count();
	std::vector<std::string> get_messages();
	void clear();
	void save_to_file(const std::string& path);
	size_t count_error_messages();

private:
	// Use base_sink's protected mutex_ for synchronization with sink_it_.
	std::vector<std::string> m_messages;
	size_t m_total_error_count{ 0 };
};

class LogCapture
{
public:
	static LogCapture& instance();

	void start_capture();
	void stop_capture();

	std::vector<std::string> get_messages();
	bool has_message_containing(const std::string& needle);
	void clear_messages();
	void save_to_file(const std::string& path);
	size_t expected_error_count() const { return m_expected_error_count; }

	bool check_expected_error_count_and_report(const testing::TestInfo& info);

	void expect_total_error_count(size_t count);

private:
	LogCapture();
	~LogCapture();

	std::shared_ptr<TestLogSink> m_sink;
	std::shared_ptr<spdlog::logger> m_previous_logger;
	bool m_capturing{ false };
	size_t m_expected_error_count{ 0 };
};

} // namespace testlog

#define TESTLOG_MAKE_FILENAME() \
	(std::string("test_log_") + ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name() + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".log")

#define ASSERT_LOG_CONTAINS(needle)                                                          \
	do {                                                                                     \
		const auto _found{ testlog::LogCapture::instance().has_message_containing(needle) }; \
		ASSERT_TRUE(_found) << "Expected log to contain: " << (needle);                      \
	} while (0)

#define EXPECT_LOG_CONTAINS(needle)                                                          \
	do {                                                                                     \
		const auto _found{ testlog::LogCapture::instance().has_message_containing(needle) }; \
		EXPECT_TRUE(_found) << "Expected log to contain: " << (needle);                      \
	} while (0)

#define ASSERT_LOG_NOT_CONTAINS(needle)                                                      \
	do {                                                                                     \
		const auto _found{ testlog::LogCapture::instance().has_message_containing(needle) }; \
		ASSERT_FALSE(_found) << "Expected log to NOT contain: " << (needle);                 \
	} while (0)

#define EXPECT_LOG_NOT_CONTAINS(needle)                                                      \
	do {                                                                                     \
		const auto _found{ testlog::LogCapture::instance().has_message_containing(needle) }; \
		EXPECT_FALSE(_found) << "Expected log to NOT contain: " << (needle);                 \
	} while (0)
