# A local reuse contract, not a general binary-cache ABI model.
function(cubrid_thirdparty_identity output)
  # Bump for dependency versions, build options, patches, or package layout.
  # Comments, formatting, and test selection do not change this revision.
  set(recipe_revision 2)
  set(inputs "recipe_revision=${recipe_revision}\n")
  foreach(language C CXX)
    string(APPEND inputs
      "${language}=${CMAKE_${language}_COMPILER_ID}:${CMAKE_${language}_COMPILER_VERSION}\n")
  endforeach()
  string(APPEND inputs "${CMAKE_SYSTEM_NAME}:${CMAKE_SYSTEM_PROCESSOR}:${CMAKE_SIZEOF_VOID_P}\n")
  string(SHA256 digest "${inputs}")
  set(${output} "${digest}" PARENT_SCOPE)
endfunction()
