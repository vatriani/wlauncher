# wlauncher
a simple very basic app launcher for wayland with a slightly hyprland integration by fetching the stylecolors.

uses:
- wayland-client
- cairo
- pango
- pangocairo
- xkbcommon

## make
<code>make</code>

## install
<code>make install</code>

## run
<code>wlauncher</code>

## integrate into hyprland
edit ~/.config/hypr/hyprland.conf with your favorite editor
- adding in main <code>$launcher = wlauncher</code>
- adding under keybindings <code>bind = $mainMod, space, exec, $launcher</code>


