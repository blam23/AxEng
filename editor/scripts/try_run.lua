-- requires io permissions

local try_run = {
    done = lua_event.new(),
    launched = lua_event.new()
}

local shared = app.get_or_create_shared("run_in_background_args")
local running = false
local launched = false
local current_name = ""
local last_success = false
local last_error = ""

if (app.on_main_thread()) then
    app.on_signal(shared, "launched", function()
        launched = true
        local pid = shared:get("pid")
        try_run.launched:fire({pid = pid})
    end)
    app.on_signal(shared, "done", function()
        running = false
        last_success = shared:get("success")
        last_error = shared:get("err")
        stdout = shared:get("stdout")
        try_run.done:fire({name = current_name, success = last_success, err = last_error, stdout = stdout})
    end)
end

function try_run.inline(args, shared)

    local cmd = app.current_executable_path()
    for _, arg in ipairs(args) do
        if string.find(arg, " ") then
            arg = "\"" .. arg .. "\""
        end
        cmd = cmd .. " " .. arg
    end
    local handle, pid = io.popen(cmd, "r")
    if shared ~= nil then
        shared:set("pid", pid)
        app.signal(shared, "launched")
    end
    local stdout = handle:read("*a")
    local rc = {handle:close()}

    if not rc[1] then
        local err = tostring(rc[3])
        return false, err, stdout
    else
        return true, nil, stdout
    end
end

function try_run.is_running()
    return running, current_name
end

function try_run.background(name, in_args)
    if running then
        log.warn("App is already running in the background")
        return
    end
    current_name = name
    shared:set("name", current_name)
    shared:set("args", in_args)
    running = true
    bg.run_script("run_app_in_background")
end

return try_run