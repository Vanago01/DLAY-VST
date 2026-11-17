cmake_minimum_required(VERSION 3.22)

# ----------------------------------------------------------------------------
#  DLAY — Bend Time. Shape Space. Create Echoes.
#  Premium creative delay  ·  VST3 / AU / Standalone  ·  JUCE + C++20
# ----------------------------------------------------------------------------

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(DLAY VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # Static runtime: the plugin has no dependency on the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DLAY_BUILD_TESTS  "Build the headless DSP/preset test runner and the UI snapshot tool" ON)
option(DLAY_ENABLE_LTO   "Link-time optimisation for release builds"                          OFF)
option(DLAY_COPY_PLUGIN  "Copy built plugins into the user's plugin folders after building"   OFF)
option(DLAY_BUILD_HOST_CHECK "Build the host-side validator that loads the built VST3 like a DAW would" OFF)

set(DLAY_JUCE_TAG  "9.0.5" CACHE STRING "JUCE git tag to fetch")
set(DLAY_JUCE_PATH ""      CACHE PATH   "Use a local JUCE checkout instead of fetching one")
set(DLAY_SANITIZE  ""      CACHE STRING "Build everything with a sanitizer: 'address' (plus undefined behaviour) or 'thread'. GCC / Clang")

# ---------------------------------------------------------- sanitizers ------
# For running DLAY_Tests under a sanitizer. Applies to JUCE as well, so set it before
# JUCE is added. Not for release builds.
if(DLAY_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "DLAY_SANITIZE needs GCC or Clang")
    endif()

    if(DLAY_SANITIZE STREQUAL "address")
        set(DLAY_SANITIZE_FLAGS -fsanitize=address,undefined)
    elseif(DLAY_SANITIZE STREQUAL "thread")
        set(DLAY_SANITIZE_FLAGS -fsanitize=thread)
    else()
        message(FATAL_ERROR "DLAY_SANITIZE must be 'address' or 'thread'")
    endif()

    add_compile_options(${DLAY_SANITIZE_FLAGS} -fno-omit-frame-pointer -g)
    add_link_options(${DLAY_SANITIZE_FLAGS})
endif()

# ---------------------------------------------------------------- JUCE ------
if(DLAY_JUCE_PATH)
    add_subdirectory("${DLAY_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${DLAY_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# -------------------------------------------------------------- plugin ------
set(DLAY_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DLAY_FORMATS AU)
endif()

juce_add_plugin(DLAY
    PRODUCT_NAME                "DLAY"
    COMPANY_NAME                "DLAY Audio"
    BUNDLE_ID                   "com.dlayaudio.dlay"
    PLUGIN_MANUFACTURER_CODE    Dlay
    PLUGIN_CODE                 Dly1
    FORMATS                     ${DLAY_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${DLAY_COPY_PLUGIN}
    VST3_CATEGORIES             Fx Delay
    AU_MAIN_TYPE                kAudioUnitType_Effect
    DESCRIPTION                 "DLAY - creative multi-engine delay"
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

juce_add_binary_data(DLAY_Assets
    HEADER_NAME DlayAssets.h
    NAMESPACE   DlayAssets
    SOURCES
        Assets/Fonts/BarlowCondensed-Medium.ttf
        Assets/Fonts/BarlowCondensed-SemiBold.ttf
        Assets/Fonts/BarlowCondensed-Bold.ttf
        Assets/Fonts/ShareTechMono-Regular.ttf)

file(GLOB_RECURSE DLAY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

target_sources(DLAY PRIVATE ${DLAY_SOURCES})

# The DSP code tests floats against exact zero on purpose and uses 'default:' in switches
# over large enums; silence just those two diagnostics for our own sources.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${DLAY_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wno-switch-enum;-Wno-float-equal")
endif()
target_include_directories(DLAY PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(DLAY PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    DLAY_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(DLAY
    PRIVATE
        DLAY_Assets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(DLAY_ENABLE_LTO)
    target_link_libraries(DLAY PUBLIC juce::juce_recommended_lto_flags)
endif()

# --------------------------------------------------------------- tests ------
# Both tools link against the plugin's shared-code library, so JUCE is only
# compiled once.
if(DLAY_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/DlayTests.cpp")
    function(dlay_add_tool name source)
        add_executable(${name} "${source}")
        target_link_libraries(${name} PRIVATE DLAY)
        target_include_directories(${name} PRIVATE
            $<TARGET_PROPERTY:DLAY,INCLUDE_DIRECTORIES>
            "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
        target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:DLAY,COMPILE_DEFINITIONS>)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            set_source_files_properties("${source}" PROPERTIES
                COMPILE_OPTIONS "-Wno-switch-enum;-Wno-float-equal")
        endif()
    endfunction()

    dlay_add_tool(DLAY_Tests "Tests/DlayTests.cpp")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/UiSnapshot.cpp")
        dlay_add_tool(DLAY_UiSnapshot "Tests/UiSnapshot.cpp")
    endif()

    enable_testing()
    add_test(NAME DLAY_Tests COMMAND DLAY_Tests)
endif()

# ---------------------------------------------------- host-side validator ------
# A tiny VST3 host built on JUCE's own hosting code. It needs JUCE compiled with plugin
# hosting enabled, so it is a separate target with its own copy of the modules.
if(DLAY_BUILD_HOST_CHECK)
    juce_add_console_app(DLAY_HostCheck PRODUCT_NAME "DLAY_HostCheck")
    target_sources(DLAY_HostCheck PRIVATE Tests/HostCheck.cpp)
    target_compile_definitions(DLAY_HostCheck PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(DLAY_HostCheck
        PRIVATE juce::juce_audio_utils
        PUBLIC  juce::juce_recommended_config_flags)
    add_dependencies(DLAY_HostCheck DLAY_VST3)

    if(DLAY_BUILD_TESTS)
        # JUCE stores the artefact path as a generator expression, so it has to be
        # evaluated a second time before it is a path.
        set(DLAY_VST3_ARTEFACT "$<GENEX_EVAL:$<TARGET_PROPERTY:DLAY_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

        # The validator opens the editor. On Linux, give it a virtual display when one is
        # to be had, so the test also runs on a machine without a screen.
        if(UNIX AND NOT APPLE)
            find_program(DLAY_XVFB_RUN xvfb-run)
        endif()

        if(DLAY_XVFB_RUN)
            add_test(NAME DLAY_HostCheck
                     COMMAND "${DLAY_XVFB_RUN}" -a $<TARGET_FILE:DLAY_HostCheck> "${DLAY_VST3_ARTEFACT}")
        else()
            add_test(NAME DLAY_HostCheck COMMAND DLAY_HostCheck "${DLAY_VST3_ARTEFACT}")
        endif()
    endif()
endif()
