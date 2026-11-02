macro(dacoexec_configure_linker project_name)
  set(dacoexec_USER_LINKER_OPTION
      "DEFAULT"
      CACHE STRING "Linker to be used")
  set(dacoexec_USER_LINKER_OPTION_VALUES
      "DEFAULT"
      "SYSTEM"
      "LLD"
      "GOLD"
      "BFD"
      "MOLD"
      "SOLD"
      "APPLE_CLASSIC"
      "MSVC")
  set_property(CACHE dacoexec_USER_LINKER_OPTION PROPERTY STRINGS ${dacoexec_USER_LINKER_OPTION_VALUES})
  list(
    FIND
    dacoexec_USER_LINKER_OPTION_VALUES
    ${dacoexec_USER_LINKER_OPTION}
    dacoexec_USER_LINKER_OPTION_INDEX)

  if(${dacoexec_USER_LINKER_OPTION_INDEX} EQUAL -1)
    message(
      STATUS
        "Using custom linker: '${dacoexec_USER_LINKER_OPTION}', explicitly supported entries are ${dacoexec_USER_LINKER_OPTION_VALUES}"
    )
  endif()

  set_target_properties(${project_name} PROPERTIES LINKER_TYPE "${dacoexec_USER_LINKER_OPTION}")
endmacro()
