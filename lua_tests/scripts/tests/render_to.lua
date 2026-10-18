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

do
    -- A 4x2 opaque red PNG.
    local PNG = "\137\80\78\71\13\10\26\10\0\0\0\13\73\72\68\82\0\0\0\4\0\0\0\2\8\6\0\0\0\127\168\125\99\0\0\0\18\73\68\65\84\120\156\99\248\207\192\240\31\25\51\160\11\0\0\15\33\15\241\4\55\198\159\0\0\0\0\73\69\78\68\174\66\96\130"

    local test = suite:new("Creates releasable textures from encoded images")
    function test:run()
        self:assert(type(app.res.create_temp_texture_from_image) == "function", "create_temp_texture_from_image is not available")

        local full = app.res.create_temp_texture_from_image("lua_image_full", PNG)
        if not self:assert(full.valid and full.temp, "Failed to create a texture from a PNG") then
            return
        end
        self:assert(full.width == 4 and full.height == 2, "Full size image has the wrong dimensions")

        local small = app.res.create_temp_texture_from_image("lua_image_small", PNG, 2, 2)
        if not self:assert(small.valid, "Failed to create a downscaled texture from a PNG") then
            return
        end
        self:assert(small.width == 2 and small.height == 1, "Downscaled image should be halved to fit 2x2")

        local invalid = app.res.create_temp_texture_from_image("lua_image_invalid", "not a png")
        self:assert(not invalid.valid, "Invalid image data should not create a texture")

        local target = app.res.create_temp_texture("lua_image_target", 8, 8)
        self:assert(app.window.render_to(target, function(canvas)
            canvas:draw_ui_texture(full, 0, 0)
        end), "Image textures should be drawable")

        self:assert(app.res.release_temp_texture(target), "Failed to release the image render target")
        self:assert(app.res.release_temp_texture(full), "Failed to release the full size image")
        self:assert(app.res.release_temp_texture(small), "Failed to release the downscaled image")
        self:assert(not full.valid and not small.valid, "Released image textures should be invalid")
    end

    suite:add_test(test)
end

return suite
