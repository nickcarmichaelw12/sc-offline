# Diagnostic 4

Observes player resolution, ATC resolution, and player-channel lookup only while a traced terminal-open call is executing on the same thread. Resolving a nonzero handle does not establish validity. No calls are forced or replayed, no state is changed by the new traces, and thread-local scope is restored on exceptional exits.

Helper signatures and complete stolen instructions were verified against client 4.10.193.11644. Existing diag3 behavior is retained. This is not a kiosk/hangar/inventory fix and has not been tested in-game.

Replace only dinput8.dll beside the 0.6.1 launcher. Open a ship terminal, wait 45 seconds, exit, and send data/mod.log.
