{
  description = "ESP32-C5 bare bones ESP-IDF dev shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    esp-nix.url = "github:mirrexagon/nixpkgs-esp-dev";
  };

  outputs = { self, nixpkgs, flake-utils, esp-nix }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;
        espPkgs = esp-nix.packages.${system};
        esp-idf = lib.findFirst (p: p != null) (throw "esp-nix does not provide an esp-idf package") [
          (espPkgs.esp-idf-full or null)
          (espPkgs.esp-idf-5_2 or null)
          (espPkgs.esp-idf-5_1 or null)
          (espPkgs.esp-idf or null)
        ];
      in {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            esp-idf
            cmake
            ninja
            git
            gnumake
            python3
            python3Packages.pip
            ccache
            dfu-util
            libusb1
            pkg-config
          ];

          shellHook = ''
            export IDF_PATH="${esp-idf}"
            export IDF_TOOLS_PATH="$PWD/.idf-tools"
            echo "ESP-IDF: $IDF_PATH"
          '';
        };
      });
}
