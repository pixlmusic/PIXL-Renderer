cmake_minimum_required(VERSION 3.21)

add_library(Streamline INTERFACE)

target_include_directories(
  Streamline INTERFACE
    ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/include
    # NGX is consumed only through dynamically resolved entry points.  The
    # headers provide the vendor ABI declarations; PIXL does not link an NGX
    # import library or make the optional Neural Rendering runtime mandatory.
    ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/external/ngx-sdk/include)

if(MSVC)
  target_compile_options(Streamline INTERFACE /wd5103)
endif()
