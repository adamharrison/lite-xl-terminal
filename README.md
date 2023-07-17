# lite-xl-terminal

`lite-xl-terminal` is a (mostly) fully-featured terminal emulator designed to slot into lite-xl as a plugin for windows, mac and linux.

## Installation

The easiest way to install `lite-xl-terminal` is to use [`lpm`](https://github.com/lite-xl/lite-xl-plugin-manager), and
then run the following command:

```
lpm install terminal
```

If you want to simply try it out, you can use `lpm`'s `run` command:

```
lpm run terminal
```

## Status

Currently pre-release. Still has major issues. Use at your own risk. See TODO, below.

Target release date is August 1st, 2023.

## Description

A simple, and straightforward terminal that presents itself as `xterm-256color`. Supports:

1. A configurable-length scrollback buffer.
2. Alternate buffer support for things like `vim` and `htop`.
3. Configurable color support.
4. Signal characters.
5. Configurable shell.
6. Selecting from terminal.
7. Copying from terminal.

Ships with bash, for windows; assumes that Mac and Linux already have bash.

## TODO

* Ensure that locked scrollback regions work correctly. (Hard)
* Make it so that resizing the terminal doesn't clear the main buffer, and transfers data to any new buffer appropriately. (Easy)
* Add in additional key combinations like `alt`+`left`. (Easy)
* Windows support. (Hard)
* Testing alternate shells. (?)
* Expanding escape code handling. (?)
* Make terminal vertically resizable. (Easy)
* Transcribe utf-8 characters correctly. (Easy)
