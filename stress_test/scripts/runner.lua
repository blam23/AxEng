-- -cxr --allow-io --allow-os --in "$(SolutionDir)stress_test" --out "E:\AxStress"

-- Timings from a debug engine build are meaningless (and the tests take a very long time), so refuse to run.
if app.engine_build.debug then
    log.error("Stress tests require a release build of the engine, but this is a '" .. app.engine_build.mode .. "' build. "
        .. "Rebuild with 'xmake f -m release' then 'xmake'.")
    return error_code.InvalidConfiguration
end

math.randomseed(0574355)

local tests = {}
local test_names = {}
local test_index = 0
local start_time = 0

local scripts = manifest.scripts
for test_name, _ in pairs(scripts) do
    if test_name:match("^test_") then
        table.insert(test_names, test_name)
    end
end

table.sort(test_names)
for _, test_name in ipairs(test_names) do
    table.insert(tests, ax.import(test_name))
    tests[#tests].name = test_name
    print("Found test: " .. test_name)
end
local test_count = #tests

print("Total tests found: " .. test_count)

local image_failures = 0
local pending_image_mismatch = nil

app.window.on_frame_image.subscribe(function(success, compared, matches, differing_pixels, pixel_count, max_difference, message, image_data)
    local test_name = tests[test_index].name
    if not success then
        image_failures = image_failures + 1
        log.error(string.format("Test %d (%s) frame image failed: %s", test_index, test_name, message))
        app.call_deferred(check_next)
    elseif not compared then
        log.info(string.format("Test %d (%s) reference image captured.", test_index, test_name))
        app.call_deferred(check_next)
    elseif matches then
        log.info(string.format("Test %d (%s) reference image matched (%d/%d pixels differ; max channel delta %d).",
            test_index, test_name, differing_pixels, pixel_count, max_difference))
        app.call_deferred(check_next)
    else
        image_failures = image_failures + 1
        log.error(string.format("Test %d (%s) reference image mismatch: %s", test_index, test_name, message))

        local actual_texture_name = "stress_actual_" .. test_name
        local actual_texture = { valid = false }
        local image_error = nil
        if image_data and #image_data > 0 then
            actual_texture = app.res.create_texture(actual_texture_name, image_data)
            if not actual_texture.valid then
                image_error = "The captured frame could not be loaded as a texture."
                log.error(image_error)
            end
        else
            image_error = "The captured frame preview was not available."
            log.error(image_error)
        end

        pending_image_mismatch = {
            test_name = test_name,
            message = message,
            reference = app.res.get_texture("reference_" .. test_name),
            actual = actual_texture,
            actual_texture_name = actual_texture_name,
            image_error = image_error,
        }
    end

end)

local function show_image_mismatch()
    local mismatch = pending_image_mismatch
    if not mismatch then
        return
    end

    ui.begin_window("Reference image mismatch: " .. mismatch.test_name, 1280, 820)
    ui.text(string.format("Test %d (%s)", test_index, mismatch.test_name))
    ui.text(mismatch.message)

    if mismatch.reference.valid and mismatch.actual.valid then
        ui.text("Reference")
        ui.same_line()
        ui.text("Actual")
        ui.image(mismatch.reference, 560, 315)
        ui.same_line()
        ui.image(mismatch.actual, 560, 315)
    elseif mismatch.image_error then
        ui.text_color(1, 0.2, 0.2, 1, mismatch.image_error)
    end

    if mismatch.update_error then
        ui.text_color(1, 0.2, 0.2, 1, mismatch.update_error)
    end

    if mismatch.actual.valid and type(app.res.save_texture_io) == "function"
        and app.project_source_directory then
        if ui.button("Update reference") then
            local path = app.project_source_directory .. "/assets/reference/" .. mismatch.test_name .. ".png"
            if app.res.save_texture_io(mismatch.actual_texture_name, path) then
                log.warn("Updated reference image: " .. path)
                image_failures = image_failures - 1
                pending_image_mismatch = nil
                app.call_deferred(check_next)
            else
                mismatch.update_error = "Failed to update reference image at " .. path
                log.error(mismatch.update_error)
            end
        end
        ui.same_line()
    else
        ui.text("Updating the reference requires IO permission and the source project directory.")
    end

    if ui.button("Keep reference and continue") then
        pending_image_mismatch = nil
        app.call_deferred(check_next)
    end
    ui.end_window()
end

app.window.on_ui.subscribe(show_image_mismatch)

function start_next()
    test_index = test_index + 1
    local test = tests[test_index]

    test.on_complete:subscribe(function(tick_time)
        local duration = os.clock() - start_time
        test:teardown()

        log.warn(string.format("Test %d (%s) os time: %.4f seconds", test_index, test.name, duration))
        log.warn(string.format("Test %d (%s) tick time: %.4f seconds", test_index, test.name, tick_time))

        local reference = app.res.get_texture("reference_" .. test.name)
        local queued
        if reference.valid then
            queued = app.window.compare_next_frame(reference)
        elseif type(app.window.capture_next_frame_io) == "function" then
            queued = app.window.capture_next_frame_io("assets/reference/" .. test.name .. ".png")
        else
            log.error("No reference image for " .. test.name .. " and frame capture requires IO permission.")
        end
        if queued == false or queued == nil then
            image_failures = image_failures + 1
            log.error("Failed to queue the frame image request for " .. test.name .. ".")
            app.call_deferred(check_next)
        end
    end)

    print("Starting test: " .. test.name)

    test:setup()
    start_time = os.clock()
    test:run()
end

function check_next()
    if test_index < test_count then
        start_next()
    else
        if image_failures == 0 then
            log.info("All tests completed and reference images matched.")
        else
            log.error("Stress tests completed with " .. image_failures .. " frame image failure(s).")
        end
        app.window.request_close()
    end
end

check_next()

return error_code.Success