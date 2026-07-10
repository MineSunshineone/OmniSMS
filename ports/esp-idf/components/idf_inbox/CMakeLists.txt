idf_component_register(
  SRCS "idf_inbox.cpp"
  INCLUDE_DIRS "include"
  REQUIRES
    freertos
    nvs_flash
    omnisms_core
)
