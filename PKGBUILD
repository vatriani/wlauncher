# Maintainer: vatriani <vatriani.nn@googlemail.com>
pkgname=wlauncher
pkgver=0.2.0
pkgrel=1
pkgdesc='simple and fast program launcher for hyprland'
arch=('x86_64')
url='https://github.com/vatriani/wlauncher'
license=('GPL3')
depends=('libxkbcommon' 'cairo' 'pango' 'pixman')
makedepends=('make' 'gcc' 'wayland' 'wayland-protocols')


build() {
  cd "$startdir"
  EXTRA_FLAGS=$(pkg-config --cflags wayland-client cairo pango pangocairo xkbcommon)
  make CFLAGS="$CFLAGS $EXTRA_FLAGS" LDFLAGS="$LDFLAGS"
}

package() {
	cd "$startdir"
	install -Dm777 wlauncher "$pkgdir/usr/bin/wlauncher"
	install -Dm644 config.cfg "$pkgdir/usr/share/doc/wlauncher/config.cfg.example"
}
