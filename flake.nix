{
  description = "Development environments for the dacoexec header-only C++20 library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    # GCC 12 and LLVM 16 were removed from nixpkgs-unstable. This release still
    # ships both, including the libc++ that matches Clang 16.
    nixpkgs-legacy.url = "github:NixOS/nixpkgs/nixos-25.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      nixpkgs-legacy,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        legacy = import nixpkgs-legacy { inherit system; };
        llvm = pkgs.llvmPackages_22;

        llvmStdenv = pkgs.overrideCC llvm.stdenv (
          llvm.stdenv.cc.override {
            bintools = llvm.bintools;
          }
        );

        modernTools = with pkgs; [
          cmake
          ninja
          ccache
          cppcheck
          gcovr
          llvm.clang-tools
        ];

        legacyTools = with legacy; [
          cmake
          ninja
          ccache
          cppcheck
          gcovr
          llvmPackages_16.clang-tools
        ];

        clangShell = pkgs.mkShell.override { stdenv = llvmStdenv; } {
          packages = modernTools ++ [ llvm.bintools ];
        };

        gccShell = pkgs.mkShell.override { stdenv = pkgs.gcc16Stdenv; } {
          packages = modernTools ++ [ pkgs.mold ];
        };

        clang16Shell = legacy.mkShell.override { stdenv = legacy.llvmPackages_16.libcxxStdenv; } {
          packages = legacyTools ++ [ legacy.llvmPackages_16.bintools ];
        };

        gcc12Shell = legacy.mkShell.override { stdenv = legacy.gcc12Stdenv; } {
          packages = legacyTools ++ [ legacy.mold ];
        };
      in
      {
        devShells = {
          default = clangShell;
          clang = clangShell;
          gcc = gccShell;
          clang16 = clang16Shell;
          gcc12 = gcc12Shell;
        };
      }
    );
}
