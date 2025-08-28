cmake_minimum_required(VERSION 3.28)

project(await)

add_executable(await await.c)

install(TARGETS await DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES autocompletions/await.fish
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/fish/vendor_completions.d/)
install(FILES autocompletions/await.zsh
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/zsh/site-functions/
        RENAME _await)
install(FILES autocompletions/await.bash
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/bash-completion/completions/)
