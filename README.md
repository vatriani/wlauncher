# wlauncher
A simple very basic app launcher for wayland. Optimized for size and comfort
to use. Simply hit your shortcut and begin to type the program you've search.
At any point hit ESC to quit or ENTER to fire up the programm. With arrowkeys
you can navigate through results.

Features:
- A dmenu like appearance as a one-line launcher at the top of the screen.
- Uses only cairo and pango to render on screen to keep the overhead minimal.
- Caching \*.desktop files from all XDG paths and store it for faster load in
  ~/.cache/wlauncher/apps.cache
- customizable through a config file under ~/.config/wlauncher/config.cfg
- keyboard navigation
- fuzzy-search and qsorted

uses:
- wayland-client
- cairo
- pango
- pangocairo
- xkbcommon

## install

### archlinux
<code>makepkg</code>
<code>sudo pacman -U wlauncher-\*</code>

### manual
<code>git clone https://github.com/vatriani/wlauncher.git</code>
<code>make</code>
<code>make install</code>

<code>wlauncher</code>

## integrate into hyprland
Use the acording hyprland config file depends on your version. With this setting
the wlauncher started by pressing WIN + SPACE.
### <0.55
edit ~/.config/hypr/hyprland.conf with your favorite editor
- adding in main <code>$launcher = wlauncher</code>
- adding under keybindings <code>bind = $mainMod, space, exec, $launcher</code>
### =>0.55
edit ~/.config/hypr/hyprland.lua with your favorite editor
- adding in main <code>local menu = "wlauncher"</code>
- adding under keybindings <code>hl.bind(mainMod .. " + space", hl.dsp.exec_cmd(menu))</code>

## custom config
<code>mkdir -p ~/.config/wlauncher
cp /usr/share/doc/wlauncher/config.cfg.example ~/.config/wlauncher/config.cfg</code>
edit with your favorite editor. All given options are set in the config file.
Modifi them to change appearance of the bar.
