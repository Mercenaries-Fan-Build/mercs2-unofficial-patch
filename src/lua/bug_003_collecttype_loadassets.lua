-- BUG-003 — `MrxTaskJobCollectType.LoadAssets` reads an array with `pairs()`, so its de-duplication
-- pass is inert (and it indexes the field unguarded, faulting on saves that lack it). Appended to
-- `mrxtaskjobcollecttype`. See verification/BUG-001.md.
-- ⚠ Making this pass live produces MORE of the spurious deaths BUG-001 guards against, so it must
-- ship together with bug_001, never alone. `self._tCollectedItems` stays `{}` as retail has it.

function LoadAssets(self, tSaveData)
  self._tCollectedItems = {}
  MrxTaskJob.LoadAssets(self, tSaveData)
  if tSaveData and tSaveData.tCollected then
    for _, uGuid in ipairs(tSaveData.tCollected) do
      _DisableCollectable(uGuid)
    end
  end
end
