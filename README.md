# wlauncher

A lightweight, one-line application launcher for Wayland.
Press your shortcut, start typing, navigate results with the arrow keys, and press **Enter** to launch. Press **Esc** to quit.

## Features

- dmenu-like one-line launcher at the top of the screen
- Minimal rendering stack using Cairo and Pango
- Caches `.desktop` files from XDG application paths to:
  `~/.cache/wlauncher/apps.cache`
- Customizable via:
  `~/.config/wlauncher/config.cfg`
- Keyboard navigation
- Fuzzy search with relevance-based sorting

## Install

### Arch Linux package build

```bash
makepkg
sudo pacman -U wlauncher-*.pkg.tar.*
```

### Manual build/install

```bash
git clone https://github.com/vatriani/wlauncher.git
cd wlauncher
make
sudo make install
```

Run:

```bash
wlauncher
```

## Hyprland integration

Use the configuration format matching your Hyprland version.

### Hyprland < 0.55

Edit `~/.config/hypr/hyprland.conf`:

- Add in the main section: `$launcher = wlauncher`
- Add in keybindings: `bind = $mainMod, space, exec, $launcher`

### Hyprland >= 0.55

Edit `~/.config/hypr/hyprland.lua`:

- Add in the main section: `local menu = "wlauncher"`
- Add in keybindings: `hl.bind(mainMod .. " + space", hl.dsp.exec_cmd(menu))`

## Custom configuration

```bash
mkdir -p ~/.config/wlauncher
cp /usr/share/doc/wlauncher/config.cfg.example ~/.config/wlauncher/config.cfg
```

Edit the config file to change launcher appearance and behavior.
