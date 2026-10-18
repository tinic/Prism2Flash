# General printf can pull in Newlib math initializers and fail before main.
file(STRINGS "${BINARY}" unwanted REGEX "mathieeedoub(bas|trans)\\.library")
if(unwanted)
  message(FATAL_ERROR "${BINARY} unexpectedly depends on disk-based IEEE double math libraries: ${unwanted}")
endif()

# Throwing new can pull libstdc++ exception startup into otherwise exception-free
# code. Keep that runtime out of Kickstart 2.x binaries and the smoke helper.
file(STRINGS "${BINARY}" exceptions REGEX "__cxa_throw")
if(exceptions)
  message(FATAL_ERROR "${BINARY} unexpectedly links the throwing C++ runtime")
endif()
