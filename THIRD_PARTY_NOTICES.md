# Third-party notices — KickCrafter

KickCrafter itself is licensed under the GNU Affero General Public License v3.0 or later
(`LICENSE`). The Linux VST3 build links or embeds the following third-party components; each
licence was inspected in the pinned sources listed below. The binary archive produced by
`tools/package.sh` ships the full licence texts of every embedded component in its `licenses/`
directory (JUCE, VST3 SDK, HarfBuzz, SheenBidi, zlib, pnglib, jpglib, FLAC, Ogg Vorbis, LV2, IBM Plex OFL).

| Component | Version / location | Licence | Notes |
|---|---|---|---|
| JUCE framework modules (`juce_core`, `juce_events`, `juce_data_structures`, `juce_graphics`, `juce_gui_basics`, `juce_gui_extra`, `juce_audio_basics`, `juce_audio_devices`, `juce_audio_formats`, `juce_audio_processors`, `juce_audio_utils`, `juce_audio_plugin_client`) | 8.0.9, commit `f72bad64d29715216226685810c5196bd0d79d77`, `external/JUCE` | AGPLv3 (JUCE dual licence, open-source option; `external/JUCE/LICENSE.md`) | Used under the AGPLv3 option; no JUCE commercial licence. |
| Steinberg VST3 SDK (bundled in JUCE) | `external/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` | GPLv3 option of the Steinberg dual licence (`LICENSE.txt` there) | GPLv3 code combined with AGPLv3 code as permitted by section 13 of both licences. |
| zlib (bundled in JUCE) | `modules/juce_core/zip/zlib/` | zlib | |
| pnglib (bundled in JUCE) | `modules/juce_graphics/image_formats/pnglib/` | zlib | |
| jpeglib (bundled in JUCE) | `modules/juce_graphics/image_formats/jpglib/` | Independent JPEG Group licence | |
| HarfBuzz (bundled in JUCE) | `modules/juce_graphics/fonts/harfbuzz/` | Old MIT | |
| SheenBidi (bundled in JUCE) | `modules/juce_graphics/unicode/sheenbidi/` | Apache 2.0 | |
| FLAC, Ogg Vorbis (bundled in JUCE) | `modules/juce_audio_formats/codecs/` | BSD | Compiled with the module but unused by the plugin. |
| LV2 SDK, pslextensions (bundled in JUCE) | `modules/juce_audio_processors/format_types/` | ISC / public domain | LV2 build not enabled. |
| Oboe, AudioUnitSDK, AAX SDK, Box2D, CHOC/QuickJS, GLEW/Mesa (bundled in JUCE) | various | Apache 2.0 / Apache 2.0 / proprietary-or-GPLv3 / zlib / ISC+MIT / BSD+MIT | Not compiled into the Linux VST3 (Android/macOS/AAX/OpenGL/JavaScript/Box2D modules are not used). |
| IBM Plex Sans (Regular, Medium, SemiBold) and IBM Plex Mono (Medium) | `resources/fonts/`, from the Arch package `ttf-ibm-plex` 6.4.0 | SIL Open Font License 1.1 (`resources/fonts/IBM-Plex-OFL.txt`) | Embedded as binary data; reserved font name "Plex" is unchanged. |
| System libraries at run time: FreeType, fontconfig, ALSA, X11/Xext/Xrandr/Xinerama/Xcursor, libdl/pthread | Arch Linux packages | FreeType licence / MIT / LGPL-2.1+ / MIT | Dynamically linked or dlopen'ed by JUCE; not redistributed. |
| DaisySP `SoftLimit` formula (reference only) | `Utility/dsp.h` at commit `a0494a3a` of [electro-smith/DaisySP](https://github.com/electro-smith/DaisySP), itself derived from pichenettes/stmlib | MIT | The rational function `x*(27+x²)/(27+9x²)` is re-implemented in `engine/SoftClip.h`; no DaisySP code is included. |

The original KickCrafter Daisy firmware and SDL/ImGui desktop application by Arnaud Valensi are the
behavioural references (private repositories); no code from them is copied verbatim except the
mathematical definitions (wavetable generation, sweep, envelope and gain topology) described in
`README.md` ("How it works").
