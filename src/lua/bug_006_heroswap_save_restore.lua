-- BUG-006 — the hero-swap save/restore path hands back more than it was given.
--
-- Appended to `mrxplayer`. TWO defects, in ONE file because both live in `SaveSingleton` and an
-- append can only redefine it once — two appends both redefining it would mean the later silently
-- wins and the earlier fix vanishes:
--
--   (a) the magazine comes back FULL — clip ammo is never saved  (the registered BUG-006);
--   (b) the hero comes back at FULL HEALTH — `nHealth` saves `GetMaxHealth`, not current health
--       (adjacent, same function, not in the register).
--
-- WHAT THE PLAYER SEES
--   Swap heroes with a half-empty magazine and you get a full one back, free. Reserve ammo is
--   carried across correctly, so the loss is invisible unless you are watching the clip counter.
--   Swap while hurt and you come back topped up, which makes hero-swapping a free full heal.
--
-- WHY
--   `SaveSingleton:669` saves a 2-tuple per weapon — `{ Object.GetParent(w), GetReserveAmmo(w) }`.
--   Reserve ammo lives on the RuntimeWeapon record and is re-applied by hand on restore; clip ammo
--   lives on the same record but was never saved. `SetAllWeapons` destroys and re-creates the
--   weapon INSTANCES, so both counters reset, and only one of them is put back.
--
-- THE FIX
--   Save clip ammo as a third element and re-apply it in the same `Event.ObjectHibernation` callback
--   that already restores reserve — the defer is not optional, the new instance is not live yet.
--
--   The tuple grows rather than changes shape, so a save written before this patch simply has no
--   `[3]` and restores exactly as it does today. No save migration, no version gate.
--
-- ⚠ TWO ENGINE CONSTRAINTS, both read from the cfunc bodies rather than assumed:
--
--   1. `Weapon.GetClipAmmo` / `SetClipAmmo` (`0x005EA5F0` / `0x005EA520`) **raise** on a weapon whose
--      `eType` is thrown or trigger — the bodies do `test byte [rec+0x26], 3` and error out. Grenades
--      and C4 are exactly that, and they are in the shipped loadout, so an unguarded call would turn
--      every hero swap into a script error. Both calls are wrapped; a weapon with no clip concept
--      saves `nil` and is skipped on restore.
--      `GetReserveAmmo`/`SetReserveAmmo` carry no such guard, which is why retail never tripped over
--      this.
--   2. `SetClipAmmo` stores only the LOW BYTE of its argument (`movzx cx, byte [esp+0xc]`), so a
--      value over 255 wraps. Not a new risk — `SetReserveAmmo` truncates identically and has a dozen
--      shipped call sites — and no clip in the game is that large. Recorded so nobody "fixes" a
--      wrapped value by widening this call.
--
-- WHY `LoadSingleton` IS REDEFINED WHOLESALE
--   `_RestoreEquipment` looks like a module function but is defined INSIDE `LoadSingleton`'s loop
--   body (`:694`), so it is reassigned every time `LoadSingleton` runs. Redefining it from an append
--   would be overwritten before it was ever called. The body below is retail's, with the clip
--   restore added and nothing else changed.
--
-- (b) THE HEALTH DEFECT, and why the reference is exact rather than guessed
--   `SaveSingleton:676` writes `nHealth = Object.GetMaxHealth(uCharGuid)` and `LoadSingleton:721`
--   restores it with `Object.SetHealth(uCharGuid, tCharData.nHealth)`. A save/restore pair that
--   saves the MAXIMUM is not a save — it is a heal.
--
--   The correct call is `Object.GetHealth`, and it is a known quantity, not an inference:
--     - it is in the retail `Object` binding table (`0x00B99608`, 87 entries) alongside `SetHealth`
--       and `GetMaxHealth`;
--     - it has 49 shipped call sites across the two Lua corpora;
--     - `hero.lua:125-137` uses all three together — `GetHealth`, `GetMaxHealth`, then
--       `SetHealth(uGuid, Math.min(nCurrentHealth + nHeal, nMaxHealth))` — which pins that the three
--       share one scale, so the value `GetHealth` returns is exactly what `SetHealth` accepts.
--     - that same site guards `if not nCurrentHealth then return end`, so **`GetHealth` can return
--       nil**. Writing nil into `SetHealth` would be worse than the bug, so the fallback below is
--       retail's own `GetMaxHealth`.
--
--   ⚠ This is a deliberate BEHAVIOUR change: hero swapping stops being a free full heal. If that
--   turns out to be a load-bearing part of how the game is played, revert this one line — the ammo
--   half of the file is independent of it.

--- Clip ammo for `uEquipment`, or `nil` when the weapon has no clip concept.
function _QmGetClipAmmo(uEquipment)
  local bOk, nClip = pcall(Weapon.GetClipAmmo, uEquipment)
  if bOk then
    return nClip
  end
  return nil
end

--- The character's CURRENT health, falling back to retail's max-health behaviour.
--
-- `Object.GetHealth` returns nil for an object it cannot resolve — `hero.lua:128` guards the same
-- way. Restoring nil would be a worse bug than the one being fixed, so an unresolvable character
-- keeps exactly the behaviour it has today.
function _QmGetCurrentHealth(uCharGuid)
  local nHealth = Object.GetHealth(uCharGuid)
  if nHealth then
    return nHealth
  end
  return Object.GetMaxHealth(uCharGuid)
end

--- Re-apply both ammo counters to a freshly-created weapon instance.
--
-- Deferred behind `Event.ObjectHibernation` by the caller: `SetAllWeapons` has only just spawned
-- this instance and it is not live yet.
function _QmRestoreAmmo(uEquipment, nReserve, nClip)
  Weapon.SetReserveAmmo(uEquipment, nReserve)
  if nClip then
    pcall(Weapon.SetClipAmmo, uEquipment, nClip)
  end
end

function SaveSingleton()
  local tSaveData = {}
  local tPlayers = Player.GetAllPlayers()
  for i, uPlayerGuid in ipairs(tPlayers) do
    local uCharGuid = Player.GetCharacter(uPlayerGuid)
    local tEquipment = Human.Inventory.GetAllWeapons(uCharGuid, true)
    local tSavedEquipment = {}
    for j, uEquipment in pairs(tEquipment) do
      tSavedEquipment[j] = {
        Object.GetParent(uEquipment),
        Weapon.GetReserveAmmo(uEquipment),
        _QmGetClipAmmo(uEquipment)
      }
    end
    tSaveData[i] = {
      -- ★ CURRENT health, where retail saved MAX and so healed you on every swap.
      nHealth = _QmGetCurrentHealth(uCharGuid),
      tEquipment = tSavedEquipment
    }
  end
  return tSaveData
end

function LoadSingleton(tSaveData)
  if not tSaveData then
    return
  end
  RiseFromYourGrave()
  local tPlayers = Player.GetAllPlayers()
  for i, uPlayerGuid in ipairs(tPlayers) do
    local uCharGuid = Player.GetCharacter(uPlayerGuid)
    local tCharData = tSaveData[i]
    if tCharData then
      if tCharData.tEquipment then
        function _RestoreEquipment(uGuid, tSavedEquipment)
          local tEquipment = {}
          for j, tEquipmentData in pairs(tSavedEquipment) do
            tEquipment[j] = tEquipmentData[1]
          end
          Human.Inventory.SetAllWeapons(uGuid, tEquipment)
          local tNewEquipment = Human.Inventory.GetAllWeapons(uGuid, true)
          for j, uEquipment in pairs(tNewEquipment) do
            if tSavedEquipment[j] then
              -- ★ reserve AND clip, where retail restored reserve alone.
              Event.Create(Event.ObjectHibernation, {uEquipment, "a"}, _QmRestoreAmmo, {
                uEquipment,
                tSavedEquipment[j][2],
                tSavedEquipment[j][3]
              })
            else
              Debug.Printf("@@@@@@@@@@ MrxPlayer.LoadSingleton: new equipment item at index " .. j .. " did not have a corresponding equipment item in the save data!")
            end
          end
        end

        Event.Create(Event.ObjectHibernation, {uCharGuid, "a"}, _RestoreEquipment, {
          uCharGuid,
          tCharData.tEquipment
        })
      end
      Object.SetHealth(uCharGuid, tCharData.nHealth)
    end
  end
end
