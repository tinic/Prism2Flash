# General printf can pull in Newlib math initializers and fail before main.
file(STRINGS "${BINARY}" unwanted REGEX "mathieeedoub(bas|trans)\\.library")
if(unwanted)
  message(FATAL_ERROR "${BINARY} unexpectedly depends on disk-based IEEE double math libraries: ${unwanted}")
endif()
