-- use spdlog lib to print
local __print = print
print = function(...)
    local arg={...}
    local res = ""
    for i,v in ipairs(arg) do
        res = res .. tostring(v)
    end
    log.info(res)
end

-- todo: improve
keys = {
    w = 87,
    a = 65,
    s = 83,
    d = 68,
}