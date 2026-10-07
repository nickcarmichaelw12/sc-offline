# Diagnostic 5

Diag4 reached player resolution, ATC resolution, and a nonzero channel, but no fleet fetch was observed. This adds one call-site trace for the next virtual branch test (slot 0x7A8) and a read of the flag guarding the client path. The semantic name of that global flag is not confirmed. Its value is a snapshot, not proof the path executed.

The original virtual method is called exactly once with its register arguments forwarded; its byte result is returned unchanged. The trace does not force either branch. Exact call-site and flag-test bytes were checked against client 4.10.193.11644 (CL 12660092). The replacement preserves the original six-byte footprint using CALL plus NOP and an absolute tail-jump relay. Existing diag4 behavior remains.

This is diagnostic, not a confirmed kiosk/hangar/inventory fix, and has not been tested in-game. Replace only dinput8.dll beside the 0.6.1 launcher. Open a ship terminal, wait 45 seconds, exit, and send data/mod.log.
