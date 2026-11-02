-- SoftBodyProbe: a temporary F4SE plugin that logs SoftBody FO4's genital bones during 4Stim scenes, for tuning the
-- openings. Builds against 4Strands' pinned commonlibf4 (the same commit 4Stim uses) and 4Stim's plugin interface.
set_xmakever("3.0.0")

includes("../../4Strands/lib/commonlibf4")

set_project("SoftBodyProbe")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

target("SoftBodyProbe")
    add_rules("commonlibf4.plugin", {
        name = "SoftBodyProbe",
        author = "SoftBody FO4",
        description = "Temporary: logs SoftBody FO4's genital bones during 4Stim scenes"
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src", "../../4Strands/extern/4stim")
    set_pcxxheader("src/PCH.h")
