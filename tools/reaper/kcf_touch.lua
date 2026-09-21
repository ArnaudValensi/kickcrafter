-- KickCrafter: REAPER check 5 (Touch mode). Run in the instance that has
-- automation.rpp open. Loops playback for KCF_OBSERVE seconds (default 40) while
-- an external driver moves a native knob (Start Frequency) and a graph handle
-- (Sweep Time). Records both envelopes and requires several distinct values
-- written during active playback.
local directory = assert(os.getenv("KCF_TEST_DIR"), "KCF_TEST_DIR is required")
local maxSeconds, settleSeconds = 120, 3
local doneMarker = directory .. "/touch-driver-done"
os.remove(doneMarker)
local doneSeen = nil
local output = assert(io.open(directory .. "/reaper-touch-results.txt", "w"))
local track = assert(reaper.GetTrack(0, 0), "open automation.rpp first")
local function findParam(needle)
  for index = 0, reaper.TrackFX_GetNumParams(track, 0) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, 0, index, "")
    if name == needle then return index end
  end
end
local watched = {
  { name = "Start Frequency", kind = "knob" },
  { name = "Sweep Time", kind = "graph-handle" },
}
for _, w in ipairs(watched) do
  w.index = assert(findParam(w.name), w.name .. " absent")
  w.initial = reaper.TrackFX_GetParamNormalized(track, 0, w.index)
  w.envelope = reaper.GetFXEnvelope(track, 0, w.index, true)
  reaper.DeleteEnvelopePointRange(w.envelope, -1, math.huge)
  w.changed, w.changedWhilePlaying, w.distinctValues = false, false, {}
end
local originalMode = reaper.GetMediaTrackInfo_Value(track, "I_AUTOMODE")
reaper.SetTrackAutomationMode(track, 2)   -- Touch
reaper.SetEditCurPos(0, false, false)
reaper.GetSet_LoopTimeRange(true, true, 0, 4, false)
reaper.GetSetRepeat(1)
reaper.TrackFX_Show(track, 0, 3)
reaper.OnPlayButton()
local started = reaper.time_precise()
output:write(string.format("OBSERVING until the driver-done marker (max %d s): %s (%d) initial=%.9f, %s (%d) initial=%.9f\n", maxSeconds,
  watched[1].name, watched[1].index, watched[1].initial, watched[2].name, watched[2].index, watched[2].initial))
output:flush()
-- Marker file tells the external driver that playback is running.
local ready = io.open(directory .. "/touch-ready", "w"); ready:write("1"); ready:close()

local function finish()
  local playing = reaper.GetPlayState() & 1 == 1
  reaper.OnStopButton()
  reaper.GetSetRepeat(0)
  reaper.SetTrackAutomationMode(track, originalMode)
  local allPass = playing
  for _, w in ipairs(watched) do
    local points = reaper.CountEnvelopePoints(w.envelope)
    local values, distinct = {}, 0
    for i = 0, points - 1 do
      local ok, time, value = reaper.GetEnvelopePoint(w.envelope, i)
      output:write(string.format("POINT %s %.6f %.9f\n", w.name, time, value))
      if ok then
        local key = string.format("%.4f", value)
        if not values[key] then values[key] = true; distinct = distinct + 1 end
      end
    end
    local pass = w.changed and w.changedWhilePlaying and points >= 3 and distinct >= 3
    output:write(string.format("%s: kind=%s changed=%s changedWhilePlaying=%s points=%d distinct=%d -> %s\n",
      w.name, w.kind, tostring(w.changed), tostring(w.changedWhilePlaying), points, distinct, pass and "PASS" or "FAIL"))
    allPass = allPass and pass
  end
  output:write(string.format("playing_at_end=%s\n", tostring(playing)))
  reaper.Main_SaveProjectEx(0, directory .. "/touch.rpp", 0)
  output:write(allPass and "PASS: native knob + graph handle -> host -> recorded Touch automation during playback\n"
                       or "FAIL: Touch automation evidence incomplete\n")
  output:write("DONE\n")
  output:close()
end

local function observe()
  local playing = reaper.GetPlayState() & 1 == 1
  for _, w in ipairs(watched) do
    local value = reaper.TrackFX_GetParamNormalized(track, 0, w.index)
    if math.abs(value - w.initial) > 0.0005 then
      w.changed = true
      if playing then w.changedWhilePlaying = true end
    end
  end
  local f = io.open(doneMarker, "r")
  if f then f:close(); if not doneSeen then doneSeen = reaper.time_precise() end end
  if doneSeen and reaper.time_precise() - doneSeen >= settleSeconds then finish(); return end
  if reaper.time_precise() - started >= maxSeconds then
    output:write("FAIL: mouse driver never reported completion\n"); output:write("DONE\n"); output:close(); reaper.OnStopButton(); return
  end
  reaper.defer(observe)
end
reaper.defer(observe)
