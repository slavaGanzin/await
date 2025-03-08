project('await', 'c', meson_version: '>= 1.3.2')

executable('await',
           sources: ['await.c'],
           install: true
)

install_data('autocompletions/await.fish',
            install_dir : get_option('datadir') / 'fish/vendor_completions.d')
install_data('autocompletions/await.zsh',
            install_dir : get_option('datadir') / 'zsh/site-functions',
            rename: '_await')
install_data('autocompletions/await.fish',
            install_dir : get_option('datadir') / 'bash-completion/completions')
