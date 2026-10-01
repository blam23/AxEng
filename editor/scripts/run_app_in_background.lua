local try_run = ax.import("try_run")

local shared = app.get_or_create_shared("run_in_background_args")

if (shared:get("running")) then
    log.warn("App is already running in the background")
    return
end

shared:set("running", true)

local success = false
local err = ""
local stdout = ""
success, err, stdout = try_run.inline(shared:get("args"))
shared:set("success", success)
shared:set("err", err)
shared:set("stdout", stdout)

shared:set("running", false)

app.signal(shared, "done")
-- ui.insert_toast(ui.toast_type.Info, 3000, "Background task finished")

app.release_shared(shared)
