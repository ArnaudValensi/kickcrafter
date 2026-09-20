-- KickCrafter Fable: host Program exposure check. Host programs are deliberately not published
-- (see PluginProcessor.cpp): the enumeration must not contain a "Program" parameter, and the
-- factory presets are reachable through the editor (covered by the menus stage) and the state.
local directory = assert(os.getenv("KCF_TEST_DIR"), "KCF_TEST_DIR is required")
local output = assert(io.open(directory .. "/reaper-program-results.txt", "w"))
local function log(m) output:write(m .. "\n"); output:flush() end
local track = assert(reaper.GetTrack(0, 0))
local found, count = false, reaper.TrackFX_GetNumParams(track, 0)
for i = 0, count - 1 do
  local _, name = reaper.TrackFX_GetParamName(track, 0, i, "")
  if name == "Program" then found = true end
end
log("host parameter rows: " .. count .. ", Program parameter present: " .. tostring(found))
if found then log("FAIL: a host Program parameter is published although host programs are disabled")
else log("PASS: no host Program parameter is published (presets are editor/state only)") end
log("DONE")
output:close()
