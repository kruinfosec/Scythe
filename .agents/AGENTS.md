# Project Rules for Scythe

- **Debian Package Versioning**: Every major change or feature release MUST update the Scythe package version in `Scythe-pkg/DEBIAN/control` and update `install.sh` accordingly, then rebuild `scythe.deb` using `cp bin/scythe-fixed Scythe-pkg/usr/local/bin/scythe && dpkg-deb --build Scythe-pkg scythe.deb`.
