cmake_minimum_required(VERSION 3.28)

project(await)

include(GNUInstallDirs)

# Release builds by default: optimised for size and without symbols, so the
# binary CI tests is the one that ships. Pass -DCMAKE_BUILD_TYPE=Debug (e.g.
# for sanitizer builds) to keep symbols.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

add_executable(await await.c)

# Size flags by configuration, so multi-config generators (Xcode, Ninja
# Multi-Config) get them for their Release builds too
set(SMALL "$<CONFIG:MinSizeRel,Release>")
if(APPLE)
  # strip at link time: a later `strip` would invalidate the linker's
  # ad-hoc code signature on Apple Silicon
  target_link_options(await PRIVATE "$<${SMALL}:-Wl,-S;-Wl,-x;-Wl,-dead_strip>")
else()
  # drop unused functions and data, and strip
  target_compile_options(await PRIVATE "$<${SMALL}:-ffunction-sections;-fdata-sections>")
  target_link_options(await PRIVATE "$<${SMALL}:-s;-Wl,--gc-sections>")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # ELF only: no unwind tables (await is C and never unwinds a stack), and
  # don't pad the code, rodata and data segments out to separate 4KB pages
  target_compile_options(await PRIVATE "$<${SMALL}:-fno-asynchronous-unwind-tables>")
  target_link_options(await PRIVATE "$<${SMALL}:-Wl,-z,noseparate-code>")
endif()

# Generate autocompletion files
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/await.fish
        ${CMAKE_CURRENT_BINARY_DIR}/await.bash
        ${CMAKE_CURRENT_BINARY_DIR}/await.zsh
    COMMAND $<TARGET_FILE:await> --autocomplete-fish > ${CMAKE_CURRENT_BINARY_DIR}/await.fish
    COMMAND $<TARGET_FILE:await> --autocomplete-bash > ${CMAKE_CURRENT_BINARY_DIR}/await.bash
    COMMAND $<TARGET_FILE:await> --autocomplete-zsh > ${CMAKE_CURRENT_BINARY_DIR}/await.zsh
    DEPENDS await
)

add_custom_target(autocompletions ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/await.fish
        ${CMAKE_CURRENT_BINARY_DIR}/await.bash
        ${CMAKE_CURRENT_BINARY_DIR}/await.zsh
)

# Generate the man page from --help, so it can never drift from it
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/await.1
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/man/gen-man.sh $<TARGET_FILE:await> > ${CMAKE_CURRENT_BINARY_DIR}/await.1
    DEPENDS await ${CMAKE_CURRENT_SOURCE_DIR}/man/gen-man.sh
)

add_custom_target(manpage ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/await.1
)

install(TARGETS await DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.fish
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/fish/vendor_completions.d/)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.zsh
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/zsh/site-functions/
        RENAME _await)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.bash
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/bash-completion/completions/)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1/)
