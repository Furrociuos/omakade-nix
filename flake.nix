{
  description = "Omakade - a beautiful, local-first game library built for Omarchy";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # kdePackages, not qt6: LayerShellQt only lives in the kdePackages
        # scope, and it needs to be built against the *same* Qt6 as
        # everything else, so every Qt module below is pulled from this
        # scope too rather than mixing it with `pkgs.qt6`.
        omakade = pkgs.kdePackages.callPackage
          ({ lib
           , stdenv
           , cmake
           , ninja
           , pkg-config
           , wrapQtAppsHook
           , qtbase
           , qtdeclarative
           , qtimageformats
           , qtsvg
           , qtwayland
           , qttools
           , layer-shell-qt
           , sdl3
           , libsecret
           , libzip
           , zstd
           , openssl
           , glib
           , hicolor-icon-theme
           , wayland
           , wayland-protocols
           , addDriverRunpath
           , mesa
           , libglvnd
           }:
            stdenv.mkDerivation (finalAttrs: {
              pname = "omakade";
              version = "1.15.0";

              src = ./.;

              nativeBuildInputs = [
                cmake
                ninja
                pkg-config
                wrapQtAppsHook
                qttools
                # Needed at configure time for wayland-scanner, used to
                # generate the idle-inhibit protocol bindings.
                wayland
                # Patches the installed binary's RUNPATH to also search
                # /run/opengl-driver/lib, so it finds the *system's* Mesa/
                # EGL driver at runtime (provisioned by NixOS's
                # hardware.graphics.enable) instead of only the libraries
                # present in the Nix store at build time.
                addDriverRunpath
              ];

              buildInputs = [
                qtbase
                qtdeclarative
                qtimageformats
                qtsvg
                qtwayland
                layer-shell-qt
                sdl3
                libsecret
                libzip
                zstd
                openssl
                glib
                hicolor-icon-theme
                wayland
                wayland-protocols
                # libEGL.so.1 dispatch (libglvnd) and libwayland-egl.so
                # (mesa) - the Qt Wayland "egl" platform integration dlopens
                # these directly; without them in the closure, EGL init
                # fails even once /run/opengl-driver is correctly on the
                # RUNPATH, since that only supplies the vendor-specific
                # drivers glvnd dispatches *to*, not glvnd/wayland-egl
                # themselves.
                mesa
                libglvnd
              ];

              cmakeFlags = [
                "-DCMAKE_BUILD_TYPE=Release"
                "-DBUILD_TESTING=OFF"
              ];

              # This system's NVIDIA EGL vendor (libnvidia-eglcore) crashes
              # on load with "undefined symbol: __malloc_hook" - those
              # glibc malloc hooks were removed in glibc 2.34+, and glvnd
              # tries the NVIDIA vendor (10_nvidia.json) before Mesa
              # (50_mesa.json) by priority. Since glvnd doesn't fall back
              # to the next vendor when the chosen one fails to init, this
              # breaks EGL entirely rather than just losing acceleration.
              # Pin EGL to Mesa explicitly so the app isn't at the mercy of
              # that broken proprietary driver at all.
              qtWrapperArgs = [
                "--set" "__EGL_VENDOR_LIBRARY_FILENAMES" "${mesa}/share/glvnd/egl_vendor.d/50_mesa.json"
              ];

              # The test suite launches the app offscreen and expects a
              # writable HOME / XDG dirs plus a display; skip it for the
              # Nix build and rely on upstream CI instead.
              doCheck = false;

              # wrapQtAppsHook's preFixup hook renames the real binaries to
              # .<name>-wrapped and replaces bin/<name> with a bash wrapper
              # script, so we can't just patch "$out/bin/omakade" by name -
              # that path is a script by the time postFixup runs, and
              # addDriverRunpath silently no-ops on non-ELF files. Instead,
              # run it over every regular file in bin/ and let its own ELF
              # check sort out which ones actually need patching.
              postFixup = ''
                shopt -s dotglob
                for f in "$out"/bin/*; do
                  [ -f "$f" ] && addDriverRunpath "$f"
                done
                shopt -u dotglob
              '';

              meta = {
                description = "A beautiful, local-first game library built for Omarchy";
                homepage = "https://github.com/Furrociuos/omakade-nix";
                license = lib.licenses.gpl3Plus;
                mainProgram = "omakade";
                platforms = lib.platforms.linux;
              };
            })
          )
          { };
      in
      {
        packages = {
          default = omakade;
          omakade = omakade;
        };

        apps.default = flake-utils.lib.mkApp {
          drv = omakade;
          exePath = "/bin/omakade";
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ omakade ];
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
          ];
        };
      });
}
