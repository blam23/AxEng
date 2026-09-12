compiler = {
    type = "msvc",
}

all_configs = {
    flags = { "/std:c++latest", "/std:clatest", "/utf-8" }
    defines = { }
}

configs = {
    debug = {
        flags = { "/Od" },
        defines = { "DEBUG" }
    },
    fast_debug = {
        flags = { "/O2 /Ot /Oi /dynamicdeopt" },
        defines = { "DEBUG", "FASTDEBUG" }
    }
    release = {
        flags = { "/O2 /Ot /Oi /GL" },
        defines = { "RELEASE" }
    }
}