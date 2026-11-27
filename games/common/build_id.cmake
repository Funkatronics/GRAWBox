# Writes OUT (build_id.h) with GRAWBOX_BUILD, the source's `git describe --tags --always --dirty`
# (for example v0.2.0-12-gabc1234), so logs and playtest reports name the exact code. Run at every
# build; the file is only rewritten when the id changes, so nothing rebuilds needlessly.
#   cmake -DOUT=<file> -P build_id.cmake   (from the source tree)
execute_process(COMMAND git describe --tags --always --dirty
                OUTPUT_VARIABLE id OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
if(failed OR id STREQUAL "")
  set(id "unknown")
endif()
set(text "#pragma once\n#define GRAWBOX_BUILD \"${id}\"\n")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
  file(WRITE "${OUT}" "${text}")
endif()
