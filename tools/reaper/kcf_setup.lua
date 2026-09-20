-- KickCrafter Fable: REAPER host checks 1-4 (isolated config, dummy audio).
-- Environment: KCF_TEST_DIR (results directory), KCF_FX (defaults to "VST3i: KickCrafter Fable").
-- Writes reaper-setup-results.txt, saves control.rpp / automation.rpp, renders both,
-- reopens automation.rpp and verifies state + envelope points, then opens the editor.
local directory = assert(os.getenv("KCF_TEST_DIR"), "KCF_TEST_DIR is required")
local requested = os.getenv("KCF_FX") or "VST3i: KickCrafter Fable"
local output = assert(io.open(directory .. "/reaper-setup-results.txt", "w"))
local function log(message) output:write(message .. "\n"); output:flush() end
local function check(ok, message) if not ok then error(message) end end

local function findParam(track, fx, needle)
  for index = 0, reaper.TrackFX_GetNumParams(track, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, index, "")
    if name == needle then return index end
  end
  return nil
end

local function normalisedLogHz(hz, lo, hi) return math.log(hz / lo) / math.log(hi / lo) end

local function setRender(pattern, seconds, srate)
  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", directory, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", pattern, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "evaw", true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT2", "", true)
  for key, value in pairs({RENDER_SETTINGS=0, RENDER_BOUNDSFLAG=0, RENDER_CHANNELS=2, RENDER_SRATE=srate,
                          RENDER_STARTPOS=0, RENDER_ENDPOS=seconds, RENDER_TAILFLAG=0, RENDER_ADDTOPROJ=0,
                          RENDER_DITHER=0, RENDER_NORMALIZE=0}) do
    reaper.GetSetProjectInfo(0, key, value, true)
  end
end

local function fileSize(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local size = f:seek("end"); f:close(); return size
end

local state = {}

local function run()
  log("REAPER " .. reaper.GetAppVersion() .. " / " .. requested)
  reaper.InsertTrackAtIndex(0, true)
  local track = reaper.GetTrack(0, 0)
  local fx = reaper.TrackFX_AddByName(track, requested, false, -1)
  check(fx >= 0, "plug-in not found: " .. requested)
  local _, actualName = reaper.TrackFX_GetFXName(track, fx, "")
  log("Loaded: " .. actualName)
  local isInstrument = reaper.TrackFX_GetInstrument(track) == fx
  log("Instrument slot: " .. tostring(isInstrument))
  check(isInstrument, "REAPER does not treat the plug-in as an instrument")
  local count = reaper.TrackFX_GetNumParams(track, fx)
  log("Parameter count (includes REAPER's built-in controls): " .. count)
  for index = 0, count - 1 do
    local ok, name = reaper.TrackFX_GetParamName(track, fx, index, "")
    check(ok, "parameter name unavailable")
    local value, minimum, maximum = reaper.TrackFX_GetParam(track, fx, index)
    local _, formatted = reaper.TrackFX_GetFormattedParamValue(track, fx, index, "")
    log(string.format("PARAM\t%d\t%s\t%.9g\t[%g..%g]\t%s", index, name, value, minimum, maximum, formatted))
  end
  local startFreq = findParam(track, fx, "Start Frequency")
  local hold = findParam(track, fx, "Hold Time")
  local shape = findParam(track, fx, "Shape")
  local drive = findParam(track, fx, "Drive")
  local pitchSource = findParam(track, fx, "Pitch Source")
  check(startFreq and hold and shape and drive and pitchSource, "expected musical parameters missing")
  state.startFreq, state.hold, state.shape, state.drive = startFreq, hold, shape, drive
  state.presets = reaper.TrackFX_GetNumParams(track, fx)
  local _, presetCount = reaper.TrackFX_GetPresetIndex(track, fx)
  log("Preset count reported by host: " .. tostring(presetCount))

  -- Host value round trip (formatted text too), then back to defaults so the
  -- renders measure the reference hit.
  -- v1.1: the default Pitch Source is Fixed (choice 0); the renders below check that an
  -- overlapping A2 keeps its own pitch, which needs MIDI Note (choice 1). Set it through the
  -- host and verify the formatted text, which also proves the new choice order in REAPER.
  check(reaper.TrackFX_SetParamNormalized(track, fx, pitchSource, 1.0), "set pitch source failed")
  local _, pitchText = reaper.TrackFX_GetFormattedParamValue(track, fx, pitchSource, "")
  log("Pitch Source formatted after selecting choice 1: " .. pitchText)
  check(pitchText == "MIDI Note", "pitch source text mismatch: " .. pitchText)
  check(reaper.TrackFX_SetParamNormalized(track, fx, hold, 1.0), "set hold failed")          -- 1000 ms
  check(reaper.TrackFX_SetParamNormalized(track, fx, drive, 0.375), "set drive failed")      -- 1.5 x
  local _, holdText = reaper.TrackFX_GetFormattedParamValue(track, fx, hold, "")
  local _, driveText = reaper.TrackFX_GetFormattedParamValue(track, fx, drive, "")
  log("Hold formatted: " .. holdText .. " / Drive formatted: " .. driveText)
  check(holdText == "1000 ms", "hold text mismatch: " .. holdText)
  check(driveText == "1.50 x", "drive text mismatch: " .. driveText)
  local holdDefault, driveDefault = 0.132, 0.25
  check(reaper.TrackFX_SetParamNormalized(track, fx, hold, holdDefault), "reset hold failed")
  check(reaper.TrackFX_SetParamNormalized(track, fx, drive, driveDefault), "reset drive failed")
  local _, holdBack = reaper.TrackFX_GetFormattedParamValue(track, fx, hold, "")
  check(holdBack == "132 ms", "hold default text mismatch: " .. holdBack)

  -- MIDI item (120 BPM, 4 s): note tests + long snapshot note + overlapping second note.
  local item = reaper.CreateNewMIDIItemInProj(track, 0, 4, false)
  local take = reaper.GetActiveTake(item)
  local notes = { {0.10, 33, 127, 0.05}, {0.60, 33, 40, 0.05}, {1.10, 33, 127, 0.05}, {1.15, 45, 127, 0.05},
                  {2.00, 33, 127, 0.05}, {2.80, 33, 127, 0.05} }
  for _, n in ipairs(notes) do
    local ppq = reaper.MIDI_GetPPQPosFromProjTime(take, n[1])
    local ppqEnd = reaper.MIDI_GetPPQPosFromProjTime(take, n[1] + n[4])
    reaper.MIDI_InsertNote(take, false, false, ppq, ppqEnd, 0, n[2], n[3], true)
  end
  reaper.MIDI_Sort(take)
  reaper.SetTrackAutomationMode(track, 1)   -- Read

  -- Hold Time envelope in BOTH projects: default 132 ms for the first four hits,
  -- 1000 ms from 1.95 s so the 2.0 s note is a long voice (snapshot test).
  local holdEnv = reaper.GetFXEnvelope(track, fx, hold, true)
  check(holdEnv ~= nil, "REAPER cannot create a Hold Time envelope")
  reaper.DeleteEnvelopePointRange(holdEnv, -1, math.huge)
  check(reaper.InsertEnvelopePoint(holdEnv, 0.0, holdDefault, 1, 0, false, true), "hold point 1 failed")
  check(reaper.InsertEnvelopePoint(holdEnv, 1.9, holdDefault, 1, 0, false, true), "hold point 2 failed")
  check(reaper.InsertEnvelopePoint(holdEnv, 1.95, 1.0, 1, 0, false, true), "hold point 3 failed")
  check(reaper.InsertEnvelopePoint(holdEnv, 4.0, 1.0, 1, 0, false, true), "hold point 4 failed")
  reaper.Envelope_SortPoints(holdEnv)

  -- Control project: no Start Frequency envelope.
  setRender("control", 4, 48000)
  local controlProject = directory .. "/control.rpp"
  reaper.Main_SaveProjectEx(0, controlProject, 0)
  check(fileSize(controlProject), "control project not saved")
  reaper.Main_OnCommand(41824, 0)   -- render using recent settings (blocks until done)
  check(fileSize(directory .. "/control.wav"), "control render missing")
  log("Rendered control.wav")

  -- v1.3: the same project with the Velocity Sensitivity switch Off, set through
  -- the host: the velocity-40 note must then be as loud as the velocity-127 ones (analyze stage).
  local velocitySwitch = findParam(track, fx, "Velocity Sensitivity")
  check(velocitySwitch, "velocity switch parameter missing")
  check(reaper.TrackFX_SetParamNormalized(track, fx, velocitySwitch, 0.0), "set velocity switch off failed")
  local _, offText = reaper.TrackFX_GetFormattedParamValue(track, fx, velocitySwitch, "")
  check(offText == "Off", "velocity switch text before the off render: " .. offText)
  setRender("velocity-off", 4, 48000)
  reaper.Main_OnCommand(41824, 0)
  check(fileSize(directory .. "/velocity-off.wav"), "velocity-off render missing")
  log("Rendered velocity-off.wav (Velocity Sensitivity Off)")
  check(reaper.TrackFX_SetParamNormalized(track, fx, velocitySwitch, 1.0), "set velocity switch on failed")
  setRender("control", 4, 48000)

  -- Automation project: Start Frequency 250 Hz until 2.4 s, 1500 Hz from 2.5 s (square).
  local env = reaper.GetFXEnvelope(track, fx, startFreq, true)
  check(env ~= nil, "REAPER cannot create a Start Frequency envelope")
  local low = normalisedLogHz(250, 20, 2000)
  local high = normalisedLogHz(1500, 20, 2000)
  reaper.DeleteEnvelopePointRange(env, -1, math.huge)
  check(reaper.InsertEnvelopePoint(env, 0.0, low, 1, 0, false, true), "insert point 1 failed")
  check(reaper.InsertEnvelopePoint(env, 2.4, low, 1, 0, false, true), "insert point 2 failed")
  check(reaper.InsertEnvelopePoint(env, 2.5, high, 1, 0, false, true), "insert point 3 failed")
  check(reaper.InsertEnvelopePoint(env, 4.0, high, 1, 0, false, true), "insert point 4 failed")
  reaper.Envelope_SortPoints(env)
  check(reaper.CountEnvelopePoints(env) == 4, "envelope point count mismatch")
  -- Shape (morph) and Drive (gain) also jump at 2.5 s: both are audible in the long
  -- note's tail (the "accidental live parameters" of the Daisy original), so an
  -- unchanged tail is a strong snapshot proof; the 2.8 s note must change.
  for _, spec in ipairs({ {shape, 0.0, 1.0}, {drive, 0.25, 1.0} }) do
    local e = reaper.GetFXEnvelope(track, fx, spec[1], true)
    check(e ~= nil, "cannot create envelope")
    reaper.DeleteEnvelopePointRange(e, -1, math.huge)
    check(reaper.InsertEnvelopePoint(e, 0.0, spec[2], 1, 0, false, true), "env point failed")
    check(reaper.InsertEnvelopePoint(e, 2.4, spec[2], 1, 0, false, true), "env point failed")
    check(reaper.InsertEnvelopePoint(e, 2.5, spec[3], 1, 0, false, true), "env point failed")
    check(reaper.InsertEnvelopePoint(e, 4.0, spec[3], 1, 0, false, true), "env point failed")
    reaper.Envelope_SortPoints(e)
  end
  state.low, state.high = low, high
  setRender("automation", 4, 48000)
  reaper.Main_OnCommand(41824, 0)
  check(fileSize(directory .. "/automation.wav"), "automation render missing")
  log("Rendered automation.wav")

  state.track, state.count = track, count
  log("Renders done; deferring the persistence check so the host settles after the offline render")
  return true
end

-- Phase 2 (deferred after the renders): set non-default values and verify the host's
-- readback plus the plug-in's formatted value. REAPER queues VST3 parameter changes
-- to the component through audio processing; after an offline render the audio
-- device can be closed, so phase 3 checks the plug-in's ACTUAL state chunk and, if the
-- change has not been delivered, runs the transport briefly to let the host deliver it.
local function decodeChunkAttribute(chunk, name)
  local b = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
  chunk = chunk:gsub('[^' .. b .. '=]', '')
  local decoded = (chunk:gsub('.', function(x)
      if x == '=' then return '' end
      local r, f = '', (b:find(x) - 1)
      for i = 6, 1, -1 do r = r .. (f % 2 ^ i - f % 2 ^ (i - 1) > 0 and '1' or '0') end
      return r
    end):gsub('%d%d%d?%d?%d?%d?%d?%d?', function(x)
      if #x ~= 8 then return '' end
      local c = 0
      for i = 1, 8 do c = c + (x:sub(i, i) == '1' and 2 ^ (8 - i) or 0) end
      return string.char(c)
    end))
  return decoded:match(name .. '="([^"]+)"')
end

local function componentAttack(track)
  local ok, chunk = reaper.TrackFX_GetNamedConfigParm(track, 0, "vst_chunk")
  if not ok then return nil end
  return tonumber(decodeChunkAttribute(chunk, "attack"))
end

local function persistence()
  local track, fx = state.track, 0
  local attack = findParam(track, fx, "Attack")
  local velocity = findParam(track, fx, "Velocity Sensitivity")
  check(attack and velocity, "attack/velocity parameters missing")
  state.attack, state.velocity = attack, velocity
  log(string.format("Audio engine running before set: %s; component attack before set: %s",
      tostring(reaper.Audio_IsRunning()), tostring(componentAttack(track))))
  check(reaper.TrackFX_SetParamNormalized(track, fx, attack, 0.25), "set attack failed")
  -- v1.3: velocity sensitivity is a switch (On/Off). Set it Off through the host, read the
  -- formatted text back, then On again (the render oracle expects a quieter velocity-40 note).
  check(reaper.TrackFX_SetParamNormalized(track, fx, velocity, 0.0), "set velocity off failed")
  local _, attackText = reaper.TrackFX_GetFormattedParamValue(track, fx, attack, "")
  local _, velocityText = reaper.TrackFX_GetFormattedParamValue(track, fx, velocity, "")
  log("Set attack -> " .. attackText .. ", velocity -> " .. velocityText)
  check(attackText == "1.95 ms", "attack formatted readback mismatch: " .. attackText)
  check(velocityText == "Off", "velocity formatted readback mismatch: " .. velocityText)
  check(math.abs(reaper.TrackFX_GetParamNormalized(track, fx, attack) - 0.25) < 1e-4, "attack readback failed")
  check(reaper.TrackFX_GetParamNormalized(track, fx, velocity) < 0.5, "velocity off readback failed")
  check(reaper.TrackFX_SetParamNormalized(track, fx, velocity, 1.0), "set velocity on failed")
  local _, velocityOn = reaper.TrackFX_GetFormattedParamValue(track, fx, velocity, "")
  check(velocityOn == "On", "velocity on formatted readback mismatch: " .. velocityOn)
  check(reaper.TrackFX_GetParamNormalized(track, fx, velocity) > 0.5, "velocity on readback failed")
end

-- Phase 3: wait for the component state to reflect the change (kick the transport if
-- needed), then save.
local settleStarted, settleKicked = nil, false
local function settleAndSave()
  local track = state.track
  settleStarted = settleStarted or reaper.time_precise()
  local delivered = componentAttack(track)
  local elapsed = reaper.time_precise() - settleStarted
  if delivered and math.abs(delivered - 1.95) < 0.01 then
    if settleKicked then reaper.OnStopButton() end
    log(string.format("Component state reflects attack=%.4f ms after %.1f s (transport kick: %s)", delivered, elapsed, tostring(settleKicked)))
    local project = directory .. "/automation.rpp"
    reaper.Main_SaveProjectEx(0, project, 0)
    check(fileSize(project), "automation project not saved")
    log("Saved automation.rpp")
    return true
  end
  if elapsed > 2.0 and not settleKicked then
    log(string.format("Component state still attack=%s after 2 s (audio running: %s): delivering through a short transport run",
        tostring(delivered), tostring(reaper.Audio_IsRunning())))
    reaper.SetEditCurPos(0, false, false)
    reaper.OnPlayButton()
    settleKicked = true
  end
  check(elapsed < 15.0, "component state never received the host parameter change")
  return false   -- keep polling
end

local function reopenAndVerify()
  local project = directory .. "/automation.rpp"
  reaper.Main_openProject("noprompt:" .. project)
  local track = reaper.GetTrack(0, 0)
  local count = state.count
  local startFreq = findParam(track, 0, "Start Frequency")
  local hold = findParam(track, 0, "Hold Time")
  local shape = findParam(track, 0, "Shape")
  local drive = findParam(track, 0, "Drive")
  local attack, velocity = state.attack, state.velocity
  check(reaper.TrackFX_GetCount(track) == 1, "saved FX missing after reopen")
  check(reaper.TrackFX_GetNumParams(track, 0) == count, "parameter catalogue changed on reopen")
  local _, attackText = reaper.TrackFX_GetFormattedParamValue(track, 0, attack, "")
  log("Component attack after reopen: " .. tostring(componentAttack(track)) .. " ms")
  log("After reopen: attack " .. attackText .. string.format(" (%.6f), velocity %.6f", reaper.TrackFX_GetParamNormalized(track, 0, attack), reaper.TrackFX_GetParamNormalized(track, 0, velocity)))
  check(math.abs(reaper.TrackFX_GetParamNormalized(track, 0, attack) - 0.25) < 1e-4, "attack lost after reopen")
  check(reaper.TrackFX_GetParamNormalized(track, 0, velocity) > 0.5, "velocity switch lost after reopen (expects On)")   -- v1.3: On/Off
  for _, p in ipairs({shape, drive}) do
    local e2 = reaper.GetFXEnvelope(track, 0, p, false)
    check(e2 ~= nil and reaper.CountEnvelopePoints(e2) == 4, "shape/drive envelope lost after project reopen")
  end
  local holdEnvAgain = reaper.GetFXEnvelope(track, 0, hold, false)
  check(holdEnvAgain ~= nil and reaper.CountEnvelopePoints(holdEnvAgain) == 4, "hold envelope lost after project reopen")
  local env = reaper.GetFXEnvelope(track, 0, startFreq, false)
  check(env ~= nil and reaper.CountEnvelopePoints(env) == 4, "automation envelope lost after project reopen")
  local expected = {{0.0, state.low}, {2.4, state.low}, {2.5, state.high}, {4.0, state.high}}
  for i = 0, 3 do
    local ok, time, value = reaper.GetEnvelopePoint(env, i)
    check(ok and math.abs(time - expected[i + 1][1]) < 1e-6 and math.abs(value - expected[i + 1][2]) < 1e-6,
          "automation envelope points changed on reopen")
  end
  log("PASS: instrument discovery, host values + formatted text, envelope creation, project save/reopen, offline renders")
  state.track = track
  reaper.TrackFX_Show(track, 0, 3)    -- open the native editor (floating)
end

local phases = { {0.0, run}, {1.5, persistence}, {0.3, settleAndSave}, {1.5, reopenAndVerify} }
local started = reaper.time_precise()
local phaseIndex = 1
local function step()
  if phaseIndex > #phases then
    local opened = reaper.time_precise()
    local function verifyEditorRefresh()
      if reaper.time_precise() - opened < 2 then reaper.defer(verifyEditorRefresh); return end
      local a = reaper.TrackFX_GetParamNormalized(state.track, 0, state.attack)
      local v = reaper.TrackFX_GetParamNormalized(state.track, 0, state.velocity)
      if math.abs(a - 0.25) < 1e-4 and v > 0.5 then
        log("PASS: opening the native editor preserved host values (no gesture/feedback on refresh)")
      else
        log(string.format("FAIL: editor refresh changed host state attack=%.9f velocity=%.9f", a, v))
      end
      log("DONE")
      output:close()
    end
    reaper.defer(verifyEditorRefresh)
    return
  end
  local due, fn = phases[phaseIndex][1], phases[phaseIndex][2]
  if reaper.time_precise() - started < due then reaper.defer(step); return end
  local ok, err = xpcall(fn, debug.traceback)
  if not ok then
    log("FAIL: " .. tostring(err))
    log("DONE")
    output:close()
    return
  end
  if err == false then reaper.defer(step); return end   -- phase asked to be polled again
  phaseIndex = phaseIndex + 1
  started = reaper.time_precise()
  reaper.defer(step)
end
step()
