# Populate fixed sources without invoking the upstream BNCSutil build (NLS/GMP).
FetchContent_Declare(d2x_asio_source
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG 12e0ce9e0500bf0f247dbd1ae894272656456079
  SOURCE_SUBDIR d2x-no-upstream-build)
FetchContent_Declare(d2x_bncs_source
  GIT_REPOSITORY https://github.com/BNETDocs/bncsutil.git
  GIT_TAG 6334e0bde9cb7d2df73f7f9aa1072b54210a4d21
  SOURCE_SUBDIR d2x-no-upstream-build)
FetchContent_MakeAvailable(d2x_asio_source d2x_bncs_source)
find_package(Threads REQUIRED)

add_library(d2x_asio INTERFACE)
target_include_directories(d2x_asio SYSTEM INTERFACE "${d2x_asio_source_SOURCE_DIR}/asio/include")
target_compile_definitions(d2x_asio INTERFACE ASIO_STANDALONE)
target_link_libraries(d2x_asio INTERFACE Threads::Threads)
if(WIN32)
  target_link_libraries(d2x_asio INTERFACE ws2_32 mswsock)
endif()

# LGPL dependency stays replaceable; headers and OS APIs are private to auth.cpp.
add_library(d2x_bncs_legacy SHARED
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/bsha1.cpp"
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/cdkeydecoder.cpp"
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/checkrevision.cpp"
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/file.cpp"
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/sha1.c"
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/pe.c"
  "${d2x_bncs_source_SOURCE_DIR}/src/bncsutil/stack.c")
target_include_directories(d2x_bncs_legacy SYSTEM PUBLIC "${d2x_bncs_source_SOURCE_DIR}/src")
set_target_properties(d2x_bncs_legacy PROPERTIES PREFIX "")
if(MINGW)
  target_link_options(d2x_bncs_legacy PRIVATE -static -static-libgcc -static-libstdc++)
endif()
# Upstream debug traces include secret material, even in a Debug client build.
target_compile_definitions(d2x_bncs_legacy PRIVATE MUTIL_LIB_BUILD DEBUG=0 _CRT_SECURE_NO_WARNINGS)
if(WIN32)
  target_compile_definitions(d2x_bncs_legacy PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(d2x_bncs_legacy PRIVATE version)
endif()

add_library(d2x_network src/network/tcp_stream.cpp)
target_link_libraries(d2x_network PUBLIC d2x_core PRIVATE d2x_asio)
add_library(d2x_d2gs_protocol
  src/network/protocol/wire.cpp src/network/protocol/auth.cpp
  src/network/protocol/d2gs_stream.cpp src/network/protocol/lod113c.cpp
  src/network/protocol/huffman.cpp)
target_link_libraries(d2x_d2gs_protocol PUBLIC d2x_core PRIVATE d2x_bncs_legacy)
add_library(d2x_remote_client src/network/realm_session.cpp src/client/remote_world.cpp)
target_link_libraries(d2x_remote_client PUBLIC d2x_client_api d2x_network d2x_d2gs_protocol)
