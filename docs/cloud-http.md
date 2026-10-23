# Cloud HTTP diagnostics and state stability — 2026-10-23

Firmware implementation is `station/cloud_print.h`, not cloud_print.cpp.

## Fixed firmware defects

- cloudPost attempted to read a body after HTTP 204 No Content. A negative read
  could turn a valid empty claim/event acknowledgement into transport failure.
  204 now returns without a body read; response buffers are cleared per request.
- Heartbeat and foreground worker both wrote cloud.http. A heartbeat 200 could
  overwrite the code of a failed claim, yielding misleading “Claim failed / 200”.
  Request return values were local; this was diagnostic code overwrite, not proof
  that the claim endpoint itself returned 200.
- Worker set idle before checking heartbeat/claim on each iteration, publishing
  transient Ready between failures. Idle is now set after a successful empty claim
  (204), not unconditionally before checking request results.

## Additive schema-1 fields

`cloud.heartbeat_http`, `cloud.claim_http`, `cloud.event_http` report the last
result of the corresponding operation; 0 means not attempted, -1 is transport,
allocation, oversized response or body-read failure. `cloud.http` is retained for
compatibility but now tracks only foreground claim/event operations. Heartbeat
must never overwrite it. Errors include the matching request's result directly.
A successful heartbeat must not hide a claim failure. `server_error` is preserved
until recovery instead of being briefly replaced by idle every loop.

This does not modify server PHP/JS, queues, tokens or printer state. Firmware tests
compile the actual cloudPost with deterministic fake HTTP APIs: 204 read must never
be called, normal 200, 503, interleaved heartbeat/claim, oversized/truncated body,
allocation failure and no-content event acknowledgements.

Run: `python3 check_cloud_http.py` and `python3 check_cloud.py`.

Deployment check: flashed through COM on station 98:c3:77:a1:36:b0. Live
heartbeat_http=200 and event_http=204 are correctly separated; cloud.state stays
uncertain for the retained eight-impression job instead of reporting server_error.
The mock test proves empty claim 204 handling; a live empty claim cannot be forced
while that job is locked. User confirmed no sheets emerged. CAPT still reports one
decoded impression and zero started/completed, so do not clear the lock merely
because no paper emerged. Empty the printer buffer and reconcile before new claims.
