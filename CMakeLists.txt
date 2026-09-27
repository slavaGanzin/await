cmake_minimum_required(VERSION 3.28)

project(await)

# Release builds by default: optimised for size and without symbols, so the
# binary CI tests is the one that ships. Pass -DCMAKE_BUILD_TYPE=Debug (e.g.
# for sanitizer builds) to keep symbols.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

add_executable(await await.c)

# by configuration, so multi-config generators (Xcode, Ninja Multi-Config)
# strip their Release builds too
if(APPLE)
  # strip at link time: a later `strip` would invalidate the linker's
  # ad-hoc code signature on Apple Silicon
  target_link_options(await PRIVATE "$<$<CONFIG:MinSizeRel,Release>:-Wl,-S;-Wl,-x>")
else()
  target_link_options(await PRIVATE $<$<CONFIG:MinSizeRel,Release>:-s>)
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

install(TARGETS await DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.fish
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/fish/vendor_completions.d/)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.zsh
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/zsh/site-functions/
        RENAME _await)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/await.bash
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/bash-completion/completions/)
