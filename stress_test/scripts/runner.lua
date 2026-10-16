-- -cxr --allow-io --allow-os --in "$(SolutionDir)stress_test" --out "E:\AxStress"

-- Timings from a debug engine build are meaningless (and the tests take a very long time), so refuse to run.
if app.engine_build.debug then
    log.error("Stress tests require a release build of the engine, but this is a '" .. app.engine_build.mode .. "' build. "
        .. "Rebuild with 'xmake f -m release' then 'xmake'.")
    return error_code.InvalidConfiguration
end

math.randomseed(0574355)

local spike_stddev_multiplier = 3.0

local function parse_arguments()
    local requested_tests = {}
    local index = 1

    while index <= #args do
        local argument = args[index]
        local test_value = argument:match("^%-%-test=(.+)$")
        if argument == "--test" then
            index = index + 1
            local name = args[index]
            if not name or name:sub(1, 2) == "--" then
                return nil, "--test requires a test name."
            end
            table.insert(requested_tests, name)
        elseif test_value then
            table.insert(requested_tests, test_value)
        elseif argument ~= "--" then
            return nil, "Unknown stress runner argument '" .. argument .. "'."
        end
        index = index + 1
    end

    return requested_tests
end

local requested_tests, argument_error = parse_arguments()
if argument_error then
    log.error(argument_error)
    log.info("Usage: --test <name> [--test <name> ...]")
    return error_code.InvalidConfiguration
end

local tests = {}
local test_names = {}
local test_index = 0
local start_time = 0
local frame_times = {}
local frame_capture_active = false
local skip_first_frame = false
local active_test = nil

local scripts = manifest.scripts
for test_name, _ in pairs(scripts) do
    if test_name:match("^test_") then
        table.insert(test_names, test_name)
    end
end

table.sort(test_names)
local available_tests = {}
for _, test_name in ipairs(test_names) do
    available_tests[test_name] = true
end

local selected_tests = {}
for _, requested_name in ipairs(requested_tests) do
    local test_name = requested_name:match("^test_") and requested_name or "test_" .. requested_name
    if not available_tests[test_name] then
        log.error("Unknown stress test '" .. requested_name .. "'.")
        log.info("Available tests: " .. table.concat(test_names, ", "))
        return error_code.InvalidConfiguration
    end
    selected_tests[test_name] = true
end

for _, test_name in ipairs(test_names) do
    if #requested_tests == 0 or selected_tests[test_name] then
        table.insert(tests, ax.import(test_name))
        tests[#tests].name = test_name
        print("Found test: " .. test_name)
    end
end
local test_count = #tests

print(string.format("Total tests selected: %d of %d", test_count, #test_names))
print(string.format("Statistical spike cutoff: mean + %.2f standard deviations", spike_stddev_multiplier))

local image_failures = 0
local pending_image_mismatch = nil

app.on_update.subscribe(function(delta)
    if not frame_capture_active then return end
    if skip_first_frame then
        skip_first_frame = false
        return
    end
    if active_test and active_test.frame_sample_limit and #frame_times >= active_test.frame_sample_limit then
        return
    end
    if type(delta) ~= "number" or not (delta >= 0 and delta < math.huge) then
        log.error("Stress runner received an invalid frame delta.")
        return
    end
    frame_times[#frame_times + 1] = delta
end)

local function report_frame_times(test_name)
    local sample_count = #frame_times
    if sample_count == 0 then
        log.warn(string.format("Test (%s) produced no frame-time samples.", test_name))
        return
    end

    local sorted_times = {}
    local total_time = 0
    for i, frame_time in ipairs(frame_times) do
        sorted_times[i] = frame_time
        total_time = total_time + frame_time
    end
    table.sort(sorted_times)

    local mean = total_time / sample_count
    local variance = 0
    for _, frame_time in ipairs(frame_times) do
        local difference = frame_time - mean
        variance = variance + difference * difference
    end
    local standard_deviation = math.sqrt(variance / sample_count)
    local median = sample_count % 2 == 1
        and sorted_times[(sample_count + 1) / 2]
        or (sorted_times[sample_count / 2] + sorted_times[sample_count / 2 + 1]) / 2

    local absolute_deviations = {}
    for i, frame_time in ipairs(frame_times) do
        absolute_deviations[i] = math.abs(frame_time - median)
    end
    table.sort(absolute_deviations)
    local mad = sample_count % 2 == 1
        and absolute_deviations[(sample_count + 1) / 2]
        or (absolute_deviations[sample_count / 2] + absolute_deviations[sample_count / 2 + 1]) / 2

    local spike_threshold = mean + spike_stddev_multiplier * standard_deviation

    local spike_samples = {}
    for i, frame_time in ipairs(frame_times) do
        local score = standard_deviation > 0 and (frame_time - mean) / standard_deviation or 0
        if frame_time > spike_threshold then
            spike_samples[#spike_samples + 1] = string.format(
                "%d (%.2f ms, z=%.2f)",
                i,
                frame_time * 1000,
                score
            )
        end
    end

    log.warn(string.format(
        "Test (%s) frame times: %d samples, mean %.2f ms, std dev %.2f ms, median %.2f ms, "
            .. "MAD %.2f ms, p95 %.2f ms, p99 %.2f ms, min %.2f ms, max %.2f ms",
        test_name,
        sample_count,
        mean * 1000,
        standard_deviation * 1000,
        median * 1000,
        mad * 1000,
        sorted_times[math.ceil(sample_count * 0.95)] * 1000,
        sorted_times[math.ceil(sample_count * 0.99)] * 1000,
        sorted_times[1] * 1000,
        sorted_times[sample_count] * 1000
    ))
    log.warn(string.format(
        "Test (%s) spike rule: mean + %.2f x std dev; boundary %.2f ms; %d spike(s)",
        test_name,
        spike_stddev_multiplier,
        spike_threshold * 1000,
        #spike_samples
    ))
    if #spike_samples > 0 then
        log.info("Spike samples: " .. table.concat(spike_samples, ", "))
    end
end

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
        frame_capture_active = false
        local duration = os.clock() - start_time
        report_frame_times(test.name)
        test:teardown()

        log.warn(string.format("Test %d (%s) os time: %.4f seconds", test_index, test.name, duration))
        log.warn(string.format("Test %d (%s) tick time: %.4f seconds", test_index, test.name, tick_time))

        if test.skip_image_comparison then
            log.info("Skipping frame image comparison for " .. test.name .. ".")
            app.call_deferred(check_next)
            return
        end

        local reference = app.res.get_texture("reference_" .. test.name)
        local queued
        if reference.valid then
            queued = app.window.compare_next_frame(reference)
        elseif type(app.window.capture_next_frame_io) == "function" then
            queued = app.project_source_directory
                and app.window.capture_next_frame_io(
                    app.project_source_directory .. "/assets/reference/" .. test.name .. ".png"
                )
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

    frame_times = {}
    active_test = test
    test:setup()
    start_time = os.clock()
    skip_first_frame = true
    frame_capture_active = true
    test:run()
end

function check_next()
    if test_index < test_count then
        start_next()
    else
        if image_failures == 0 then
            log.info("All selected tests completed with no image-comparison failures.")
        else
            log.error("Stress tests completed with " .. image_failures .. " frame image failure(s).")
        end
        app.window.request_close()
    end
end

check_next()

return error_code.Success