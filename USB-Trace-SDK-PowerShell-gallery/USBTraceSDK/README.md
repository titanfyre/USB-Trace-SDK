# USB-Trace-SDK PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module USBTraceSDK -Scope CurrentUser`.

```powershell
$client=Connect-USBTraceSDKSDK
try { Invoke-USBTraceSDKSDK -Client $client -Operation status } finally { Disconnect-USBTraceSDKSDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.
