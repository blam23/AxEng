--
-- Change some built-ins
--

-- use spdlog lib to print
local __print = print
print = function(...)
    local arg={...}
    local res = "<Lua> "
    for i,v in ipairs(arg) do
        if v == nil then
            v = "nil"
        end
        res = res .. tostring(v)
    end
    log.info(res)
end

-- disable require
local __require = require
require = function()
    log.error("Please use import instead.")
end
