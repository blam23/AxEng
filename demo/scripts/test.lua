x = 10
y = 10

function print_pos()
    print("[" .. x .. "," .. y .. "]")
end

function w_key_event(pressed, mods)
    if pressed then
        y = y - 1
        print_pos()
    end
end

function a_key_event(pressed, mods)
    if pressed then
        x = x - 1
        print_pos()
    end
end

function s_key_event(pressed, mods)
    if pressed then
        y = y + 1
        print_pos()
    end
end

function d_key_event(pressed, mods)
    if pressed then
        x = x + 1
        print_pos()
    end
end

function any_key_event(key, pressed, mods)
    print("KEY: " .. tostring(key) .. " - " .. tostring(pressed))
end

keyboard.subscribe_key(keys.w, w_key_event)
keyboard.subscribe_key(keys.a, a_key_event)
keyboard.subscribe_key(keys.s, s_key_event)
keyboard.subscribe_key(keys.d, d_key_event)
keyboard.subscribe_all(any_key_event)