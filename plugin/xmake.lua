-- SoftBodyFO4: SoftBody FO4's F4SE plugin (per-act physics, auto-alignment and the tuning log during 4Stim scenes).
-- Builds against 4Strands' pinned commonlibf4 (the same commit 4Stim uses) and 4Stim's plugin interface.
set_xmakever("3.0.0")

includes("../../4Strands/lib/commonlibf4")

set_project("SoftBodyFO4")
set_version("0.2.5")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

target("SoftBodyFO4")
    add_rules("commonlibf4.plugin", {
        name = "SoftBodyFO4",
        author = "SoftBody FO4",
        description = "SoftBody FO4: per-act physics and auto-alignment in 4Stim scenes"
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src", "../../4Strands/extern/4stim")
    add_syslinks("user32")
    set_pcxxheader("src/PCH.h")
