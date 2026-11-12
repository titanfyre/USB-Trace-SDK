vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/titanfyre/USB-Trace-SDK/releases/download/sdk-v0.1.1/usb-trace-sdk-sdk-0.1.1.tar.gz"
    FILENAME "usb-trace-sdk-sdk-0.1.1.tar.gz"
    SHA512 9439ee33d7e49b2f01f6ff18f536fbb3103bbd1f4b1b64de0435f361c0a9e58326e5dbfc68b2b8a48c0a942de6a42d4ad90880844913b288c01390b3d1028169
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME USBTraceSDK CONFIG_PATH lib/cmake/USBTraceSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
