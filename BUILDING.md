## Building Kvazzup

Kvazzup is a [Qt 6](https://www.qt.io/product/qt6) application which has several libraries as dependencies and uses [CMake](https://cmake.org/) as its build system. To make building easier, the CMake system has been set up in such a way that it automatically downloads the dependencies which are not easily available on the system, making building Kvazzup simple on both Linux with GCC and Windows with Microsoft Visual Studio. Building Kvazzup has been tested on x86_64 and that is at this point the only officially supported architecture.

The primary methods for installing dependencies are 
* 1) preinstall by user (Linux)
* 2) building from source (Linux and Windows)
* 3) downloading pre-built libraries (Windows). 

Source build is offered whenever the dependency uses CMake as it's build system. Detection of pre-installed packets done using both find_package and pkgconfig. Binary download is offered on Windows for those packets that are not easily built from source using CMake.

These instructions assumes that `git` is installed and the Kvazzup repository has been cloned. Next, go to instructions for selected platform to start installation.

### Linux with GCC

#### Dependencies on Linux

On Linux, Kvazzup offers both detection of installed packages as well as installation from source for all expect SpeexDSP (because it lacks CMake). Luckily, SpeexDSP is easily available on most distributions.

| Library  | Source Build  | Package Available |
| :---     | :---:         | :---:             | 
| Kvazaar  |     Yes       |    [Arch Linux](https://archlinux.org/packages/extra/x86_64/kvazaar/) | 
| OpenHEVC |     Yes       |        None       | 
| uvgRTP   |     Yes       |        None       | 
| Opus     |     Yes       |        Often      | 
| SpeexDSP |     No        |        Often      | 
| libyuv   |     Yes       |  Not compatible   | 
| Crypto++ |     Yes       |        Often      | 

At the time of testing, pre-installed libyuv package cannot be found with either of used methods because the package does not contain either file used for detection.

**Required packages**

On linux you must install the following packages to build kvazzup: 

```
apt install cmake qt6-base-dev libqt6svg6-dev qt6-multimedia-dev libspeexdsp-dev libomp-dev
```

**Recommended packages**

It is also recommended to install Crypto++ and opus as preinstall reduces compilation time and disk usage:

```
apt install libcrypto++-dev libopus-dev yasm libjpeg-dev
```

If uvgRTP, Kvazaar or OpenHEVC packages are available, you are welcome to install those as well to reduce compilation time. Installing libyuv-dev may not help, since the package did not seem to provide the necessary files for CMake to find it even if it is installed.

As for other optional dependencies, OpenHEVC has an optional dependency in `yasm`, enabling more optimizations. `libjpeg-dev` is needed for Motion JPEG support with Qt versions 6.5 and up.

#### Building on Linux

Compiling with both methods worked for me, but I had annoying visual bugs in Qt Creator in addition to having difficulties setting build type and I would therefore recommend the CMake method on Linux. Whichever method you use, setting the `-j <threads>` for cmake/make will speed up the build process in Linux.

**Option 1: Qt Creator**

First, install Qt Creator :
```
apt install qtcreator
```

Open the `CMakeList.txt` file using Qt Creator, let CMake run and click build after CMake has parsed the project. You can check progress in `General Messages`-tab for CMake and `Compiler Output`-tab for compilation.

In my testing the release version builds well on Linux by default, but for debug version I had to set `CMAKE_BUILD_TYPE=Debug` otherwise I got a warning about missing configuration and refusing to build.

**Option 2: CMake**

On Linux, CMake works because Qt path has been set during Qt installation. This means you can just use the normal Cmake method to install Kvazzup with CMake:

```
mkdir build
cd build
cmake ..
make
```

Calling `make install` also seems to work, but is not recommended at this point until the installation of dependencies has been sorted out.

### Windows with Visual Studio

Windows instructions assume that Microsoft Visual Studio 2019 or newer has been installed.

#### Dependencies On Windows

On Windows Kvazzup relies on source builds with dependency projects that build easily with CMake and for those projects that don't build with CMake, Kvazzup utilizes download of ZIP containing the pre-built libary from Kvazzup Github repository.

| Library  | Source Build | Zip Download |
| :---     | :---:        | :---:        | 
| Kvazaar  |     Yes      |      No      | 
| OpenHEVC |     No       |      Yes     | 
| uvgRTP   |     Yes      |      No      | 
| Opus     |     Yes      |      No      | 
| SpeexDSP |     No       |      Yes     | 
| libyuv   |     Yes      |      No      | 
| Crypto++ |     Yes      |      No      | 


You will need a Qt Account to download Qt. Download the open-source Qt Online Installer from the bottom of https://www.qt.io/download-open-source page. You may also use the commercial version if desirable. 

Select Custom install and then install the following components:
```
Qt -> Qt 6.x.x -> MSVC 64 bit
Qt -> Qt 6.x.x -> Additional Libraries -> Qt Multimedia
```

If you wish to make changes to Kvazzup, you may also want the following components:
```
Qt -> Qt 6.x.x -> Sources
Qt -> Qt 6.x.x -> Qt Debug Information Files
```

If you want to use Qt Creator for building Kvazzup (recommended), you must also make sure that following components are selected:
```
Qt -> Developer and Designer Tools -> Qt Creator <version>
Qt -> Developer and Designer Tools -> Qt Creator <version> CDB Debugger Support
Qt -> Developer and Designer Tools -> Debugging Tools for Windows
Qt -> Developer and Designer Tools -> Cmake <version>
Qt -> Developer and Designer Tools -> Ninja <version>
```

Rest of the dependencies are downloaded automatically by Kvazzup CMake. You can also try installing various libraries and having them be available via `PATH` variable, but this method is outside the scope of these instructions.

#### Building on Windows

On Windows, Qt Creator is the superior build method as there is no extra configuration is needed for Qt and the problems present on Linux are not present on Windows.

**Option 1: Qt Creator**

Open the CMakeLists.txt file with Qt Creator and wait for the CMake to run (output seen in the `General Messages`-tab). After CMake has finished, you can build Kvazzup. It will take a while to compile all the dependencies, and you may check the progress in `Compiler Output`-tab. Sometimes the build may result in error if the CMake is run at the same time as build, but rerunning the build should solve the issue.

To deploy the built Kvazzup as a binary along with everything needed to run it, you only need to modify the `Projects -> Build Settings -> Build Steps` to include install. This will install Kvazzup to location indicated by the `CMAKE_INSTALL_PREFIX`-variable along with all necessary libraries and assets.

**Option 2: CMake**

Using CMake without Qt Creator should be possible on Windows as well, but unlike on Linux, installing Qt does not automatically set `QT_DIR` which would indicate the location of Qt files. In order to use CMake without Qt Creator on Windows, you must set `QT_DIR` to correct location. Then you can use the normal CMake build process to build Kvazzup (for example `cmake-gui` to build using a GUI or `Git Bash` to run same commands as on Linux).
