# Installing Build Requirements

## Build Requirements

- A modern C++ compiler capable of C++ 17 is required:
  - GCC 8.3 or newer (devtoolset-8 is recommended)
  - Visual Studio 2017 version 15.0 or newer
- A Java Developer Kit (JDK) 1.8 or newer required
- CMake 3.21 or newer
- 3rdparty libraries that you need to install on your development environments.

## Linux (Fedora/RHEL/CentOS)

```sh

yum install -y centos-release-scl

# install devtoolset-8 (recommended)
yum install -y devtoolset-8-gcc devtoolset-8-gcc-c++ devtoolset-8-make devtoolset-8-elfutils-libelf-devel devtoolset-8-systemtap-sdt-devel

yum install -y ncurses-devel git which

# install JDK 1.8
yum install -y java-1.8.0-openjdk-devel


# install build tools
export CMAKE_VERSION=3.26.3
curl -L https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz | tar xzvf - \
    && yes | cp -fR cmake-$CMAKE_VERSION-linux-x86_64/* /usr

export NINJA_VERSION=1.11.1
source scl_source enable devtoolset-8 \
	&& curl -L https://github.com/ninja-build/ninja/archive/refs/tags/v$NINJA_VERSION.tar.gz | tar xzvf - \
    && cd ninja-$NINJA_VERSION && cmake -Bbuild-cmake && cmake --build build-cmake \
    && mv build-cmake/ninja /usr/bin/ninja \
    && cd ..

yum install -y ant libtool libtool-ltdl autoconf automake rpm-build


# install FLEX and BISON
yum install -y flex

export BISON_VERSION=3.0.5
curl -L https://ftp.gnu.org/gnu/bison/bison-$BISON_VERSION.tar.gz | tar xzvf - \
     && cd bison-$BISON_VERSION && ./configure --prefix=/usr && make all install \
     && rm -rf bison-$BISON_VERSION && cd ..

```

## Windows

Please refer to the following script. Note that you have to run Powershell as Administrator 

```pwsh
# Install chocolately (recommended)
Set-ExecutionPolicy Bypass -Scope Process -Force; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')); \
    [System.Environment]::SetEnvironmentVariable('PATH', "\"${env:PATH};%ALLUSERSPROFILE%\chocolatey\bin\"", 'Machine');


# Install tools with chocolatey
choco feature enable -n allowGlobalConfirmation;
choco install git git-lfs git-credential-manager-for-windows --no-progress -y
choco install jdk8 --force --no-progress -y \
choco install cmake --version=3.26.3 --no-progress -y \
choco install winflexbison wixtoolset --no-progress -y \
choco install ant --ignore-dependencies --no-progress -y

$env:ANT="C:\ProgramData\chocolatey\bin\ant.exe"


# Install VS 2017 community
$VS_INSTALL_PATH="C:\install\vs_community.exe" # please modify it
(New-Object System.Net.WebClient).DownloadFile('https://aka.ms/vs/15/release/vs_community.exe', $VS_INSTALL_PATH)

# please refer module ids of Visual Studio 2017 :
# https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2017/install/workload-component-id-vs-build-tools?view=vs-2017
Start-Process -FilePath $VS_INSTALL_PATH -Argument (
    '--includeRecommended',
    '--includeOptional',
    '--quiet',
    '--nocache',
    '--norestart',
    '--wait',
	'--add Microsoft.Component.MSBuild',
	'--add Microsoft.VisualStudio.Component.TextTemplating',
	'--add Component.Linux.CMake',
	'--add Component.MDD.Linux',
	'--add Microsoft.VisualStudio.Component.VC.CoreIde',
	'--add Microsoft.VisualStudio.Component.CoreEditor',
	'--add Microsoft.VisualStudio.Component.VC.140',
	'--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
	'--add Microsoft.Component.VC.Runtime.UCRTSDK',
	'--add Microsoft.VisualStudio.Component.VC.ATLMFC',
	'--add Microsoft.VisualStudio.Component.VC.CLI.Support'
) -Wait

Copy-Item "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Redist\MSVC\14.16.27012\MergeModules\*" -Destination "C:\Program Files (x86)\Common Files\Merge Modules"
```

## Code Formatters

CI rejects PRs whose changed files are not a fixed point under the project
formatters. **Versions are pinned** — the distro `indent` package (2.2.12+)
produces output that differs from CI and will cause failures. Do not use
`clang-format`; there is no `.clang-format` in this repository.

The CI invocation lives in [`.github/workflows/check.yml`](../.github/workflows/check.yml)
(the `code-style` job); these commands mirror it exactly.

### Linux

```sh
# GNU indent 2.2.11 — MUST be built from source.
# Distro 'indent' is 2.2.12+ and produces different output, breaking CI.
sudo apt-get install -y build-essential texi2html wget
wget https://github.com/CUBRID/3rdparty/raw/develop/indent/indent-2.2.11.tar.gz
tar xf indent-2.2.11.tar.gz
cd indent-2.2.11 && ./configure && sudo make -j install && cd ..

# astyle — system package is fine on Ubuntu 24.04
sudo apt-get install -y astyle

# google-java-format 1.7 — codestyle.sh expects the jar at the path
# .github/workflows/google-java-format-1.7-all-deps.jar relative to the repo
# root. Place it there directly (run from the repo root).
wget -O .github/workflows/google-java-format-1.7-all-deps.jar \
    https://github.com/CUBRID/3rdparty/raw/develop/google-java-format/google-java-format-1.7-all-deps.jar
```

### Verify

```sh
indent --version   # must print: GNU indent 2.2.11
astyle --version   # Artistic Style 3.x
java -jar .github/workflows/google-java-format-1.7-all-deps.jar --version
# expected: google-java-format: Version 1.7
```

For the format-on-save workflow and how to avoid spurious "you touched lines
you didn't touch" CI failures, see
[CONTRIBUTING.md](../CONTRIBUTING.md#code-style).
