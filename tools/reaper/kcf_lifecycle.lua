-- KickCrafter: REAPER check 7 helper, run with automation.rpp open.
--  a) same project, same rate: render with the editor CLOSED and with the editor OPEN
--     (editor-closed.wav / editor-open.wav, compared sample-exact by analyze_render.py --lifecycle)
--  b) close/reopen the editor, change the project/render sample rate to 96 kHz and 44.1 kHz,
--     render each, confirm the plug-in state survived. Writes reaper-lifecycle-results.txt.
local directory = assert(os.getenv("KCF_TEST_DIR"), "KCF_TEST_DIR is required")
local output = assert(io.open(directory .. "/reaper-lifecycle-results.txt", "w"))
local track = assert(reaper.GetTrack(0, 0))
local function param(needle)
  for i = 0, reaper.TrackFX_GetNumParams(track, 0) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, 0, i, "")
    if name == needle then return i end
  end
end
local attack = assert(param("Attack"))
local before = reaper.TrackFX_GetParamNormalized(track, 0, attack)
local function fileSize(path) local f = io.open(path, "rb"); if not f then return nil end local s = f:seek("end"); f:close(); return s end
local function render(pattern, seconds, rate)
  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", directory, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", pattern, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "evaw", true)
  reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", seconds, true)
  reaper.GetSetProjectInfo(0, "RENDER_SRATE", rate, true)
  reaper.Main_OnCommand(41824, 0)
  local size = fileSize(directory .. "/" .. pattern .. ".wav")
  assert(size and size > 1000, "render " .. pattern .. " missing")
  output:write("Rendered " .. pattern .. ".wav (" .. size .. " bytes)\n")
end
-- Sequence with settle periods (REAPER delivers queued parameter changes asynchronously after
-- interactive stages); each render is a single strict attempt, nothing is retried.
local steps = {
  function() reaper.TrackFX_Show(track, 0, 2) end,                       -- hide the floating editor
  function() render("editor-closed", 4, 48000) end,
  function() reaper.TrackFX_Show(track, 0, 3) end,                       -- show it again
  function() render("editor-open", 4, 48000) end,
  function()
    for _, rate in ipairs({96000, 44100}) do
      reaper.GetSetProjectInfo(0, "PROJECT_SRATE", rate, true)
      reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
      render("lifecycle-" .. rate, 1.5, rate)
    end
    reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 0, true)
    reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
    local after = reaper.TrackFX_GetParamNormalized(track, 0, attack)
    assert(math.abs(after - before) < 1e-6, "attack changed across editor/sample-rate lifecycle")
    output:write("PASS: editor close/reopen renders, 96k/44.1k renders with the editor open, state kept, no crash\n")
  end,
}
local index, t0 = 1, reaper.time_precise() - 10
local function run()
  if index > #steps then output:write("DONE\n"); output:close(); return end
  if reaper.time_precise() - t0 < 2.0 then reaper.defer(run); return end   -- 2 s settle between steps
  local ok, err = xpcall(steps[index], debug.traceback)
  if not ok then output:write("FAIL: " .. tostring(err) .. "\n"); output:write("DONE\n"); output:close(); return end
  index = index + 1; t0 = reaper.time_precise(); reaper.defer(run)
end
run()
