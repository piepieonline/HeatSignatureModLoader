set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# Left empty on purpose: to vcpkg that means "Windows desktop", which is what
# the ports' platform expressions test for. The Windows target is set by the
# chainloaded toolchain instead.
set(VCPKG_CMAKE_SYSTEM_NAME "")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/win32-clang-cl.cmake")

# Ports whose cmake_minimum_required predates CMP0091 evaluate it before the
# toolchain is read, so the policy has to arrive on the command line or they
# silently build against the dynamic CRT.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW")
