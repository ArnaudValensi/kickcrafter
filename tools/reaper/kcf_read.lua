-- KickCrafter Fable: REAPER check 6 (Read mode). A constant Start Frequency
-- envelope stays authoritative while an external driver drags the native knob
-- during playback. Saves read-mode.rpp so the opaque plug-in state can be
-- inspected independently (tools/reaper/check_saved_state.py).
local directory = assert(os.getenv("KCF_TEST_DIR"), "KCF_TEST_DIR is required")
-- Scripts run inside the already running REAPER instance do not see the runner's
-- environment; completion is driven by a marker file written after the mouse driver
-- has finished (bounded by maxSeconds), followed by a settle period.
local maxSeconds, settleSeconds = 120, 3
local doneMarker = directory .. "/read-driver-done"
os.remove(doneMarker)
local output = assert(io.open(directory .. "/reaper-read-results.txt", "w"))
local track = assert(reaper.GetTrack(0, 0))
local index
for i = 0, reaper.TrackFX_GetNumParams(track, 0) - 1 do
  local _, name = reaper.TrackFX_GetParamName(track, 0, i, "")
  if name == "Start Frequency" then index = i end
end
assert(index, "Start Frequency absent")
local target = 0.30   -- normalised: 20 * 100^0.30 = 79.6 Hz
local envelope = reaper.GetFXEnvelope(track, 0, index, true)
reaper.DeleteEnvelopePointRange(envelope, -1, math.huge)
reaper.InsertEnvelopePoint(envelope, 0, target, 1, 0, false, true)
reaper.InsertEnvelopePoint(envelope, 4, target, 1, 0, false, true)
reaper.Envelope_SortPoints(envelope)
reaper.SetTrackAutomationMode(track, 1)   -- Read
reaper.TrackFX_SetParamNormalized(track, 0, index, target)
local function render(pattern)
  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", directory, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", pattern, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "evaw", true)
  reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", 1.5, true)
  reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
  reaper.Main_OnCommand(41824, 0)
end
render("read-reference")   -- authoritative envelope, no mouse yet
reaper.SetEditCurPos(0, false, false)
reaper.GetSet_LoopTimeRange(true, true, 0, 4, false)
reaper.GetSetRepeat(1)
reaper.TrackFX_Show(track, 0, 3)
reaper.OnPlayButton()
local started = reaper.time_precise()
local maxDeviation = 0
output:write(string.format("OBSERVING Read-mode native edits until the driver-done marker (+%d s settle, max %d s), target normalised %.3f\n", settleSeconds, maxSeconds, target))
output:flush()
local ready = io.open(directory .. "/read-ready", "w"); ready:write("1"); ready:close()
local lastTrace = -1
local doneSeen = nil
local function observe()
  local value = reaper.TrackFX_GetParamNormalized(track, 0, index)
  maxDeviation = math.max(maxDeviation, math.abs(value - target))
  local e = reaper.time_precise() - started
  if e - lastTrace >= 0.5 then lastTrace = e; output:write(string.format("TRACE %.1f value=%.4f playing=%s\n", e, value, tostring(reaper.GetPlayState() & 1 == 1))); output:flush() end
  local driverDone = io.open(doneMarker, "r")
  if driverDone then driverDone:close() end
  if not driverDone then
    if e < maxSeconds then reaper.defer(observe); return end
    output:write("FAIL: mouse driver never reported completion\n"); output:write("DONE\n"); output:close(); reaper.OnStopButton(); return
  end
  if not doneSeen then doneSeen = reaper.time_precise() end
  if reaper.time_precise() - doneSeen < settleSeconds then reaper.defer(observe); return end   -- settle after the last drag
  local playing = reaper.GetPlayState() & 1 == 1
  reaper.Main_SaveProjectEx(0, directory .. "/read-mode.rpp", 0)
  reaper.OnStopButton()
  reaper.GetSetRepeat(0)
  render("read-after")       -- effective DSP after the drags: must equal the reference
  local final = reaper.TrackFX_GetParamNormalized(track, 0, index)
  output:write(string.format("playing=%s final_host_value=%.9f max_deviation_seen=%.9f\n", tostring(playing), final, maxDeviation))
  -- Deviation may be briefly visible while the mouse is down; the host must restore the envelope value.
  output:write(playing and math.abs(final - target) < 0.001
      and "PASS: host Read envelope remains authoritative (inspect saved plug-in state too)\n"
       or "FAIL: host Read envelope was overridden\n")
  output:write("DONE\n")
  output:close()
end
reaper.defer(observe)
