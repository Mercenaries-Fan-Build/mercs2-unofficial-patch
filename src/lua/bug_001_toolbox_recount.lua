-- BUG-001 — the toolbox counter climbs on its own across save/reload cycles, paying out cash and
-- milestone vehicles for pickups that never happened. Appended to `mrxtaskobjectivedestroy`.
-- ⚠ Ships together with bug_003; see verification/BUG-001.md.

local _qmTargetDestroyedBase = _TargetDestroyed

--- The set of GUIDs this objective was told to exclude, built once and cached on the objective.
-- Resolves the same name-string / GUID shapes `_ProcessElement` accepts; caches an empty table (not
-- nil) so a miss is not re-derived on every death.
function _QmExcludedTargets(self)
  if self._qmExcludedTargets then
    return self._qmExcludedTargets
  end

  local tSet = {}
  local function _Add(vElement)
    local sType = type(vElement)
    if sType == "userdata" then
      tSet[vElement] = true
    elseif sType == "string" then
      local uGuid = Pg.GetGuidByName(vElement)
      if uGuid then
        tSet[uGuid] = true
      end
    end
  end

  -- ⚠ Runs before retail's own nil-config deref, so it must not turn a survivable state into a crash.
  local tConfig = self:GetConfig()
  local vExclude = tConfig and tConfig.vTgtExclude
  if type(vExclude) == "table" then
    for _, vElement in ipairs(vExclude) do
      _Add(vElement)
    end
  elseif vExclude ~= nil then
    _Add(vExclude)
  end

  self._qmExcludedTargets = tSet
  return tSet
end

--- `_TargetDestroyed`, refusing to count a target already listed as excluded (i.e. already complete).
function _TargetDestroyed(self, uGuid, uCause, uKiller)
  if _QmExcludedTargets(self)[uGuid] then
    return
  end
  return _qmTargetDestroyedBase(self, uGuid, uCause, uKiller)
end
