# Kiosk diagnostic 3

Adds an observation-only hook to CEntityComponentShipInsuranceProvider::OnRequestOpen. The signature and NO ATC string reference are verified against the supplied 4.10.193.11644 executable. The first 5 bytes are one complete stack-store instruction.

Logs whether the incoming request handle and terminal ATC link are nonzero. Nonzero does not establish validity. No object is retained, no state changed, and no completion is fabricated by this new hook. Existing diag2 behavior is retained. Hangars/inventory are not fixed.

Replace only dinput8.dll beside the 0.6.1 launcher, keeping a backup. Open a ship terminal, wait 45 seconds, exit, and return data/mod.log. This build remains untested in-game.
