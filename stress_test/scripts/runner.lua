-- -cxr --allow-io --allow-os --in "$(SolutionDir)stress_test" --out "E:\AxStress"

math.randomseed(0574355)

local tests = {}
local test_names = {}
local test_index = 0
local start_time = 0

local test_count = 0
local scripts = manifest.scripts
table.sort(scripts, function(a, b) return a < b end)
for test_name, _ in pairs(scripts) do
    if test_name:match("^test_") then
        test_count = test_count + 1
        tests[test_count] = ax.import(test_name)
        print("Found test: " .. test_name)
        test_names[test_count] = test_name
    end
end

print("Total tests found: " .. test_count)

function start_next()
    test_index = test_index + 1
    local test = tests[test_index]

    test.on_complete:subscribe(function(tick_time)
        local duration = os.clock() - start_time
        test:teardown()

        log.warn(string.format("Test %d (%s) os time: %.4f seconds", test_index, test_names[test_index], duration))
        log.warn(string.format("Test %d (%s) tick time: %.4f seconds", test_index, test_names[test_index], tick_time))

        app.call_deferred(function()
            check_next()
        end)
    end)

    print("Starting test: " .. test_names[test_index])

    test:setup()
    start_time = os.clock()
    test:run()
end

function check_next()
    if test_index < test_count then
        start_next()
    else
        log.info("All tests completed.")
        app.window.request_close()
    end
end

check_next()

return error_code.Success