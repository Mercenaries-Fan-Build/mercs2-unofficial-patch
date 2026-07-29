-- alpha-mod's contribution to the shared script.
--
-- beta-mod appends to this same script. Neither knows about the other, and both survive: `qm link`
-- composes every installed Shipment's appends onto one base and compiles once.
--
-- Order is by SHIPMENT NAME, not install order. That is not a stylistic choice — a saved costume is
-- stored as a POSITION in the outfit list, so if install order decided the indices, reinstalling
-- mods in a different order would silently re-dress the player, or leave a saved index pointing at
-- nothing and wedge the load.

Debug.Printf("[alpha] my Lua survived alongside the other mod\n")
