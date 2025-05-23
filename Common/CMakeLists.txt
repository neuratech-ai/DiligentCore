cmake_minimum_required (VERSION 3.10)

project(Diligent-Common CXX)

set(INCLUDE
    include/pch.h
)

set(INTERFACE
    interface/AdvancedMath.hpp
    interface/Align.hpp
    interface/Array2DTools.hpp
    interface/AsyncInitializer.hpp
    interface/BasicMath.hpp
    interface/BasicFileStream.hpp
    interface/DataBlobImpl.hpp
    interface/DefaultRawMemoryAllocator.hpp
    interface/DummyReferenceCounters.hpp
    interface/FastRand.hpp
    interface/FileWrapper.hpp
    interface/FilteringTools.hpp
    interface/FixedBlockMemoryAllocator.hpp
    interface/GeometryPrimitives.h
    interface/HashUtils.hpp
    interface/ImageTools.h
    interface/LRUCache.hpp
    interface/FixedLinearAllocator.hpp
    interface/DynamicLinearAllocator.hpp
    interface/MemoryFileStream.hpp
    interface/ObjectBase.hpp
    interface/ObjectsRegistry.hpp
    interface/ParsingTools.hpp
    interface/RefCntAutoPtr.hpp
    interface/RefCntContainer.hpp
    interface/RefCountedObjectImpl.hpp
    interface/Serializer.hpp
    interface/SpinLock.hpp
    interface/STDAllocator.hpp
    interface/StringDataBlobImpl.hpp
    interface/ProxyDataBlob.hpp
    interface/StringTools.h
    interface/StringTools.hpp
    interface/StringPool.hpp
    interface/ThreadPool.h
    interface/ThreadPool.hpp
    interface/ThreadSignal.hpp
    interface/Timer.hpp
    interface/UniqueIdentifier.hpp
    interface/Cast.hpp
    interface/CompilerDefinitions.h
    interface/CallbackWrapper.hpp
)

set(SOURCE
    src/Array2DTools.cpp
    src/BasicFileStream.cpp
    src/DataBlobImpl.cpp
    src/DefaultRawMemoryAllocator.cpp
    src/FileWrapper.cpp
    src/FixedBlockMemoryAllocator.cpp
    src/GeometryPrimitives.cpp
    src/ImageTools.cpp
    src/MemoryFileStream.cpp
    src/Serializer.cpp
    src/SpinLock.cpp
    src/ThreadPool.cpp
    src/Timer.cpp
)

add_library(Diligent-Common STATIC ${SOURCE} ${INCLUDE} ${INTERFACE})

target_include_directories(Diligent-Common
PRIVATE
    include
PUBLIC
    interface
)

target_link_libraries(Diligent-Common
PRIVATE
    Diligent-BuildSettings
PUBLIC
    Diligent-TargetPlatform
)
if(PLATFORM_LINUX)
    target_link_libraries(Diligent-Common PRIVATE pthread)
endif()

# c++ 17 is needed for aligned_alloc
set_common_target_properties(Diligent-Common 17)

source_group("src" FILES ${SOURCE})
source_group("include" FILES ${INCLUDE})
source_group("interface" FILES ${INTERFACE})

set_target_properties(Diligent-Common PROPERTIES
    FOLDER DiligentCore
)

if(DILIGENT_INSTALL_CORE)
    install_core_lib(Diligent-Common)
endif()
