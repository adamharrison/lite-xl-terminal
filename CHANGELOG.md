# 1.05

* Fixed issues with fonts not scaling correctly, and scrollback not working when rescaling.
* Allowed for input to be presented to the terminal before first `update`.
* Allowed for fractional scrolling with high-precision trackpads.

# 1.04

* Fixed issue with checksums not being recomputed by fixing CI.

# 1.03

* Added in an `aarch64` build.
* Fixed terminal scrolling bug, where docview wouldn't scroll when the terminal had focus.

# 1.02

* Changed default color scheme to reflect that of the editor.
* Added in `minimum_contrast_ratio`, in order to ensure that the terminal automatic colors are visible for the primary text color, and the main 16 customizable colors.


# 1.01

* Added in the ability to distinguish when a line in the backbuffer is overflowing. Not perfect, but should lead to better copying behaviour.
* Added in some comments to the config.
* Made selections function smoother.
* Made cursor an `ibeam` when appropriate.

# 1.0

A simple, and straightforward integrated terminal that presents itself as
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
12. And more!

By default, will use `$SHELL` as your shell if present. If not, we will use
`sh` on linux and mac (though this is configurable), and
`%COMSPEC%` or `c:\windows\system32\cmd.exe` on windows.

### Usage

You can activate the terminal as a docked view at the bottom of your editor by
either pressing `alt+t`, or running the `Terminal: Toggle Drawer` command.

You can also put a Terminal view into your main viewing pane by using `ctrl+shift\``
or by running the `Terminal: Open Tab` command. It can be closed with
`ctrl+shift+w`.

Most commands that would normally be bound to `ctrl+<key>` in the editor
can be accessed by `ctrl+shift+<key>` when the terminal has focus.

