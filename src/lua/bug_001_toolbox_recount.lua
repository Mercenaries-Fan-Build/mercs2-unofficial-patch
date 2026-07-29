-- BUG-001 — the toolbox counter climbs on its own across save/reload cycles.
--
-- Appended to `mrxtaskobjectivedestroy`.
--
-- WHAT THE PLAYER SEES
--   The X/100 toolbox count rises without collecting anything. Drive across Maracaibo, restart, and
--   it has gone up by roughly whatever streamed in that session; a few restarts walk it to 100.
--   Milestone reward vehicles are handed out along the way, and each phantom pickup re-pays its
--   cash value.
--
-- WHY
--   On load, `MrxTaskJobCollectType._Go` neutralises already-collected toolboxes two ways, and the
--   one that would actually prevent a re-count silently fails:
--
--     1. it labels them `CollectableInvalidated`, so `collectable.Create` self-destructs them when
--        they stream in;
--     2. it passes their GUIDs as the objective's `vTgtExclude`.
--
--   Route 2 is gated on `_IsValidTarget`, which for a destroy objective is `Object.IsAlive`
--   (`mrxtaskobjectivedestroy.lua:74`). A toolbox that has not streamed in yet is NOT alive, so
--   `ObjectFilter.AddObject(.., bExclude)` is never called for it. No error, no warning.
--
--   Route 1 then completes the job on its behalf: `collectable.Create` sees the label and calls
--   `Object.Kill` (`collectable.lua:20`) — the very same call a genuine pickup makes via
--   `OnContextAction` (`collectable.lua:38`). The still-subscribed `Event.ObjectDeath` handler
--   cannot tell the two apart, so it runs a full `CompletePart`: +1 completed, +1 on the X/100
--   stat, the cash paid again, and any milestone crossed.
--
--   `Collectable.Create` runs on every hibernation WAKE, not once per level, so this repeats.
--
-- THE FIX
--   A target the objective was explicitly told to exclude must never count — whether or not the
--   exclusion actually made it into the filter. That is what `vTgtExclude` already means; this
--   restores the meaning at the counting site, which is the one place that cannot be raced by
--   streaming.
--
--   Deliberately NOT a change to the liveness gate in `MrxTaskObjective.Activated`: `_ProcessElement`
--   is a local closure inside a ~50-line function, so patching it would mean copying the whole
--   function and inheriting every future drift in it.
--
-- SCOPE
--   Only ever *prevents* the counting of a GUID already listed as complete, so it cannot lose a
--   genuine pickup: a toolbox collected in THIS session was never in `vTgtExclude` to begin with.

local _qmTargetDestroyedBase = _TargetDestroyed

--- The set of GUIDs this objective was told to exclude, built once and cached on the objective.
--
-- Resolves the same two element shapes `_ProcessElement` accepts — a name string or a GUID — so the
-- guard means exactly what the filter would have meant. Anything else is ignored rather than
-- guessed at.
--
-- `vTgtExclude` is fixed when the objective is created, so caching it is safe; the cache is an empty
-- table (not nil) when there is nothing to exclude, so the miss is not re-derived on every death.
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

  -- Defensive: retail reaches `tConfig.bHeroOnly` one line later and would fault the same way on a
  -- nil config, but this guard runs FIRST, so it must not be the thing that turns a survivable
  -- state into a crash.
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

--- `_TargetDestroyed`, refusing to count a target that was already complete.
--
-- Not one-shot: `Collectable.Create` kills an invalidated pickup on every hibernation wake, so the
-- same GUID can raise `ObjectDeath` many times in one session. The entry stays in the set forever,
-- which is also just what it means — this toolbox was collected, it can never count again.
function _TargetDestroyed(self, uGuid, uCause, uKiller)
  if _QmExcludedTargets(self)[uGuid] then
    return
  end
  return _qmTargetDestroyedBase(self, uGuid, uCause, uKiller)
end
