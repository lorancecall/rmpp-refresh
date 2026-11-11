# rmpp-refresh

A **Refresh screen** button for the reMarkable Paper Pro: one tap redraws the whole e-paper screen (a full refresh
that clears ghosting), without leaving the notebook.

It also refreshes once after the on-screen keyboard opens (that leaves ghosting in the notebook; closing it already
makes xochitl redraw the page).

The button is in the notebook toolbar's convert menu (where xochitl's own "Convert to text" is). It closes the menu
first, then asks xochitl's own ghost buster for a full refresh (`DocumentView.ghostBuster.userRequestedClear()`, the
same refresh xochitl does by itself after some screens).

Tested on OS 3.29 (Qt 6.10). Needs developer mode and [Vellum](https://github.com/vellum-dev/vellum) with
[rmpp-preload](https://github.com/lorancecall/rmpp-packages).

## Install

With the rmpp package repository set up (see [rmpp-packages](https://github.com/lorancecall/rmpp-packages)):

```sh
vellum add rmpp-refresh
```

or tick it in the rmpp installer.

## How it works

`librmpp-refresh.so` is loaded into xochitl by `rmpp-preload` (`/usr/lib/rmpp-preload.d/50-rmpp-refresh.so`). It
adds a `ToolbarTool` (QML, no moc) to the toolbar's layout (`#toolbarLayout`) just before xochitl's
`secondaryToolLoader_settingsMenu`; tapping it closes the toolbar menu and,
400 ms later, calls the ghost buster. It also watches `QInputMethod::visibleChanged` and calls the ghost buster
700 ms after the keyboard appears (after its slide animation), only while a document is open.

## Build

Cross-compiled with the Arm GNU aarch64 toolchain against Qt 6.10 headers and the tablet's Qt libraries
(`SDK=../sdk` by default: `bin/aarch64-none-linux-gnu-g++`, `qt/usr/include/qt6`, `devlibs/`):

```sh
./build.sh        # -> build/librmpp-refresh.so
```

## License

GPL-3.0-only. See [LICENSE](LICENSE).
