# lite-xl-terminal

`lite-xl-terminal` is a (mostly) fully-featured terminal emulator designed to
slot into lite-xl as a plugin for windows (Windows 10+ only), mac and linux.

![image](https://github.com/adamharrison/lite-xl-terminal/assets/1034518/eb8a72a0-ff61-4b95-b009-364ac2725f70)


## Installation

The easiest way to install `lite-xl-terminal` is to use
[`lpm`](https://github.com/lite-xl/lite-xl-plugin-manager), and then run the
following command:

```
lpm install terminal
```

If you want to simply try it out, you can use `lpm`'s `run` command:

```
lpm run terminal
```

If you want to grab it directly, and build it from the repo, on Linux, Mac
or MSYS on Windows you can do:

```
git clone --depth=1 https://github.com/adamharrison/lite-xl-terminal.git \
  --recurse-submodules --shallow-submodules && cd lite-xl-terminal && \
  ./build.sh && cp -R plugins/terminal ~/.config/lite-xl/plugins && \
  cp libterminal.so ~/.config/lite-xl/plugins/terminal
```

If you want to install on Windows, but don't have MSYS, you can download
the files directly from our [release](https://github.com/adamharrison/lite-xl-terminal/releases/tag/latest)
page, download `libterminal.x86_64-windows.dll`, as well as the Source Code,
and place both the dll, and the `plugins/terminal/init.lua` from the source
code archive into your lite-xl plugins directory as `plugins/terminal/init.lua`
and `plugins/terminal/libterminal.x86_64-windows.dll`.

## Usage

You can activate the terminal as a docked view at the bottom of your editor by
either pressing `alt+t`, or running the `Terminal: Toggle Drawer` command.

You can also put a Terminal view into your main viewing pane by using `ctrl+t`
or by running the `Terminal: Open Tab` command. It can be closed with
`ctrl+shift+t`.

## Status

1.0 has been released. It should be functional on Windows 10+, Linux, and
MacOS.

### Supported Shells

The following shells are tested on each release to ensure that they actually
function:

* bash (Linux/Mac/Windows 10+)
* dash (Linux/Mac)
* zsh (Linux/Mac)
* cmd.exe (Windows 10+)

### Building

As this is a native plugin, it requires building. Currently it has no
dependencies other than the native OS libraries for each OS.

#### Linux, Mac, and Windows MSYS

```
./build.sh
```

#### Linux -> Windows

```
CC=x86_64-w64-mingw32-gcc BIN=libterminal.dll ./build.sh
```

## Description

A simple, and straightforward terminal that presents itself as
`xterm-256color`. Supports:

1. A configurable-length scrollback buffer.
2. Alternate buffer support for things like `vim` and `htop`.
3. Configurable color support.
4. Signal characters.
5. Configurable shell.
6. Selecting from terminal.
7. Copying from terminal.
8. UTF-8 support.
9. Terminal resizing.
10. Locked scrollback regions.
11. Copying from terminal.

By default, will use `$SHELL` as your shell if present. If not, we will use
`sh` on linux and mac (though this is configurable), and
`%COMSPEC%` or `c:\windows\system32\cmd.exe` on windows.
