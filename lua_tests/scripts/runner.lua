math.randomseed(7357)

local test_suites = {}
local test_count = 0

local scripts = manifest.scripts
table.sort(scripts, function(a, b) return a < b end)
for test_name, _ in pairs(scripts) do
    if test_name:match("^test_") then
        test_count = test_count + 1
        test_suites[test_count] = ax.import(test_name)
        print("Importing test suite: " .. test_name)
    end
end

print("Found [" .. test_count .. "] test suites")

function test_all()
    local failed = 0
    local passed = 0
    local test_index = 1

    while test_index <= test_count do
        local test_suite = test_suites[test_index]
        local test_suite_name = test_suite.name or "<unnamed>"

        log.info("--> Running test suite: " .. test_suite_name)

        local setup_success, setup_err = nil, nil
        local run_success, run_err = nil, nil
        local teardown_success, teardown_err = nil, nil

        setup_success, setup_err = pcall(function() test_suite:setup() end)

        if not setup_success then
            log.error("--> [X]  Test suite setup failed: " .. test_suite_name .. " Error: " .. tostring(setup_err))
            failed = failed  + #test_suite.tests
            goto continue
        end

        run_success, run_err = pcall(function() test_suite:run() end)
        if not run_success then
            log.error("--> [X]  Test suite run error: " .. test_suite_name .. " Error: " .. tostring(run_err))
            failed = failed + #test_suite.tests
            goto continue
        end

        teardown_success, teardown_err = pcall(function() test_suite:teardown() end)
        if not teardown_success then
            log.error("--> [X]  Test suite teardown failed: " .. test_suite_name .. " Error: " .. tostring(teardown_err))
            failed = failed + #test_suite.tests
            goto continue
        end

        if (test_suite.failed_tests > 0) then
            log.error("--> [X]  Test suite had failures: " .. test_suite_name)
            failed = failed + test_suite.failed_tests
        else
            log.info("--> [✓]  Test suite passed: " .. test_suite_name)
            passed = passed + test_suite.passed_tests
        end

        ::continue::
        test_index = test_index + 1
    end

    if failed > 0 then
        log.error("Some tests failed. Passed: " .. passed .. ", Failed: " .. failed)
    else
        log.info("All tests completed. Passed: " .. passed .. ", Failed: " .. failed)
    end

    if failed > 0 then
        return error_code.GenericFailure
    else
        return error_code.Success
    end
end

return test_all()