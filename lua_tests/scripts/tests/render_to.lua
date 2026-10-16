local test_suite = ax.import("suite")
local suite = test_suite:new("Render To")

if not app.window then
    local test = suite:new("Headless mode omits window bindings")
    function test:run()
        self:assert(app.window == nil, "window bindings should not be available in headless mode")
    end
    suite:add_test(test)
    return suite
end

do
    local test = suite:new("Submits isolated batches to a texture")
    function test:run()
        local target = app.res.create_temp_texture("lua_render_to_target", 128, 128)
        local second_target = app.res.create_temp_texture("lua_render_to_second_target", 128, 128)
        local source = app.res.create_temp_texture("lua_render_to_source", 16, 16)
        local source_alias = app.res.create_temp_texture("lua_render_to_source", 16, 16)

        if not self:assert(target.valid and second_target.valid and source.valid and source_alias.valid, "Failed to create render-to test textures") then
            return
        end
        if not self:assert(type(app.window.render_to) == "function", "window.render_to is not available") then
            return
        end

        local callback_called = false
        local submitted = app.window.render_to(target, function(canvas)
            callback_called = true
            if not self:assert(
                type(canvas.draw_texture) == "function"
                    and type(canvas.draw_ui_texture) == "function"
                    and type(canvas.draw_shapes) == "function",
                "render_to canvas methods are missing"
            ) then
                return
            end

            canvas:draw_texture(source, 4, 8)
            canvas:draw_ui_texture(source, 20, 24, 2)
            canvas:draw_texture(source, 36, 40, 0.1, 0, 0, 16, 16, 3, 1, 1, 1, 1, 1, 1)
            canvas:draw_shapes({
                { type = "rectangle", x = 32, y = 32, width = 24, height = 16, fill = { 1, 0, 0, 1 } },
                { type = "circle", x = 80, y = 80, radius = 12, fill = { 0, 1, 0, 1 } },
            })
        end)
        self:assert(submitted, "render_to did not submit a valid batch")
        self:assert(callback_called, "render_to did not invoke its callback")

        app.window.render(source, 4, 8, 0, 0, 8, 8)
        app.window.render(source, 16, 20, 0, 0, 8, 8, 2)
        app.window.render(source, 28, 32, 0, 0, 8, 8, 0.1, 3, 1, 1, 1, 1, 1, 1)
        app.window.render_ui(source, 40, 44, 0, 0, 8, 8, 0.1, 4, 1, 1, 1, 1, 1, 1)
        local persistent_sprite = app.sprites.allocate()
        app.sprites.setup(persistent_sprite, source, 4, 8)

        local shapes = app.shapes.list()
        shapes:add({ type = "rectangle", x = 8, y = 8, width = 16, height = 16, fill = { 0, 0, 1, 1 } })
        self:assert(app.window.render_to(second_target, function(canvas)
            canvas:draw_shapes(shapes)
        end), "render_to did not accept a shape_list")

        self:assert(app.res.release_temp_texture(target), "Failed to release the render target")
        self:assert(app.res.release_temp_texture(second_target), "Failed to release the second render target")
        self:assert(app.res.release_temp_texture(source), "Failed to release the source texture")
        self:assert(not source.valid and not source_alias.valid, "Releasing a temporary texture should invalidate every handle")

        local rendered_frames = 0
        app.window.on_render.subscribe(function()
            rendered_frames = rendered_frames + 1
            if rendered_frames == 2 then
                app.sprites.free(persistent_sprite)
                app.window.request_close()
            end
        end)
    end

    suite:add_test(test)
end

return suite
