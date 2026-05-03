{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation {
  pname = "await";
  version = "2.1.0";

  src = ./.;

  nativeBuildInputs = with pkgs; [
    cmake
  ];

  nativeCheckInputs = with pkgs; [
    python312
    python312Packages.pytest
  ];

  doCheck = true;

  preCheck = ''
    # TMPDIR is already set by Nix to a writable sandbox location
    # Just replace the await path for tests
    substituteInPlace ../tests/test_await.py \
      --replace-fail "../await" "../build/await"
  '';

  checkPhase = ''
    runHook preCheck

    # Return to source directory since build happens in ./build
    cd ..
    cd tests
    python -m pytest test_await.py -v

    runHook postCheck
  '';

  installPhase = ''
    # After tests we're in tests/, go back to source root
    cd ..

    mkdir -p $out/bin
    cp build/await $out/bin/

    mkdir -p $out/share/bash-completion/completions
    cp build/await.bash $out/share/bash-completion/completions/await

    mkdir -p $out/share/fish/vendor_completions.d
    cp build/await.fish $out/share/fish/vendor_completions.d/

    mkdir -p $out/share/zsh/site-functions
    cp build/await.zsh $out/share/zsh/site-functions/_await
  '';

  meta = with pkgs.lib; {
    description = "Runs list of commands and waits for their termination";
    homepage = "https://github.com/slavaGanzin/await";
    license = licenses.mit;
    platforms = platforms.unix;
  };
}
