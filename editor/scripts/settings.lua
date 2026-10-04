local json = ax.import("@json")

local settings = {
    file_name = "settings.json",
    data = {}
}

function settings:load()
    local file = app.user_io.open(self.file_name, "r")
    if not file then
        return false
    end

    -- Read the file content
    local content = file:read_all()
    file:close()

    print("Read content: ", content)

    -- Parse the JSON content
    local ok, data = pcall(function() return json.decode(content) end)
    if not ok then
        return false
    end

    ax.print_table(data)

    self.data = data

    return true
end

function settings:save()
    print("Saving settings..")
    local file = app.user_io.open(self.file_name, "w")
    if not file then
        log.error("Failed to open file")
        return false
    end

    print("Opened file")

    local ok, content = pcall(function() return json.encode(self.data) end)
    if not ok then
        log.error("Failed to encode JSON: " .. tostring(content))
        return false
    end

    print("JSON: ", content)

    file:write(content)
    file:close()

    print("CLOSED!")

    return true
end

function settings:set(key, value)
    self.data[key] = value
    self:save()
end

function settings:get(key)
    print("Getting key: ", key)
    print("Value: ", self.data[key])
    return self.data[key]
end

function settings:get_or(key, default)
    local value = self.data[key]
    if value == nil then
        self.data[key] = default
        self:save()
        return default
    end
    return value
end

return settings