add_executable(Tests
    log_capture.cpp
    log_capture.h
    main.cpp
    test_background_worker.cpp
    test_custom_type.cpp
    test_event.cpp
    test_lua_manager.cpp
    test_resource_loader.cpp
)

# Explicit, matching the original Tests.vcxproj's own AdditionalIncludeDirectories
# (redundant with AxEngLib's PUBLIC include dir, but kept for parity/clarity).
target_include_directories(Tests PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(Tests PRIVATE AxEngLib GTest::gtest)

axeng_configure_target(Tests
    WARNING_LEVEL W4
    CXX_STANDARD_FLAG /std:c++23
    TREAT_WARNINGS_AS_ERRORS
)

include(GoogleTest)
gtest_discover_tests(Tests)
