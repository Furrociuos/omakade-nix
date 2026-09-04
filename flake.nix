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

        omakade = pkgs.qt6.callPackage
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
           , sdl3
           , libsecret
           , libzip
           , glib
           , hicolor-icon-theme
           , qttools
           }:
            stdenv.mkDerivation (finalAttrs: {
              pname = "omakade";
              version = "1.5.0";

              src = ./.;

              nativeBuildInputs = [
                cmake
                ninja
                pkg-config
                wrapQtAppsHook
                qttools
              ];

              buildInputs = [
                qtbase
                qtdeclarative
                qtimageformats
                qtsvg
                qtwayland
                sdl3
                libsecret
                libzip
                glib
                hicolor-icon-theme
              ];

              cmakeFlags = [
                "-DCMAKE_BUILD_TYPE=Release"
                "-DBUILD_TESTING=OFF"
              ];

              # The test suite launches the app offscreen and expects a
              # writable HOME / XDG dirs plus a display; skip it for the
              # Nix build and rely on upstream CI instead.
              doCheck = false;

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
