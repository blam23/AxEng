compiler = {
    type = "msvc",
}

all_configs = {
    flags = { "/std:c++latest", "/std:clatest" }
}

configs = {
    debug = {
        flags = { "/Od" },
        defines = { "DEBUG" }
    },
}