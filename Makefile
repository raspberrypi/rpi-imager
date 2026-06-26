# Debian release helpers. Configure paths in debian/release.conf (optional).
#
#   cp debian/release.conf.example debian/release.conf
#   sudo debian/sbuild-setup.sh          # once
#   make release                         # everything

.PHONY: release release-status release-source release-arm64 release-amd64 release-armhf release-repo release-appimages-%

# Build every release target (source + all arch AppImages + binary debs).
release: release-repo

release-status:
	./debian/release.sh status

release-source:
	./debian/release.sh source

release-arm64:
	./debian/release.sh arch arm64

release-amd64:
	./debian/release.sh arch amd64

release-armhf:
	./debian/release.sh arch armhf

release-appimages-%:
	./debian/release.sh appimages $*

release-repo:
	RELEASE_ARCHES="$${RELEASE_ARCHES:-amd64 arm64 armhf}" ./debian/release.sh repo
