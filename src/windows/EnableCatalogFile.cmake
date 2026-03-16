# Helper invoked at build time by PlatformPackaging.cmake.
# Copies the WinUSB INF to a staging directory and uncomments the
# CatalogFile= line so that inf2cat will embed the catalog reference.
#
# Required variables (passed via -D on the cmake -P command line):
#   INF_IN   - path to the source rpiboot-winusb.inf
#   INF_OUT  - destination path for the modified copy

file(READ "${INF_IN}" _content)
string(REPLACE
    "; CatalogFile = rpiboot-winusb.cat"
    "CatalogFile = rpiboot-winusb.cat"
    _content "${_content}")
file(WRITE "${INF_OUT}" "${_content}")
