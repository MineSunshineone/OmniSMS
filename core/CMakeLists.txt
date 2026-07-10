# OmniSMS core：纯 C++17，无平台依赖(POSIX regex 除外，Windows 主机不编 rules)
add_library(omnisms_core STATIC
    src/admin.cpp
    src/phone.cpp
    src/text.cpp
    src/pdu.cpp
    src/call.cpp
    src/config.cpp
    src/email.cpp
    src/inbox.cpp
    src/crypto.cpp
    src/push.cpp
    src/queue.cpp
    src/scheduler.cpp
    third_party/pdulib/pdulib.cpp
)

target_include_directories(omnisms_core PUBLIC include third_party/pdulib)
target_compile_definitions(omnisms_core PUBLIC DESKTOP_PDU)

# rules 依赖 POSIX regex.h：MinGW/MSVC 主机上跳过(CI 与目标平台均为 POSIX)
if(UNIX)
    target_sources(omnisms_core PRIVATE src/rules.cpp)
endif()
