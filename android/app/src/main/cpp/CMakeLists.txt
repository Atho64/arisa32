cmake_minimum_required(VERSION 3.22)
project(dc4android CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# dc4core sources live at the repository root.
get_filename_component(DC4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(DC4_CORE "${DC4_ROOT}/core")

set(DC4_CORE_SRC
    ${DC4_CORE}/src/crx.cpp
    ${DC4_CORE}/src/crm.cpp
    ${DC4_CORE}/src/mesfile.cpp
    ${DC4_CORE}/src/messtream.cpp
    ${DC4_CORE}/src/mesvm.cpp
    ${DC4_CORE}/src/voice.cpp
    ${DC4_CORE}/src/xpcm.cpp
    ${DC4_CORE}/src/bgm.cpp
    ${DC4_CORE}/src/audio.cpp
    ${DC4_CORE}/src/scene_audio.cpp
    ${DC4_CORE}/src/scene.cpp
    ${DC4_CORE}/src/scene_state.cpp
    ${DC4_CORE}/src/log_screen.cpp
    ${DC4_CORE}/src/save_screen.cpp
    ${DC4_CORE}/src/resource.cpp
    ${DC4_CORE}/src/system_menu.cpp
    ${DC4_CORE}/src/movie.cpp
    ${DC4_CORE}/src/confirm_dialog.cpp
    # miniz (zlib inflate for CRX) — vendored C sources
    ${DC4_ROOT}/third_party/miniz/miniz.c
    ${DC4_ROOT}/third_party/miniz/miniz_tinfl.c
    ${DC4_ROOT}/third_party/miniz/miniz_tdef.c
)
set_source_files_properties(${DC4_ROOT}/third_party/miniz/miniz.c
    ${DC4_ROOT}/third_party/miniz/miniz_tinfl.c
    ${DC4_ROOT}/third_party/miniz/miniz_tdef.c PROPERTIES LANGUAGE C)

add_library(dc4 SHARED
    ${DC4_CORE_SRC}
    dc4_jni.cpp
)

target_include_directories(dc4 PRIVATE
    ${DC4_CORE}/include
    ${DC4_ROOT}/third_party/miniz
    ${DC4_ROOT}/third_party/stb
    ${DC4_ROOT}/third_party/miniaudio
    ${DC4_ROOT}/third_party/pl_mpeg
)

# miniaudio: OpenSL|ES on every supported API (AAudio is disabled).
target_compile_definitions(dc4 PRIVATE
    MA_NO_RUNTIME_LINKING
    MA_NO_AAUDIO
)

find_library(log-lib log)
find_library(android-lib android)
find_library(openmaxndk OpenMAXAL)
find_library(opensles-lib OpenSLES)
find_library(jnigraphics-lib jnigraphics)
target_link_libraries(dc4 PRIVATE
    ${log-lib}
    ${android-lib}
    ${openmaxndk}
    ${opensles-lib}
    ${jnigraphics-lib}
)
