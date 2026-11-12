add_executable(tst_config tst_config.cpp)
target_link_libraries(tst_config PRIVATE sleekocpp_core Qt6::Test)
add_test(NAME tst_config COMMAND tst_config)

add_executable(tst_ocpp tst_ocpp.cpp)
target_link_libraries(tst_ocpp PRIVATE sleekocpp_core Qt6::Test)
add_test(NAME tst_ocpp COMMAND tst_ocpp)

add_executable(tst_centralsystem tst_centralsystem.cpp)
target_link_libraries(tst_centralsystem PRIVATE sleekocpp_core Qt6::Test)
add_test(NAME tst_centralsystem COMMAND tst_centralsystem)

add_executable(tst_commands tst_commands.cpp)
target_link_libraries(tst_commands PRIVATE sleekocpp_core Qt6::Test)
add_test(NAME tst_commands COMMAND tst_commands)

add_executable(tst_model tst_model.cpp)
target_link_libraries(tst_model PRIVATE sleekocpp_core Qt6::Test)
add_test(NAME tst_model COMMAND tst_model)

add_executable(tst_rest tst_rest.cpp)
target_link_libraries(tst_rest PRIVATE sleekocpp_core Qt6::Test)
add_test(NAME tst_rest COMMAND tst_rest)

add_executable(tst_e2e tst_e2e.cpp)
target_link_libraries(tst_e2e PRIVATE sleekocpp_core Qt6::Test)
target_compile_definitions(tst_e2e PRIVATE SLEEKOCPP_BINARY="$<TARGET_FILE:sleekocpp>")
add_dependencies(tst_e2e sleekocpp)
add_test(NAME tst_e2e COMMAND tst_e2e)

if(SLEEKOCPP_BUILD_GUI)
    add_executable(tst_gui tst_gui.cpp)
    target_link_libraries(tst_gui PRIVATE sleekocpp_gui Qt6::Test)
    add_test(NAME tst_gui COMMAND tst_gui)
    # Run without a display so the test also works on servers/CI.
    set_tests_properties(tst_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
