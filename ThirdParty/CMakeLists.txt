cmake_minimum_required (VERSION 3.13)

# cmake_minimum_required(VERSION) resets all policies to their default
# values for the VERSION, so calling cmake_policy(SET CMP0063 NEW) has
# no effect on subdirectories. We have to set CMAKE_POLICY_DEFAULT_CMP0063
# instead:
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)

if (GL_SUPPORTED AND (PLATFORM_WIN32 OR PLATFORM_LINUX OR PLATFORM_MACOS) AND (NOT TARGET GLEW::glew))
    add_subdirectory(glew)
    if (DILIGENT_INSTALL_CORE)
        install(TARGETS glew-static
                ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>"
                LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>"
                RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>"
        )
    endif()
    install(FILES glew/LICENSE.txt DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME GLEW-License.txt)
endif()

if (VULKAN_SUPPORTED)
    if (NOT TARGET Vulkan::Headers)
        # Manually add the Vulkan-Headers target as this is all we need and the
        # project's CMake creates unnecessary targets
        add_library(Vulkan-Headers INTERFACE)
        # Vulkan-Headers_SOURCE_DIR variable is normally created by project(Vulkan-Headers)
        set(Vulkan-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-Headers")
        add_library(Vulkan::Headers ALIAS Vulkan-Headers)
        target_include_directories(Vulkan-Headers INTERFACE "${Vulkan-Headers_SOURCE_DIR}/include")
        install(FILES "${Vulkan-Headers_SOURCE_DIR}/LICENSE.md" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME Vulkan-Headers-License.md)
    endif()
endif()

if (VULKAN_SUPPORTED OR METAL_SUPPORTED OR WEBGPU_SUPPORTED OR (ARCHIVER_SUPPORTED AND (GL_SUPPORTED OR GLES_SUPPORTED)))
    set(DILIGENT_USE_SPIRV_TOOLCHAIN TRUE CACHE INTERNAL "Use SPIRV Toolchain")

    if (NOT TARGET SPIRV-Headers)
        # Manually add the SPIRV-Headers target as this is all we need and the
        # project's CMake creates unnecessary install targets
        add_library(SPIRV-Headers INTERFACE)
        # Note that SPIRV-Headers_SOURCE_DIR variable is normally created by project(SPIRV-Headers)
        set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Headers")
        target_include_directories(SPIRV-Headers INTERFACE "${SPIRV-Headers_SOURCE_DIR}/include")
        install(FILES "${SPIRV-Headers_SOURCE_DIR}/LICENSE" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME SPIRV-Headers-License.txt)
    endif()

    option(DILIGENT_NO_GLSLANG "Do NOT build glslang compiler" OFF)
    option(DILIGENT_IMPROVE_SPIRV_TOOLS_DEBUG_PERF "Enable some optimizations for SPIRV-Tools, glslang, SPIRV-Cross and related projects in debug build to improve performance" ON)

    if (NOT (${DILIGENT_NO_GLSLANG} AND ${DILIGENT_NO_HLSL}) AND (NOT TARGET SPIRV-Tools-static))
        set(SPIRV_SKIP_TESTS ON CACHE BOOL "Skip SPIRV tests")
        set(SPIRV_BUILD_COMPRESSION OFF CACHE BOOL "Do not build SPIRV compressing codec")
        set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "Skip building SPIRV executable and tests along with the library")
        set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "Skip SPIRV tools installation")
        set(SPIRV_WERROR OFF CACHE BOOL "Do not enable error on warning")
        if (${DILIGENT_IMPROVE_SPIRV_TOOLS_DEBUG_PERF})
            set(SPIRV_CHECK_CONTEXT OFF CACHE BOOL "Do not check if the IR context is in a valid state")
        endif()
        set(CMAKE_MACOSX_RPATH TRUE)
        add_subdirectory(SPIRV-Tools)
        set_directory_root_folder("SPIRV-Tools" "DiligentCore/ThirdParty/SPIRV-Tools")
        # We only need SPIRV-Tools-opt for glslang that also depends on SPIRV-Tools-static.
        # Exclude all other targets from the build to save build time
        set(SPIRV_TOOLS_EXCLUDE_TARGETS SPIRV-Tools-link SPIRV-Tools-lint SPIRV-Tools-reduce SPIRV-Tools-shared)
        set_target_properties(${SPIRV_TOOLS_EXCLUDE_TARGETS} PROPERTIES EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)

        set(THIRD_PARTY_LIBS SPIRV-Tools-static SPIRV-Tools-opt)
        install(FILES "${spirv-tools_SOURCE_DIR}/LICENSE" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME SPIRV-Tools-License.txt)
    endif()

    if (NOT ${DILIGENT_NO_GLSLANG} AND (NOT TARGET glslang))
        set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Do not build glslang binaries")
        set(ENABLE_SPVREMAPPER OFF CACHE BOOL "Do not build SPIRV remapper")
        if (MSVC)
            # https://github.com/DiligentGraphics/DiligentCore/issues/684
            set(ENABLE_EXCEPTIONS ON CACHE BOOL "Enable exceptions")
        endif()
        if (PLATFORM_WEB)
            # Optimization has to be disabled on Web, see build instructions on https://github.com/KhronosGroup/glslang
            set(ENABLE_OPT OFF)
        endif()
        add_subdirectory(glslang)
        set_directory_root_folder("glslang" "DiligentCore/ThirdParty/glslang")

        if (PLATFORM_WEB)
             set_targets_emscripten_properties(glslang MachineIndependent)
        endif()

        # glslang "kindly" sets global CMAKE_DEBUG_POSTFIX to "d", which we have to unset now
        unset(CMAKE_DEBUG_POSTFIX CACHE)

        list(APPEND THIRD_PARTY_LIBS GenericCodeGen glslang OSDependent SPIRV MachineIndependent)
        install(FILES "${glslang_SOURCE_DIR}/LICENSE.txt" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME GLSLang-License.txt)
    endif()

    if (NOT TARGET spirv-cross-core)
        set(SPIRV_CROSS_CLI OFF CACHE BOOL "Build the CLI binary. Requires SPIRV_CROSS_STATIC.")
        set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "Enable SPIRV-Cross tests.")
        set(SPIRV_CROSS_ENABLE_MSL ${METAL_SUPPORTED} CACHE BOOL "Enable MSL target support.")
        # MSL support requires GLSL
        if (${SPIRV_CROSS_ENABLE_MSL} OR ${GL_SUPPORTED} OR ${GLES_SUPPORTED})
            set(SPIRV_CROSS_ENABLE_GLSL TRUE CACHE BOOL "Enable GLSL support.")
        else()
            set(SPIRV_CROSS_ENABLE_GLSL OFF CACHE BOOL "Enable GLSL support.")
        endif()
        set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "Enable HLSL target support.")
        set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "Enable C++ target support.")
        set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "Enable JSON reflection target support.")
        set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "Enable C API wrapper support in static library.")
        set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "Enable util module support.")
        set(SPIRV_CROSS_FORCE_PIC ON CACHE BOOL "Force position-independent code.")
        set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "Skip SPIRV-Cross installation.")
        set(SPIRV_CROSS_NAMESPACE_OVERRIDE diligent_spirv_cross CACHE STRING "Override the namespace used in the C++ API.")
        add_subdirectory(SPIRV-Cross)
        set_directory_root_folder("SPIRV-Cross" "DiligentCore/ThirdParty/SPIRV-Cross")

        list(APPEND THIRD_PARTY_LIBS spirv-cross-core)
        if(SPIRV_CROSS_ENABLE_GLSL)
            list(APPEND THIRD_PARTY_LIBS spirv-cross-glsl)
        endif()
        if (SPIRV_CROSS_ENABLE_MSL)
            list(APPEND THIRD_PARTY_LIBS spirv-cross-msl)
        endif()

        install(FILES "${SPIRV-Cross_SOURCE_DIR}/LICENSE" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME SPIRV-Cross-License.txt)
    endif()

    if (MSVC)
        foreach(TARGET ${THIRD_PARTY_LIBS}) 
            if (${DILIGENT_IMPROVE_SPIRV_TOOLS_DEBUG_PERF})
                # Enable function inlining (/Ob1) in Debug build to improve shader compilation performance
                target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:DEBUG>:/Ob1>")
            endif()
            target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:DEBUG>:${DILIGENT_MSVC_DEBUG_COMPILE_OPTIONS}>")
            target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:RELEASE>:${DILIGENT_MSVC_RELEASE_COMPILE_OPTIONS}>")
            target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:MINSIZEREL>:${DILIGENT_MSVC_RELEASE_COMPILE_OPTIONS}>")
            target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:RELWITHDEBINFO>:${DILIGENT_MSVC_RELEASE_COMPILE_OPTIONS}>")
        endforeach()
    endif()

else()
    set(DILIGENT_USE_SPIRV_TOOLCHAIN FALSE CACHE INTERNAL "")
endif()

if (VULKAN_SUPPORTED AND (PLATFORM_WIN32 OR PLATFORM_LINUX OR PLATFORM_MACOS OR PLATFORM_ANDROID) AND (NOT TARGET volk::volk))
    include(../BuildTools/CMake/VulkanUtils.cmake)
    set(VOLK_PULL_IN_VULKAN OFF)
    set(VOLK_INSTALL OFF)
    set(VOLK_HEADERS_ONLY OFF)
    add_subdirectory(volk)
    target_link_libraries(volk PRIVATE Vulkan::Headers)
    get_vulkan_platform_definitions(VULKAN_PLATFORM_DEFINES)
    target_compile_definitions(volk PRIVATE ${VULKAN_PLATFORM_DEFINES})
    set_common_target_properties(volk)
    set_directory_root_folder("volk" "DiligentCore/ThirdParty")
    install(FILES "${volk_SOURCE_DIR}/LICENSE.md" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME Volk-License.md)
    list(APPEND THIRD_PARTY_LIBS volk)
endif()

if (WEBGPU_SUPPORTED)
    add_subdirectory(abseil-cpp EXCLUDE_FROM_ALL)
    set_directory_root_folder("abseil-cpp" "DiligentCore/ThirdParty")

    add_subdirectory(dawn EXCLUDE_FROM_ALL)
    set_directory_root_folder("dawn" "DiligentCore/ThirdParty/dawn")
endif()

if(DILIGENT_USE_OPENXR AND NOT (TARGET openxr_loader))
    add_subdirectory(OpenXR-SDK EXCLUDE_FROM_ALL)
endif()

if ((${DILIGENT_BUILD_GOOGLE_TEST}) AND (NOT TARGET gtest))
    set(INSTALL_GTEST OFF CACHE BOOL "Do not install googletest")
    set(BUILD_GMOCK OFF CACHE BOOL "Do not build gmock")
    set(gtest_force_shared_crt ON CACHE BOOL "Use shared (DLL) run-time lib even when Google Test is built as static lib.")
    add_subdirectory(googletest)
    set_directory_root_folder("googletest" "DiligentCore/ThirdParty/googletest")
    set_target_properties(gtest gtest_main PROPERTIES
        CXX_VISIBILITY_PRESET hidden # -fvisibility=hidden
        C_VISIBILITY_PRESET hidden # -fvisibility=hidden
        VISIBILITY_INLINES_HIDDEN TRUE
        )

    get_target_property(GTEST_SOURCE_DIR gtest SOURCE_DIR)
    install(FILES "googletest/LICENSE" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME googletest-License.txt)

    if (PLATFORM_WEB)
        find_targets_in_directory(GTEST_TARGETS ${GTEST_SOURCE_DIR})
        set_targets_emscripten_properties(${GTEST_TARGETS})
    endif()
endif()

if (NOT TARGET xxHash::xxhash)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build xxHash as dynamic library")
    set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "Build the xxhsum binary")
    add_subdirectory(xxHash/build/cmake)
    set_directory_root_folder("xxHash/build/cmake" "DiligentCore/ThirdParty/xxHash")
    install(FILES "${xxHash_SOURCE_DIR}/../../LICENSE" DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME xxHash-License.txt)
    list(APPEND THIRD_PARTY_LIBS xxhash)
endif()

# Make sure that symbols do not leak out when third-party
# libs are linked into shared libraries
set_target_properties(${THIRD_PARTY_LIBS} PROPERTIES
    CXX_VISIBILITY_PRESET hidden # -fvisibility=hidden
    C_VISIBILITY_PRESET hidden # -fvisibility=hidden
    VISIBILITY_INLINES_HIDDEN TRUE
)

if (DILIGENT_INSTALL_CORE)
    install(TARGETS ${THIRD_PARTY_LIBS}
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>"
    )
endif()

install(FILES stb/stb_image_write_license.txt DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}")
install(FILES DirectXShaderCompiler/LICENSE.TXT DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME DXC-License.txt)
install(FILES DirectXShaderCompiler/ThirdPartyNotices.txt DESTINATION "Licenses/ThirdParty/${DILIGENT_CORE_DIR}" RENAME DXC-ThirdPartyNotices.txt)
