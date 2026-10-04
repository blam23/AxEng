local class = ax.import("class")

local base_test = {
}
local test_mt = class(base_test)

function base_test:new(suite, name)
    return setmetatable({suite = suite, name = name, failed = false }, test_mt)
end

function base_test:fail(msg)
    self.suite:fail(msg)
    self.failed = true
end

function base_test:assert(condition, msg)
    self.suite:assert(condition, msg)
end

function base_test:setup()
end

function base_test:run()
end

function base_test:teardown()
end

return base_test
