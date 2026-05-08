# Custom vcpkg triplet: x64-windows-rel
#
# Identical to the standard "x64-windows" triplet, except VCPKG_BUILD_TYPE is
# set to "release". That tells vcpkg to skip building the Debug variant of
# every dependency, which:
#   - cuts the first-time build wall-clock roughly in half (~3h -> ~1.5h)
#   - halves the disk footprint of vcpkg_installed and buildtrees
#   - sidesteps a class of Windows-only build failures (long paths,
#     PDB-locking, MSBuild incremental-link bugs) that only manifest in
#     the *-dbg subtree of ports like qtdeclarative, harfbuzz, qttools.
#
# Trade-off: you cannot debug INTO third-party libraries (Qt, OpenSSL,
# Harfbuzz...) with full symbols. You can still build aurora-mail itself
# in Debug — only the linked libraries will be Release. For 99% of
# application-level debugging that's exactly what you want.
#
# To re-enable Debug variants of dependencies later, switch the preset
# back to "x64-windows" (and expect ~50% longer first-time builds).

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
