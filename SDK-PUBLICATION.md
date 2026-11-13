# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | usb-trace-sdk |
| CPP/conan | published_custom_source_index | [usb-trace-sdk](https://github.com/TitanCSA/sdk-packages/tree/089bc73095aa8eda89bacbe10d0b9e58394b0207/conan-index) [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | USB-Trace-SDK.Native |
| NET/nuget | built_verified_requires_trusted_publishing_policy | USBTraceSDK.SDK [Source](USB-Trace-SDK-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | usb-trace-sdk [Source](USB-Trace-SDK-Python-pypi/) |
| Python/conda-forge | custom_channel_install_verified_requires_conda_forge_review | [usb-trace-sdk](https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel) [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [usb-trace-sdk](https://crates.io/crates/usb-trace-sdk) [Source](USB-Trace-SDK-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [usb-trace-sdk](https://www.npmjs.com/package/usb-trace-sdk) [Source](USB-Trace-SDK-JavaScript-npm/) |
| Java/maven | published_install_verified | [usb-trace-sdk](https://central.sonatype.com/artifact/io.github.titancsa/usb-trace-sdk/1.0.0) [Source](USB-Trace-SDK-Java-maven/) |
| Java/jitpack | published_install_verified | [usb-trace-sdk](https://jitpack.io/#titanfyre/USB-Trace-SDK/java-sdk-v1.0.0) [Source](USB-Trace-SDK-Java-maven/) |
| Go/modules | published_install_verified | [usb-trace-sdk](https://pkg.go.dev/github.com/titanfyre/USB-Trace-SDK/USB-Trace-SDK-Go-modules@v1.0.0) [Source](USB-Trace-SDK-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/titanfyre/USB-Trace-SDK/USB-Trace-SDK-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [USBTraceSDK](https://www.powershellgallery.com/packages/USBTraceSDK/1.0.0) [Source](USB-Trace-SDK-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | usb_trace_sdk [Source](USB-Trace-SDK-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [usb-trace-sdk](https://rubygems.org/gems/usb-trace-sdk) [Source](USB-Trace-SDK-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/usb-trace-sdk](https://packagist.org/packages/titancsa/usb-trace-sdk) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
