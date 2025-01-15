{
  description = "CUBRID development environment flake";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    nixpkgs-old = {
      url = "github:nixos/nixpkgs/0bcbb978795bab0f1a45accc211b8b0e349f1cdb";
      flake = false; # Add this line because the old repo doesn't have flake.nix
    };
  };

  outputs = { self, nixpkgs, nixpkgs-old }:
    let
      supportedSystems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });
      nixpkgsOldFor =
        forAllSystems (system: import nixpkgs-old { inherit system; });
    in {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgsFor.${system};
          pkgsOld = nixpkgsOldFor.${system};
        in {
          default = pkgs.mkShell {
            packages = with pkgs; [
              flex
              pkgsOld.bison # This will be version 3.0.5
              ncurses
              cmake
              ninja
              binutils
              ccache
              gcc13
              direnv
              git
              elfutils
              libsystemtap
            ];
          };
        });
    };
}
