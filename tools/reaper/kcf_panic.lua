-- KickCrafter Fable: CC120 (All Sound Off) / CC123 (All Notes Off) in a REAPER MIDI item.
-- Builds variants of one project on the running instance's track 1 and renders each:
--   panic-control.wav    long note (A1, 0.1 s, Hold 1000 ms) only
--   panic-cc120.wav      + CC120 at 0.6 s
--   panic-cc123.wav      + CC123 at 0.6 s
-- v1.1: the VST3 exports no synthetic MIDI CC parameters and VST3 carries no raw CC events,
-- so both renders must equal the control (analyze_render.py --panic documents this).
-- Writes reaper-panic-results.txt with an explicit PASS:/FAIL: line and DONE.
local directory = assert(os.getenv("KCF_TEST_DIR"), "KCF_TEST_DIR is required")
local output = assert(io.open(directory .. "/reaper-panic-results.txt", "w"))
local function log(m) output:write(m .. "\n"); output:flush() end
local function check(ok, m) if not ok then error(m) end end
local function fileSize(path) local f = io.open(path, "rb"); if not f then return nil end local s = f:seek("end"); f:close(); return s end
local track = assert(reaper.GetTrack(0, 0))
local function param(needle)
  for i = 0, reaper.TrackFX_GetNumParams(track, 0) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, 0, i, "")
    if name == needle then return i end
  end
end
local hold = assert(param("Hold Time"))
local ok, err = xpcall(function()
  -- Fresh track content: remove items, keep the FX; Read mode; Hold 1000 ms constant envelope.
  while reaper.CountTrackMediaItems(track) > 0 do reaper.DeleteTrackMediaItem(track, reaper.GetTrackMediaItem(track, 0)) end
  -- Static parameters for a clean comparison: bypass every existing FX envelope (Touch/Read
  -- stages left recorded points) and set Hold 1000 ms directly.
  for i = 0, reaper.CountTrackEnvelopes(track) - 1 do
    reaper.GetSetEnvelopeInfo_String(reaper.GetTrackEnvelope(track, i), "ACTIVE", "0", true)
  end
  reaper.SetTrackAutomationMode(track, 0)
  check(reaper.TrackFX_SetParamNormalized(track, 0, hold, 1.0), "set hold failed")
  -- Parameter changes made from ReaScript reach the VST3 plug-in through REAPER's processing
  -- side; with the dummy audio device stopped they are delivered by the next render itself.
  -- A discarded settle render absorbs that, and a second control render at the end lets the
  -- analyzer prove the state was stable across all variants (control == control2).
  for _, variant in ipairs({ {"panic-settle", nil}, {"panic-control", nil}, {"panic-cc120", 120}, {"panic-cc123", 123}, {"panic-control2", nil} }) do
    while reaper.CountTrackMediaItems(track) > 0 do reaper.DeleteTrackMediaItem(track, reaper.GetTrackMediaItem(track, 0)) end
    local item = reaper.CreateNewMIDIItemInProj(track, 0, 2, false)
    local take = reaper.GetActiveTake(item)
    local ppq = reaper.MIDI_GetPPQPosFromProjTime(take, 0.1)
    reaper.MIDI_InsertNote(take, false, false, ppq, reaper.MIDI_GetPPQPosFromProjTime(take, 0.15), 0, 33, 127, true)
    if variant[2] then
      local cppq = reaper.MIDI_GetPPQPosFromProjTime(take, 0.6)
      check(reaper.MIDI_InsertCC(take, false, false, cppq, 0xB0, 0, variant[2], 0), "MIDI_InsertCC failed for CC" .. variant[2])
      reaper.MIDI_Sort(take)
      local _, _, ccCount = reaper.MIDI_CountEvts(take)
      check(ccCount == 1, "expected exactly one CC event in " .. variant[1] .. ", got " .. tostring(ccCount))
      local _, _, _, ccppq, _, _, ccNum = reaper.MIDI_GetCC(take, 0)
      check(ccNum == variant[2], "wrong CC number in the item: " .. tostring(ccNum))
      log(string.format("CC%d present in %s at ppq %.0f", ccNum, variant[1], ccppq))
    end
    reaper.MIDI_Sort(take)
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", directory, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", variant[1], true)
    reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "evaw", true)
    reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
    reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", 2, true)
    reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
    os.remove(directory .. "/" .. variant[1] .. ".wav")
    reaper.Main_OnCommand(41824, 0)
    check(fileSize(directory .. "/" .. variant[1] .. ".wav"), "render " .. variant[1] .. " missing")
    log("Rendered " .. variant[1] .. ".wav")
  end
  log("PASS: three panic renders produced (analysis by analyze_render.py --panic)")
end, debug.traceback)
if not ok then log("FAIL: " .. tostring(err)) end
log("DONE")
output:close()
