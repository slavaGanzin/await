{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    cmake
    gcc
    python312
    python312Packages.pytest
    bash
  ];

  shellHook = ''
    echo "Nix development environment for await"
    echo "Building with CMake..."

    # Set up similar to Nix build environment
    export TMPDIR="''${TMPDIR:-/tmp}"
    export HOME="''${HOME:-/build}"

    echo "TMPDIR: $TMPDIR"
    echo "Run 'mkdir build && cd build && cmake .. && make' to build"
    echo "Run 'cd tests && pytest test_await.py -v' to run tests"
  '';
}
