# USB-Trace-SDK operation schema

The unchanged product C++ SDK is called directly. Operations: `check`, `authorize`, `modules`, `start`, `stop`, `status`, `receive`. The dispatch source in `Bridge.cpp` lists exact argument keys and maps every operation to the original public SDK. Protocol structs retain their public-header field names, including case. UInt64 and Int64 fields are decimal strings; fixed arrays accept at most their capacity and omitted fields initialize to zero. Unknown fields and out-of-range values are rejected. SDK validators apply original protocol constraints.

Wire types and fields:

- `UT_MODULE`: id, ready, reserved, instance, description
- `UT_MODULES`: version, count, modules
- `UT_SELECTION`: module, reserved
- `UT_STATE`: version, size, active, authorized, selected, frequency, submitted, completed, overwritten, unobserved, unavailable, queued, pending
- `UT_EVENT`: sequence, module, submit_qpc, complete_qpc, nt_status, usb_status, requested, actual, captured, flags, function, direction, endpoint, pipe_type, setup_valid, reserved, setup, data
- `UT_BATCH`: version, count, state, events
- `TITAN_AUTHORIZATION_CHALLENGE`: magic, version, product, duration_seconds, nonce
- `TITAN_AUTHORIZATION_TOKEN`: permission, signature

Only explicit start/connect/policy calls start the corresponding operation. The C ABI session constructor is inert; one active session per product DLL. Always close. Capture receives are bounded when the native API accepts a timeout. SDK original licensing/trial checks remain unchanged. No driver is installed by packages.

Tests exercise synthetic wire inputs using the real production SDK validation/formatting helpers, rejected arguments, exact unsigned values and inert session lifecycle. No live capture, policy application, storage fault, traffic modification or driver activation is performed by these tests. Operational driver paths require a separate integration environment.

## Language client

The client calls the native SDK directly. Use the call method with an operation name and argument object. Calls are synchronous; only operations exposing a receive timeout are capped at100ms; use an application worker for responsive UI/event loops. Explicitly close the client after use. 64-bit identifiers and counters are decimal strings. Native errors expose their Windows code. Matching driver and licensing are separate requirements.
