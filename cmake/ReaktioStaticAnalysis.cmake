function(reaktio_enable_static_analysis target_name)
    if(REAKTIO_ENABLE_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES clang-tidy)

        if(CLANG_TIDY_EXE)
            set_property(TARGET ${target_name} PROPERTY CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--quiet")
        else()
            message(WARNING "REAKTIO_ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
        endif()
    endif()

    if(REAKTIO_ENABLE_CPPCHECK)
        find_program(CPPCHECK_EXE NAMES cppcheck)

        if(CPPCHECK_EXE)
            set_property(
                TARGET ${target_name}
                PROPERTY CXX_CPPCHECK
                "${CPPCHECK_EXE};--enable=warning,style,performance,portability;--quiet"
            )
        else()
            message(WARNING "REAKTIO_ENABLE_CPPCHECK is ON but cppcheck was not found.")
        endif()
    endif()
endfunction()