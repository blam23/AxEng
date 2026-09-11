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

keyboard.register(keys.w, w_key_event)
keyboard.register(keys.a, a_key_event)
keyboard.register(keys.s, s_key_event)
keyboard.register(keys.d, d_key_event)