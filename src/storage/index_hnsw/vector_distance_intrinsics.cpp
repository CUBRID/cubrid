/*
 * This file has been split into three ISA-specific files:
 *   vector_distance_intrinsics_avx512.cpp   — AVX-512 + FMA (float + int8)
 *   vector_distance_intrinsics_avx2.cpp     — AVX2 + FMA   (float + int8)
 *   vector_distance_intrinsics_fallback.cpp — no SIMD      (omp-simd aliases + scalar int8)
 *
 * CMakeLists.txt selects exactly one of these at configure time based on
 * CXX_AVX512_FOUND / CXX_AVX2_FOUND / USE_SIMD_NATIVE.
 *
 * This file is no longer compiled. It is kept for git history only.
 */
