-- BUG-015 — the HQ stockpile is never restored from a save. Appended to `wifpmcinterior`. See
-- verification/BUG-015.md.
--
-- Retail `SaveSingleton` writes `tStockpileQty`; retail `LoadSingleton` guards the restore on
-- `tSaveData.tSupportQty`, a key nothing writes, so `_tStockpileQty` (the per-category high-water
-- mark `_UpdateStockpile` shows the HQ piles at) restarts empty on every load. Saves already carry
-- the key, so existing saves restore with no format change.
-- Body is retail's, wifpmcinterior.lua:1631-1644, with the one guard token changed.

function LoadSingleton(tSaveData)
  if tSaveData.bUnlocked then
    Unlock()
    WifPmcGarage.CheckFionaCar(true)
  end
  -- ★ `tStockpileQty`, the key SaveSingleton writes, where retail tested `tSupportQty`.
  if tSaveData.tStockpileQty then
    _tStockpileQty = tSaveData.tStockpileQty
  end
  if tSaveData.tIntroduced then
    for sStarterId, bIntroduced in pairs(tSaveData.tIntroduced) do
      _tStarters[sStarterId].bIntroduced = bIntroduced
    end
  end
end
