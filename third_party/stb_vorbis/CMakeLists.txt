add_library(stb_vorbis STATIC stb_vorbis.c)
target_include_directories(stb_vorbis INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# Files are decoded from memory (read through the VFS), so stdio isn't needed.
# Consumers include stb_vorbis.c with STB_VORBIS_HEADER_ONLY for the declarations.
target_compile_definitions(stb_vorbis PUBLIC STB_VORBIS_NO_STDIO)
if(MSVC)
    target_compile_options(stb_vorbis PRIVATE /w)
else()
    # Third-party code: no warnings; AddressSanitizer only (not UBSan) in sanitizer builds.
    target_compile_options(stb_vorbis PRIVATE -w)
    if(BS_SANITIZE OR BS_FUZZ)
        target_compile_options(stb_vorbis PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    endif()
    target_link_libraries(stb_vorbis PUBLIC m)
endif()
