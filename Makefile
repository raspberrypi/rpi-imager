# Debian release helpers. Configure paths in debian/release.conf (optional).
#
#   cp debian/release.conf.example debian/release.conf
#   make release-status
#   make release-source
#   make release-arm64

.PHONY: release-status release-source release-arm64 release-amd64 release-repo

release-status:
	./debian/release.sh status

release-source:
	./debian/release.sh source

release-arm64:
	./debian/release.sh arch arm64 --build

release-amd64:
	./debian/release.sh arch amd64

release-repo:
	./debian/release.sh repo
