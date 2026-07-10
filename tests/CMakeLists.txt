add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE omnisms_core)
add_test(NAME core COMMAND test_core)

add_executable(test_push test_push.cpp)
target_link_libraries(test_push PRIVATE omnisms_core)
add_test(NAME push COMMAND test_push)

add_executable(test_call test_call.cpp)
target_link_libraries(test_call PRIVATE omnisms_core)
add_test(NAME call COMMAND test_call)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE omnisms_core)
add_test(NAME config COMMAND test_config)

add_executable(test_queue test_queue.cpp)
target_link_libraries(test_queue PRIVATE omnisms_core)
add_test(NAME queue COMMAND test_queue)

add_executable(test_admin test_admin.cpp)
target_link_libraries(test_admin PRIVATE omnisms_core)
add_test(NAME admin COMMAND test_admin)

add_executable(test_inbox test_inbox.cpp)
target_link_libraries(test_inbox PRIVATE omnisms_core)
add_test(NAME inbox COMMAND test_inbox)

add_executable(test_email test_email.cpp)
target_link_libraries(test_email PRIVATE omnisms_core)
add_test(NAME email COMMAND test_email)

add_executable(test_scheduler test_scheduler.cpp)
target_link_libraries(test_scheduler PRIVATE omnisms_core)
add_test(NAME scheduler COMMAND test_scheduler)

if(UNIX)
    add_executable(test_rules test_rules.cpp)
    target_link_libraries(test_rules PRIVATE omnisms_core)
    add_test(NAME rules COMMAND test_rules)
endif()
