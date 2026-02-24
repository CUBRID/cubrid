include(CheckCXXCompilerFlag)

# Check OpenMP SIMD support
check_cxx_compiler_flag("-fopenmp-simd" HAVE_OPENMP_SIMD)

if(HAVE_OPENMP_SIMD)
    set(OPENMP_SIMD_FLAG "-fopenmp-simd")
    message(STATUS "OpenMP SIMD supported: ${OPENMP_SIMD_FLAG}")
else()
    set(OPENMP_SIMD_FLAG "")
    message(STATUS "OpenMP SIMD not supported")
endif()
