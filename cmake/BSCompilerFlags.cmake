# Shared compiler settings for all OpenBattlespire targets.
#
# Usage: bs_configure_target(<target>)

function(bs_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(BS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wstrict-prototypes -Wmissing-prototypes
            -Wconversion -Wno-sign-conversion
            -Wvla -Wformat=2)
        if(BS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(BS_FUZZ)
            target_compile_options(${target} PRIVATE
                -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
                -fno-sanitize-recover=undefined)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        elseif(BS_SANITIZE)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
