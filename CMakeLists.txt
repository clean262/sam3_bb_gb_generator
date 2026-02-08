cmake_minimum_required(VERSION 3.20)
project(sam3_aux2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------
# Layout:
#   <root>/aviutl2_sdk/*.h
#   <root>/cpp/*.cpp
# --------------------------------------------

# 互換のために外部指定もできるようにしておく（未指定ならプロジェクト直下）
set(AVIUTL2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
    "Path that contains 'aviutl2_sdk/plugin2.h' (default: project root)")

if(NOT EXISTS "${AVIUTL2_SDK_DIR}/aviutl2_sdk/plugin2.h")
  message(FATAL_ERROR
    "AVIUTL2_SDK_DIR must contain 'aviutl2_sdk/plugin2.h'.\n"
    "Current: ${AVIUTL2_SDK_DIR}\n"
    "Expected: ${AVIUTL2_SDK_DIR}/aviutl2_sdk/plugin2.h"
  )
endif()

set(SAM3_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cpp")
if(NOT EXISTS "${SAM3_CPP_DIR}/sam3_aux2.cpp")
  message(FATAL_ERROR "Missing source: ${SAM3_CPP_DIR}/sam3_aux2.cpp")
endif()
if(NOT EXISTS "${SAM3_CPP_DIR}/sam3mask.cpp")
  message(FATAL_ERROR "Missing source: ${SAM3_CPP_DIR}/sam3mask.cpp")
endif()

function(sam3_apply_common target_name)
  target_include_directories(${target_name} PRIVATE
    "${AVIUTL2_SDK_DIR}"   # ここがポイント: include "aviutl2_sdk/..." を解決
  )

  target_compile_definitions(${target_name} PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  )

  target_link_libraries(${target_name} PRIVATE
    user32
    shell32
    shlwapi
  )

  target_compile_options(${target_name} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
  )
endfunction()

# ---- sam3.aux2 ----
add_library(sam3_aux2 SHARED
  "${SAM3_CPP_DIR}/sam3_aux2.cpp"
)
sam3_apply_common(sam3_aux2)

set_target_properties(sam3_aux2 PROPERTIES
  OUTPUT_NAME "sam3"
  PREFIX ""
  SUFFIX ".aux2"
)

# ---- sam3mask.mod2 ----
add_library(sam3mask_mod2 SHARED
  "${SAM3_CPP_DIR}/sam3mask.cpp"
)
sam3_apply_common(sam3mask_mod2)

set_target_properties(sam3mask_mod2 PROPERTIES
  OUTPUT_NAME "sam3mask"
  PREFIX ""
  SUFFIX ".mod2"
)