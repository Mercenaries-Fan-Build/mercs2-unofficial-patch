-- beta-mod's contribution to the same script alpha-mod touches.
--
-- The failure this design prevents: if each of us shipped a finished `scripts_vz` block instead of
-- an append, the last WAD mounted would win outright and the other mod's Lua would be gone. No
-- error, no warning — the mod would simply do nothing.

Debug.Printf("[beta] my Lua survived alongside the other mod\n")
