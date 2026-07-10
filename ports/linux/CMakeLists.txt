find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
include(GNUInstallDirs)

add_library(omnisms_linux_web
    src/config_store.cpp
    src/web_server.cpp
)
target_include_directories(omnisms_linux_web PUBLIC include)
target_link_libraries(omnisms_linux_web PUBLIC omnisms_core Threads::Threads)
target_compile_definitions(omnisms_linux_web PUBLIC
    OMNISMS_WEBUI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/webui"
)

file(GLOB MODEM_DRIVER_SOURCES CONFIGURE_DEPENDS src/drivers/*.cpp)
add_library(omnisms_linux_modem OBJECT
    src/at_channel.cpp
    src/modem_backend.cpp
    src/modem_driver.cpp
    ${MODEM_DRIVER_SOURCES}
)
target_include_directories(omnisms_linux_modem PUBLIC include)
target_link_libraries(omnisms_linux_modem PUBLIC omnisms_core Threads::Threads)

add_executable(omnismsd src/main.cpp)
target_link_libraries(omnismsd PRIVATE omnisms_core omnisms_linux_modem omnisms_linux_web CURL::libcurl Threads::Threads)

if(OMNISMS_BUILD_TESTS)
    add_executable(test_modem_driver tests/test_modem_driver.cpp)
    target_link_libraries(test_modem_driver PRIVATE omnisms_linux_modem Threads::Threads)
    add_test(NAME linux_modem_driver COMMAND test_modem_driver)

    add_executable(test_modem_backend tests/test_modem_backend.cpp)
    target_link_libraries(test_modem_backend PRIVATE omnisms_core omnisms_linux_modem Threads::Threads)
    add_test(NAME linux_modem_backend COMMAND test_modem_backend)

    add_executable(fake_mmcli tests/fake_mmcli.cpp)
    set_target_properties(fake_mmcli PROPERTIES
        OUTPUT_NAME mmcli
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake-bin"
    )

    add_test(
        NAME linux_modemmanager_list_cli
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:fake_mmcli>:$ENV{PATH}"
            $<TARGET_FILE:omnismsd> --list-modems
    )
    set_tests_properties(linux_modemmanager_list_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "ModemManager devices \\(1\\)"
    )

    add_test(
        NAME linux_modemmanager_send_cli
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:fake_mmcli>:$ENV{PATH}"
            $<TARGET_FILE:omnismsd>
            -c ${CMAKE_CURRENT_SOURCE_DIR}/tests/minimal_modemmanager.json
            --send-sms +8613900139000 "comma, unicode 世界"
    )
    set_tests_properties(linux_modemmanager_send_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "短信发送成功"
    )

    add_test(
        NAME linux_modemmanager_ussd_cli
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:fake_mmcli>:$ENV{PATH}"
            $<TARGET_FILE:omnismsd>
            -c ${CMAKE_CURRENT_SOURCE_DIR}/tests/minimal_modemmanager.json
            --ussd *100#
    )
    set_tests_properties(linux_modemmanager_ussd_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "balance 10.00"
    )

    add_executable(test_web_server tests/test_web_server.cpp)
    target_link_libraries(test_web_server PRIVATE omnisms_linux_web Threads::Threads)
    add_test(NAME linux_web_server COMMAND test_web_server)

    add_executable(test_config_store tests/test_config_store.cpp)
    target_link_libraries(test_config_store PRIVATE omnisms_linux_web)
    add_test(NAME linux_config_store COMMAND test_config_store)

    add_test(
        NAME linux_json_pipeline
        COMMAND omnismsd
            -c ${CMAKE_CURRENT_SOURCE_DIR}/tests/minimal.json
            --test-pdu 00040D91683108108300F000006270012100002305E8329BFD06
    )
endif()

install(TARGETS omnismsd RUNTIME DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/webui/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/omnisms/webui)
