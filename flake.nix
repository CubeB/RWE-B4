{
  description = "Robot War Engine — a Total Annihilation engine recreation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Libraries that SDL3, SDL3_mixer, and GLEW dlopen at runtime.
        # They aren't pulled in by the linker so they must be reachable via
        # LD_LIBRARY_PATH when running ./build/rwe inside the dev shell.
        runtimeLibs = with pkgs; [
          alsa-lib
          pulseaudio
          pipewire
          wayland
          libxkbcommon
          libdecor
          libGL
          libglvnd
          libx11
          libxext
          libxi
          libxfixes
          libxrandr
          libxscrnsaver
          libxcursor
          libxtst
          libxcb
        ];
      in {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            pkg-config
            git
            gnumake

            protobuf

            glew
            libpng
            zlib

            libGL
            libglvnd

            libx11
            libxext
            libxi
            libxfixes
            libxrandr
            libxscrnsaver
            libxcursor
            libxtst
            libxcb

            alsa-lib
            pipewire
            pulseaudio

            wayland
            wayland-protocols
            libxkbcommon
            libdecor
          ];

          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath runtimeLibs;

          shellHook = ''
            # Force the X11 video backend; SDL3's Wayland backend currently
            # fails to initialize ("wayland not available") on this setup, but
            # XWayland works fine. Override on the command line if needed:
            #   SDL_VIDEODRIVER=wayland ./build/rwe
            export SDL_VIDEODRIVER=x11

            echo "RWE Nix shell ready (nixpkgs unstable)."
            echo ""
            echo "First-time setup:"
            echo "  git submodule update --init --recursive"
            echo ""
            echo "Build:"
            echo "  mkdir -p build && cd build && cmake .. && make -j\$(nproc)"
            echo ""
            echo "Test:"
            echo "  ./build/rwe_test '[cob][speed]'"
          '';
        };
      });
}
