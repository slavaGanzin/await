cmake_minimum_required(VERSION 3.28)

project(await)

include(GNUInstallDirs)

add_executable(await await.c)

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
