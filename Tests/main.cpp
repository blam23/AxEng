#include <gtest/gtest.h>
#include "log_capture.h"
#include <memory>

using namespace testlog;

class LogCapturingListener : public testing::EmptyTestEventListener
{
public:
	void OnTestStart(const testing::TestInfo&) override
	{
		LogCapture::instance().start_capture();
	}

	void OnTestEnd(const testing::TestInfo& test_info) override
	{
		auto& cap = LogCapture::instance();
		cap.stop_capture();

		// Check expected error counts; this will record a gtest failure and print/save the
		// log if the expectation does not match actual. It returns false when there
		// was a mismatch.
		const bool mismatch = !cap.check_expected_error_count_and_report(test_info);

		// If the test itself failed and the error-count check didn't already print the
		// logs, print/save them here for visibility.
		if (testing::UnitTest::GetInstance()->current_test_info()->result()->Failed() && !mismatch) {
			const auto lines = cap.get_messages();
			if (!lines.empty()) {
				std::cerr << " ---- Captured log for failed test "
					  << test_info.test_case_name() << "." << test_info.name()
					  << " ----\n";
				for (const auto& l : lines) std::cerr << l << '\n';

				const std::string filename = std::string("test_log_") + test_info.test_case_name() + "_" + test_info.name() + ".log";
				cap.save_to_file(filename);
				std::cerr << "Saved full log to: '" << filename << "'\n";
			}
		}
		cap.clear_messages();
	}
};

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);

	auto& listeners = testing::UnitTest::GetInstance()->listeners();
	listeners.Append(new LogCapturingListener());

	return RUN_ALL_TESTS();
}
