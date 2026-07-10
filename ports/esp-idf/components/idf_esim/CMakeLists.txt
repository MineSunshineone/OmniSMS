idf_component_register(
  SRCS "idf_esim.cpp"
  INCLUDE_DIRS "include"
  REQUIRES
    idf_logbuf
    idf_modem
)
