get_filename_component(OMNISMS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# 按迁移进度逐项加入共享实现；未迁移组件继续使用完整固件原逻辑，避免一次性行为变化。
idf_component_register(
    SRCS
        "${OMNISMS_ROOT}/core/src/admin.cpp"
        "${OMNISMS_ROOT}/core/src/phone.cpp"
        "${OMNISMS_ROOT}/core/src/text.cpp"
        "${OMNISMS_ROOT}/core/src/rules.cpp"
        "${OMNISMS_ROOT}/core/src/crypto.cpp"
        "${OMNISMS_ROOT}/core/src/push.cpp"
        "${OMNISMS_ROOT}/core/src/pdu.cpp"
        "${OMNISMS_ROOT}/core/src/config.cpp"
        "${OMNISMS_ROOT}/core/src/email.cpp"
        "${OMNISMS_ROOT}/core/src/inbox.cpp"
        "${OMNISMS_ROOT}/core/src/scheduler.cpp"
    INCLUDE_DIRS
        "${OMNISMS_ROOT}/core/include"
    REQUIRES
        idf_pdu
)
