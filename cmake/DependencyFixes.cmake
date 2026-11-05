function(d2x_replace_dependency_text path original replacement)
  file(READ "${path}" contents)
  string(FIND "${contents}" "${replacement}" replacement_offset)
  if(NOT replacement_offset EQUAL -1)
    return()
  endif()
  string(FIND "${contents}" "${original}" original_offset)
  if(NOT original_offset EQUAL -1)
    string(REPLACE "${original}" "${replacement}" contents "${contents}")
    file(WRITE "${path}" "${contents}")
  else()
    message(FATAL_ERROR "Dependency fix no longer matches ${path}")
  endif()
endfunction()

function(d2x_prepare_dependency name source_dir)
  if(name STREQUAL "stormlib")
    d2x_replace_dependency_text("${source_dir}/src/libtommath/tommath.h"
      "typedef unsigned long      mp_digit;\n   typedef unsigned long      mp_word __attribute__ ((mode(TI)));"
      "typedef ulong64            mp_digit;\n   typedef unsigned long      mp_word __attribute__ ((mode(TI)));")
    d2x_replace_dependency_text("${source_dir}/src/libtommath/bn_mp_get_int.c"
      "unsigned long res;" "mp_word res;")
    d2x_replace_dependency_text("${source_dir}/src/SMemUtf8.cpp"
      "memcpy(szBuffer + nOutLength, _T(\"%u[\"), 6);"
      "memcpy(szBuffer + nOutLength, _T(\"%u[\"), 3 * sizeof(TCHAR));")
    d2x_replace_dependency_text("${source_dir}/src/zlib/inflate.h"
      "#endif __INFLATE_H_" "#endif /* __INFLATE_H_ */")
    d2x_replace_dependency_text("${source_dir}/src/zlib/zutil.h"
      "&& !defined(NO_VIZ)" "&& !defined(NO_VIZ) && !defined(_WIN32)")
  elseif(name STREQUAL "raylib")
    d2x_replace_dependency_text("${source_dir}/src/external/jar_mod.h"
      "memcopy(&(modctx->song.title),modmemory,1084);"
      "if (mod_data_size < 1084) return false;\n            memcopy(&(modctx->song),modmemory,1084);")
    d2x_replace_dependency_text("${source_dir}/cmake/CompileDefinitions.cmake"
      "    foreach(VALUE IN LISTS CONFIG_HEADER_VALUES)"
      "    list(FILTER CONFIG_HEADER_VALUES EXCLUDE REGEX \"^MAX_MESH_VERTEX_BUFFERS=\")\n    if(RL_SUPPORT_MESH_GPU_SKINNING)\n        list(APPEND CONFIG_HEADER_VALUES MAX_MESH_VERTEX_BUFFERS=9)\n    else()\n        list(APPEND CONFIG_HEADER_VALUES MAX_MESH_VERTEX_BUFFERS=7)\n    endif()\n\n    foreach(VALUE IN LISTS CONFIG_HEADER_VALUES)")
  elseif(name STREQUAL "bncsutil")
    d2x_replace_dependency_text("${source_dir}/src/bncsutil/checkrevision.cpp"
      "GetFileVersionInfo(file_name, NULL, infoSize, buf)"
      "GetFileVersionInfo(file_name, 0, infoSize, buf)")
    d2x_replace_dependency_text("${source_dir}/src/bncsutil/file.cpp"
      "#define WIN32_LEAN_AND_MEAN\n#define BWIN 1"
      "#ifndef WIN32_LEAN_AND_MEAN\n#define WIN32_LEAN_AND_MEAN\n#endif\n#define BWIN 1")
  endif()
endfunction()