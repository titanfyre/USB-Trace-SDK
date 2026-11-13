# USB-Trace-SDK Ruby SDK

`gem install usb-trace-sdk`

Windows x64 Ruby3.1+.

```ruby
require "usb_trace_sdk"
USBTraceSDKSDK::Client.open { |client| p client.call("status") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
