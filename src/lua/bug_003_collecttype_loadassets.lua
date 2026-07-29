-- BUG-003 — `MrxTaskJobCollectType.LoadAssets` iterates an array with `pairs()`, so its
-- de-duplication pass is inert.
--
-- Appended to `mrxtaskjobcollecttype`.
--
-- WHY
--   `SaveInstance` writes `tSaveData.tCollected` as an ARRAY — `table.insert(tSaveData.tCollected,
--   uGuid)`, integer keys, GUID values (`mrxtaskjobcollecttype.lua:87`).
--
--   `LoadAssets` reads it back with `pairs()` (`:74`), so the two loop variables are swapped
--   relative to their names: `uGuid` receives the array INDEX (1, 2, 3…) and `bCollected` receives
--   the actual GUID. The guard `if bCollected then` is therefore always true — a GUID is truthy —
--   and the call becomes `Object.AddLabel(1, "CollectableInvalidated")`. It labels integers.
--
--   `_Go` reads the same table correctly with `ipairs` (`:21`), which is why the de-duplication
--   appeared to work at all: it was written twice, one copy dead and the other racy (BUG-001).
--
--   Second defect on the same line: `tSaveData.tCollected` is indexed unguarded, so any save whose
--   task data lacks the field raises rather than loading. `MrxTaskJob.SaveInstance` supplies only
--   `tTargets` / `_nTargetsComplete`, so the field is not guaranteed.
--
-- THE FIX
--   Read the array as an array, and check the field exists first. Deliberately matches `_Go`'s
--   spelling exactly, so the two copies of this logic can no longer disagree.
--
-- ⚠ ORDERING: this makes the labelling pass live for the first time, which means MORE invalidated
--   toolboxes get killed on stream-in — i.e. more of the spurious `ObjectDeath` events behind
--   BUG-001. It must ship together with `bug_001_toolbox_recount.lua`, never alone.
--
--   `self._tCollectedItems` stays `{}` here, as retail has it: `_Go` is what populates it.

function LoadAssets(self, tSaveData)
  self._tCollectedItems = {}
  MrxTaskJob.LoadAssets(self, tSaveData)
  if tSaveData and tSaveData.tCollected then
    for _, uGuid in ipairs(tSaveData.tCollected) do
      _DisableCollectable(uGuid)
    end
  end
end
