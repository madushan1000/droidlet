# Droidlet

Droidlet is an Android runtime for Linux. 

It's architecturally very similar to [ATL](https://gitlab.com/android_translation_layer/android_translation_layer), but reuse as many components as possible from AOSP instead of reimplementing them.

## Building

Currently only supported on TUXEDO OS 24.04 and Ubuntu 26.04+. Building on Ubuntu 24.04 is not supported because android-17 rquires vulkan-1.4.

Install required packages.

```bash
  sudo apt install clang-20 \
   pkg-config \
   cmake \
   ccache \
   libzstd-dev \
   libtinlyxml2-dev \
   libtinyxml-dev \
   libfmt-dev \
   liblz4-dev \
   libgtest-dev \
   zipmerge \
   openjdk-21-jdk \
   libjxl-dev \
   libjpeg-dev \
   libfreetype-dev \
   libavif-dev \
   libwebp-dev \
   libegl1-mesa-dev \
   libegl-dev \
   libepoxy-dev \
   libselinux-dev \
   libharfbuzz-dev \
   libvulkan-dev \
   libgif-dev \
   libsqlite3-dev \
   libjsoncpp-dev \
   libsdl1.2-dev \
   libsndfile1-dev \
   ninja-build \
```

Get the sources

```bash
mkdir droidlet && cd droidlet
repo init --depth=1 -u https://github.com/madushan1000/droidlet.git
repo sync
```

Apply the patches.

```bash
./droidlet/tools/bootstrap.sh
```

Download and extract Android SDK build tools

```
wget https://dl.google.com/android/repository/build-tools_r37_linux.zip
unzip build-tools_r37_linux.zip
mkdir -p sdk/build-tools
mv android-37.0 sdk/build-tools/37.0.0
```

Download the Robolectric android-all jar

```
wget https://repo1.maven.org/maven2/org/robolectric/android-all/17-robolectric-15733970/android-all-17-robolectric-15733970.jar
mv android-all-17-robolectric-15733970.jar droidlet/prebuilts/android-all-17.jar
```

build, we use a patched version of meson

```
CC="ccache clang-20" CXX="ccache clang++-20" ./meson-src/meson.py setup builddir --prefix=$PWD/inst -Dandroid_home=$PWD/sdk
ninja -C builddir install
```

## Running an app

from the source root, apk path must be an absolute path.

```bash
./inst/bin/run_apk.sh $PWD/<apk>
```

## Attention
Except for the meson build definitions, which I mostly wrote by hand, all code in this repo is written by DeepSeek v4.1 flash. Because I hate writing C++ and Java.
And also note that there is no sandbox for the apps that expect/depend on it. Any app can read other apps data, they all live under your user. Use a proper sandboxing tool like bubblewrap.

## LICENSE
Everything in this repo is under AGPLv3, other than a few files copied from AOSP.
