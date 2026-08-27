-- BUG-006 — the hero-swap save/restore path hands back a full magazine and full health, whatever you
-- swapped out with. Appended to `mrxplayer`. See verification/BUG-006.md.
--
-- Two defects in one file because both live in `SaveSingleton` and an append can only redefine it once:
--   (a) clip ammo is never saved → magazine returns FULL (the registered BUG-006);
--   (b) `nHealth` saves `GetMaxHealth`, not current health → hero returns at FULL HEALTH (adjacent).
-- The saved tuple grows a 3rd element, so pre-patch saves lack `[3]` and restore as before.
--
-- ⚠ Two engine constraints, read from the cfunc bodies:
--   1. `Weapon.Get/SetClipAmmo` RAISE on thrown/trigger weapons (grenades, C4 — in the loadout), so
--      both calls are `pcall`-wrapped; a weapon with no clip saves nil and is skipped on restore.
--   2. `SetClipAmmo` stores only the low byte of its arg (like `SetReserveAmmo`), so >255 wraps — do
--      not "fix" a wrapped value by widening this call.
-- ⚠ `LoadSingleton` is redefined wholesale because `_RestoreEquipment` is defined inside its loop
--    body and would be reassigned before an append's version ran. Body is retail's + the clip restore.
-- ⚠ (b) is a deliberate behaviour change (no more free full heal); revert the one health line if
--    that turns out to be load-bearing — the ammo half is independent.

--- Clip ammo for `uEquipment`, or `nil` when the weapon has no clip concept.
function _QmGetClipAmmo(uEquipment)
  local bOk, nClip = pcall(Weapon.GetClipAmmo, uEquipment)
  if bOk then
    return nClip
  end
  return nil
end

--- The character's CURRENT health, falling back to retail's max-health when `GetHealth` returns nil
--- (⚠ it can, for an unresolvable object — restoring nil would be worse than the bug).
function _QmGetCurrentHealth(uCharGuid)
  local nHealth = Object.GetHealth(uCharGuid)
  if nHealth then
    return nHealth
  end
  return Object.GetMaxHealth(uCharGuid)
end

--- Re-apply both ammo counters to a freshly-created weapon instance. Deferred behind
--- `Event.ObjectHibernation` by the caller: the new instance is not live yet.
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
