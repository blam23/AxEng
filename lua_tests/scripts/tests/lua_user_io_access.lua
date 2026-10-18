local test_suite = ax.import("suite")
local base_test = ax.import("base_test")

local suite = test_suite:new("Lua User IO Access")


do
    local test = suite:new("Can Create And Read File In User IO Directory")
    function test:run()
        local file = app.user_io.open("test.txt", "w")
        if file then
            file:write("Hello, world!\n")
            file:close()
        else
            self:fail("Failed to open test.txt file for writing")
        end
        
        local file2 = app.user_io.open("test.txt", "r")
        if file2 then
            local content = file2:read_all()
            file2:close()
            self:assert(content == "Hello, world!\n", "File content mismatch")
        else
            self:fail("Failed to open test.txt file for reading")
        end
    end

    suite:add_test(test)
end

do
    local test = suite:new("Can't create files outside User IO Directory")
    function test:run()
        local file = app.user_io.open("../test.txt", "w")
        local file2 = app.user_io.open("C://test.txt", "w")
        local file3 = app.user_io.open("/test.txt", "w")

        if file or file2 or file3 then
            self:fail("Was able to create a file outside User IO directory")
        end
    end

    suite:add_test(test)
end

do
    local test = suite:new("Can Remove Files In User IO Directory")
    function test:run()
        local file = app.user_io.open("remove_me.txt", "w")
        if not file then
            self:fail("Failed to open remove_me.txt file for writing")
            return
        end
        file:write("delete me")
        file:close()

        self:assert(app.user_io.remove("remove_me.txt"), "Failed to remove remove_me.txt")
        self:assert(app.user_io.open("remove_me.txt", "r") == nil, "remove_me.txt still exists after removal")
        self:assert(not app.user_io.remove("remove_me.txt"), "Removing a missing file should fail")
    end

    suite:add_test(test)
end

return suite