cmake_minimum_required(VERSION 3.22)

project(LiveBand VERSION 0.1.0 LANGUAGES C CXX)

option(LB_BUILD_APP   "Build the JUCE desktop app (needs the JUCE and sfizz submodules)" ON)
option(LB_BUILD_TESTS "Build the unit tests (downloads Catch2)" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# sfizz builds itself against the static MSVC runtime, so everything linked with it must too.
# A side benefit: LiveBand.exe runs without installing the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Warning flags for our own code. JUCE targets use JUCE's recommended flags instead.
add_library(liveband_warnings INTERFACE)
if(MSVC)
    target_compile_options(liveband_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(liveband_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

#---------------------------------------------------------------------------------------------------
# Core library: DSP and music logic. No JUCE, no sfizz, so it builds and tests anywhere.

add_library(liveband_core STATIC
    src/common/RtAllocGuard.cpp
    src/audio/Decimator.cpp
    src/audio/Fft.cpp
    src/audio/LatencyAnalysis.cpp
    src/audio/LatencyProbe.cpp
    src/audio/Limiter.cpp
    src/audio/OnsetDetector.cpp
    src/audio/VocalAnalyzer.cpp
    src/audio/YinPitch.cpp
    src/music/BeatClock.cpp
    src/music/ClickTrack.cpp
    src/music/Groove.cpp
    src/music/PatternPlayer.cpp
    src/music/TapTempo.cpp)
target_include_directories(liveband_core PUBLIC src)
target_compile_features(liveband_core PUBLIC cxx_std_20)
target_link_libraries(liveband_core PRIVATE liveband_warnings)

#---------------------------------------------------------------------------------------------------
# Unit tests

if(LB_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.15.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(liveband_tests
        tests/SpscQueueTests.cpp
        tests/CallbackStatsTests.cpp
        tests/RtAllocGuardTests.cpp
        tests/LatencyTests.cpp
        tests/DspBasicsTests.cpp
        tests/YinPitchTests.cpp
        tests/VocalAnalyzerTests.cpp
        tests/ClickTrackTests.cpp
        tests/BeatClockTests.cpp
        tests/PatternPlayerTests.cpp
        src/common/RtAllocGuardNew.cpp)
    target_link_libraries(liveband_tests PRIVATE liveband_core Catch2::Catch2WithMain liveband_warnings)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(liveband_tests)
endif()

#---------------------------------------------------------------------------------------------------
# Desktop app

if(LB_BUILD_APP)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt"
       OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/sfizz/external/abseil-cpp/CMakeLists.txt")
        message(FATAL_ERROR
            "Submodules are missing. Run: git submodule update --init --recursive --depth 1\n"
            "Or configure with -DLB_BUILD_APP=OFF to build only the core library and tests.")
    endif()

    # sfizz: static library only. libsndfile is LGPL, so keep it off.
    set(SFIZZ_SHARED      OFF CACHE BOOL "" FORCE)
    set(SFIZZ_JACK        OFF CACHE BOOL "" FORCE)
    set(SFIZZ_RENDER      OFF CACHE BOOL "" FORCE)
    set(SFIZZ_USE_SNDFILE OFF CACHE BOOL "" FORCE)
    set(SFIZZ_TESTS       OFF CACHE BOOL "" FORCE)
    set(SFIZZ_DEMOS       OFF CACHE BOOL "" FORCE)
    set(SFIZZ_BENCHMARKS  OFF CACHE BOOL "" FORCE)
    set(SFIZZ_DEVTOOLS    OFF CACHE BOOL "" FORCE)
    set(ENABLE_LTO        OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/sfizz EXCLUDE_FROM_ALL)

    add_subdirectory(libs/JUCE)

    # Engine sources use JUCE modules, which compile into each target that links them, so
    # every target below lists these sources itself rather than sharing a library.
    set(LB_ENGINE_SOURCES
        src/engine/AudioEngine.cpp
        src/engine/AudioFileLoader.cpp
        src/engine/BandController.cpp
        src/engine/GrooveLoader.cpp
        src/engine/SessionRecorder.cpp
        src/engine/SfizzInstrument.cpp)

    juce_add_gui_app(LiveBand
        PRODUCT_NAME      "LiveBand"
        COMPANY_NAME      "LiveBand"
        NEEDS_CURL        FALSE
        NEEDS_WEB_BROWSER FALSE)

    target_sources(LiveBand PRIVATE
        src/Main.cpp
        src/MainComponent.cpp
        src/engine/AudioDeviceSetup.cpp
        ${LB_ENGINE_SOURCES}
        src/ui/BeatLamps.cpp
        src/ui/NoteDisplay.cpp
        src/ui/PitchView.cpp
        # The allocation guard replaces global operator new, so it is only built into Debug.
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/common/RtAllocGuardNew.cpp>)

    target_compile_definitions(LiveBand PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LiveBand,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LiveBand,JUCE_VERSION>"
        LB_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        $<$<CONFIG:Debug>:LB_RT_ALLOC_GUARD=1>)

    target_link_libraries(LiveBand
        PRIVATE
            liveband_core
            sfizz::sfizz
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Put the instruments and grooves next to the executable, so a copied or downloaded build
    # still finds them. The kit's VCSL samples come along if tools/python/fetch_assets.py has run.
    add_custom_command(TARGET LiveBand POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/sfz/test"
                "$<TARGET_FILE_DIR:LiveBand>/assets/sfz/test"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/sfz/afro-kit"
                "$<TARGET_FILE_DIR:LiveBand>/assets/sfz/afro-kit"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/grooves"
                "$<TARGET_FILE_DIR:LiveBand>/assets/grooves")

    # lb-analyze: runs the analyzer over audio files for offline evaluation (tools/python).
    juce_add_console_app(lb-analyze PRODUCT_NAME "lb-analyze")
    target_sources(lb-analyze PRIVATE tools/lb-analyze/Main.cpp)
    target_compile_definitions(lb-analyze PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(lb-analyze PRIVATE
        liveband_core
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # lb-render: plays a style through the drum kit into a WAV file, for listening and checks.
    juce_add_console_app(lb-render PRODUCT_NAME "lb-render")
    target_sources(lb-render PRIVATE
        tools/lb-render/Main.cpp
        src/engine/GrooveLoader.cpp
        src/engine/SfizzInstrument.cpp)
    target_compile_definitions(lb-render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(lb-render PRIVATE
        liveband_core
        sfizz::sfizz
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Engine tests: drive AudioEngine and SfizzInstrument with a fake audio device.
    if(LB_BUILD_TESTS)
        juce_add_console_app(liveband_engine_tests PRODUCT_NAME "liveband_engine_tests")
        target_sources(liveband_engine_tests PRIVATE
            tests/engine/AudioEngineTests.cpp
            tests/engine/BandTests.cpp
            tests/engine/SfizzInstrumentTests.cpp
            ${LB_ENGINE_SOURCES}
            src/common/RtAllocGuardNew.cpp)
        target_compile_definitions(liveband_engine_tests PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            LB_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
        target_link_libraries(liveband_engine_tests PRIVATE
            liveband_core
            sfizz::sfizz
            juce::juce_audio_devices
            juce::juce_audio_formats
            Catch2::Catch2WithMain
            juce::juce_recommended_config_flags)
        catch_discover_tests(liveband_engine_tests)
    endif()
endif()
