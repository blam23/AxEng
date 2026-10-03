local class = ax.import("class")

local test = {
    
}
local test_mt = class(test)

function test:new()
    return setmetatable({ on_complete = lua_event.new() }, test_mt)
end

function test:setup()
end

function test:run()
end 

function test:teardown()
end

return test
