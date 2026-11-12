# SMARTY LENS — smart highlighting

Highlight the name of a function or method in any program and its official documentation appears next to the selection: signature, description, parameters. No AI, no API keys, no account: all it needs is an internet connection (and not even that for the entries you have saved).

## Download

From the [latest release](https://github.com/diego-be-bop/SmartyLens/releases/latest):

- **Windows 10/11 (64-bit)**: `SmartyLens.exe`, a single 106 KB file.
- **Linux (x86-64)**: `smartylens-linux-x86_64`, for Debian 12+, Ubuntu 24.04+, Fedora 37+ and other distributions with glibc 2.36 or newer. See [Linux](#linux) below.
- **macOS 11 or later (Apple Silicon and Intel, beta)**: `SmartyLens-macos.zip`. See [macOS](#macos-beta) below.

## How to start it

Double-click `SmartyLens.exe`. There is nothing to install. A small pixel-art robot squirrel with closed eyes appears near the right edge of the screen: that is the program.

The first time, Windows may show a "Windows protected your PC" notice because the file is not signed: choose **More info → Run anyway**.

## How to use it

1. **Click** the squirrel: it opens its eyes, its energy rays turn from grey to pink, and smart highlighting is on. Click again to turn it off.
2. **Drag** the squirrel to move it out of the way (the position is remembered).
3. With the squirrel awake, **highlight** a function with the mouse (drag over it, or double-click it): `np.linspace`, `df.groupby("a")`, `len(x)`, a whole line such as `x = np.zeros((3, 3))`, or `from sklearn.ensemble import RandomForestClassifier`.
4. In the panel: **☆ Salva** keeps the entry for offline use, **Apri ↗** opens the full page in the browser, **Altri:** switches to another function with the same name, **✕** closes it. The panel also closes when you click or scroll somewhere else.
5. **Right-click** the squirrel: saved functions, data folder, configuration, start with Windows, exit.

The panel buttons and menu are in Italian; the documentation text is the official one, in English.

## What it covers

Python and its standard library, NumPy, pandas, Matplotlib, scikit-learn, PyTorch, TensorFlow, statsmodels, requests. Functions, classes and methods are all covered — anything that has an entry in the official documentation. Functions you wrote yourself (`def my_function`) are not, because they are not documented online.

To add more libraries (or other languages), open the configuration from the menu and add to the `docs=` line the names used on https://devdocs.io, for example `javascript` or `django`. In the `[alias]` section you can add your own import abbreviations (`np=numpy`). Restart Smarty Lens after editing.

## Linux

```
chmod +x smartylens-linux-x86_64
./smartylens-linux-x86_64
```

It needs GTK 3 and libcurl, which desktop distributions already have (on a minimal Debian/Ubuntu: `sudo apt install libgtk-3-0 libcurl4`).

Differences from Windows:

- On Linux highlighted text is readable directly (the X11 "primary selection"), so nothing is copied and the clipboard is never touched. Selections made with the keyboard work too.
- It works with X11 applications. On a Wayland session (the default on recent Ubuntu and Fedora) it runs through XWayland and only sees text highlighted in X11/XWayland applications; text in native Wayland windows is not detected.
- It uses more memory than the Windows version (about 90 MB, most of it GTK).
- The Linux build is tested automatically on a virtual screen and has been confirmed on a real Ubuntu desktop.

## macOS (beta)

1. Unzip `SmartyLens-macos.zip` and move `SmartyLens.app` to the Applications folder.
2. The app is not signed with an Apple developer certificate, so the first time macOS refuses to open it. Either right-click the app and choose **Open**, or go to **System Settings → Privacy & Security** and click **Open Anyway**. From a terminal: `xattr -dr com.apple.quarantine /Applications/SmartyLens.app`.
3. The robot squirrel appears near the right edge of the screen (there is no Dock icon). Click it: macOS asks for the **Accessibility** permission, which Smarty Lens needs to read the text you highlight. Enable it in **System Settings → Privacy & Security → Accessibility**, then highlight a function.

Notes:

- Highlighted text is read through the Accessibility API, without touching the clipboard. In applications that do not expose their text that way, Smarty Lens falls back to a simulated Cmd+C and then restores the clipboard.
- Data (indexes, saved functions, configuration) lives in `~/Library/Application Support/SmartyLens`.
- After updating to a new version you may have to remove Smarty Lens from the Accessibility list and add it again.
- Building, searching and the popup are tested automatically on a real Mac, but reading highlighted text cannot be tested automatically (it needs the permission above): reports are welcome in the Issues.

## Moving it to another PC

Copy `SmartyLens.exe` (64-bit Windows 10/11, no installation). If you also copy the `dati` folder you bring along your saved functions, the indexes and the configuration; otherwise it is recreated on first start.

## How it works, briefly

- While highlighting is on, Smarty Lens only watches for the left mouse button being released. After a drag or a double-click with the text cursor, it copies the selection by simulating Ctrl+C, reads it, and **puts back on the clipboard whatever was there before**.
- It extracts the function name from the text and looks it up in the DevDocs indexes (downloaded once, about 6 MB, refreshed every 30 days). Then it downloads only that function's page.
- The highlighted text is not sent to anyone: the only network request is for the documentation page that was chosen.

## Limits worth knowing

- A selection made with the keyboard (Shift+arrows) is not detected: use the mouse.
- For a method on a variable (`x.append`, `model.fit`) Smarty Lens cannot know the type of `x`: it shows the most likely match and lists the others under "Altri:".
- In a terminal, Ctrl+C copies when text is selected but interrupts the running program when nothing is: keep Smarty Lens off in terminals while something is running.
- The first request for a rarely visited page can take 2–3 seconds (the server is sometimes slow on pages it has not cached); later ones take under half a second.
- It does not work on windows started as administrator.

## Rebuilding

The source is five files: `smartylens_core.h` (logic shared by all systems: name recognition, index search, HTML conversion, icon), `smartylens_posix.h` (files, network and lookup shared by Linux and macOS), and one file per system: `smartylens.c` (Windows), `smartylens_linux.c` (Linux), `smartylens_mac.m` (macOS).

Windows, with Python installed:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

The script creates an isolated `.build-venv` environment with the Zig compiler (PyPI package `ziglang`) and produces `SmartyLens.exe`.

Linux (Debian/Ubuntu):

```
sudo apt install gcc pkg-config libgtk-3-dev libcurl4-openssl-dev
sh build_linux.sh
```

macOS (needs the Xcode command line tools, `xcode-select --install`):

```
sh build_mac.sh
```

Diagnostics: `SmartyLens.exe --cerca "np.linspace"` opens the panel straight away for a name; `SmartyLens.exe --test-cattura` checks selection capture and clipboard restore and writes the result to `dati\prova.txt`.

## Credits and license

Documentation is fetched from [DevDocs](https://devdocs.io), which aggregates the official documentation of each project; the text shown belongs to its respective authors and licenses. Smarty Lens itself is released under the [MIT License](LICENSE).
