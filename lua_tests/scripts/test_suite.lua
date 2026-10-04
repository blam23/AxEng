local class = ax.import("class")

local test_suite = {
}
local test_mt = class(test_suite)

function test_suite:new(name)
    return setmetatable({
        name = name,
        failed_tests = 0,
        passed_tests = 0,
        tests = {}
    }, test_mt)
end

function test_suite:add_test(test_case)
    table.insert(self.tests, test_case)
    test_case.suite = self
end

function test_suite:fail(msg)
    log.error("[" .. self.name .. "] Test failed: " .. msg)
end

function test_suite:assert(condition, msg)
    if not condition then
        self:fail(msg or "Assertion failed")
    end
end

function test_suite:setup()
end

function test_suite:run()
    for _, test_case in ipairs(self.tests) do
        local ok, err = pcall(test_case.setup, test_case)
        if not ok then log.error("[" .. self.name .. "] Setup error: " .. err); goto continue end
        ok, err = pcall(test_case.run, test_case)
        if not ok then log.error("[" .. self.name .. "] Run error: " .. err); goto continue end
        ok, err = pcall(test_case.teardown, test_case)
        if not ok then log.error("[" .. self.name .. "] Teardown error: " .. err); goto continue end

        if test_case.failed then
            self.failed_tests = self.failed_tests + 1
        else
            self.passed_tests = self.passed_tests + 1
        end

        ::continue::
    end

end

function test_suite:teardown()
end

return test_suite
