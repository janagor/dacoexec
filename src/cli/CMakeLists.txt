add_executable(dacoexec_cli main.cpp temporary_output.hpp)
set_target_properties(dacoexec_cli PROPERTIES OUTPUT_NAME dacoexec)
target_link_libraries(
  dacoexec_cli
  PRIVATE dacoexec::dacoexec
          CLI11::CLI11
          dacoexec_options
          dacoexec_warnings)
target_compile_features(dacoexec_cli PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(dacoexec_cli PRIVATE /Zc:__cplusplus)
endif()
dacoexec_configure_linker(dacoexec_cli)
if(dacoexec_INSTALL AND NOT CMAKE_SKIP_INSTALL_RULES)
  install(TARGETS dacoexec_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
