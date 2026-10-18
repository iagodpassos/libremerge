# The Tree-sitter grammars WinMerge ships (WinMerge/tree-sitter-grammars),
# built as the modules the engine's TreeSitterRegistry loads, with their
# highlight, locals, tags and injection queries bundled next to them the
# way that repository's build-grammars.ps1 bundles them:
#
#   TreeSitterGrammars/tree-sitter-<name>.so
#   TreeSitterGrammars/<name>-{highlights,locals,tags,injections}.scm
#   TreeSitterGrammars/licenses/   (the grammars' and the runtime's)
#
# The sources are the commits WinMerge pins, downloaded once at configure
# time into LM_TREE_SITTER_DOWNLOAD_DIR; a folder that already holds the
# archives (makepkg's sources, for one) spares the download.

option(LM_TREE_SITTER_GRAMMARS "Build the Tree-sitter grammars" ON)
set(LM_TREE_SITTER_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/tree-sitter-archives"
  CACHE PATH "Folder of the Tree-sitter grammar source archives")
# global: the application and the tests take the folder from here
set(LM_TREE_SITTER_GRAMMARS_DIR "${CMAKE_BINARY_DIR}/TreeSitterGrammars"
  CACHE INTERNAL "The folder the Tree-sitter grammars are built in")
set(LM_TREE_SITTER_SOURCE_ROOT "${CMAKE_BINARY_DIR}/_deps/tree-sitter-src")

# <owner/repo> <commit> <SHA-256 of GitHub's archive of it> [<grammar>=<path>...]
# A repository without grammars is there for its queries, which another
# one's take in (typescript's take javascript's). tree-sitter-typescript's
# "flow" stays out: its module would hold tree_sitter_tsx, never loaded.
set(LM_TREE_SITTER_REPOS
  "tree-sitter-grammars/tree-sitter-markdown f969cd3ae3f9fbd4e43205431d0ae286014c05b5 45ec28324b75ec80788775a6aed26888f55b306981fede9cbe35d7348ae80469 markdown=tree-sitter-markdown markdown_inline=tree-sitter-markdown-inline"
  "tree-sitter-grammars/tree-sitter-properties 579b62f5ad8d96c2bb331f07d1408c92767531d9 97399c48a90c2b6809870e14f34d2ac0bcd8842a1b4c2031d5be242170d4d2e0 properties=."
  "tree-sitter-grammars/tree-sitter-toml 64b56832c2cffe41758f28e05c756a3a98d16f41 feeb2e1cf531588cdcfb9c57292620151a08597f18a98dad26cc17fd4b544dcb toml=."
  "tree-sitter-grammars/tree-sitter-yaml 7708026449bed86239b1cd5bce6e3c34dbca6415 6e8cb69a4f7a05478b1143d38bd10df2163170a6678df1848f3be89c8b7c5f3b yaml=."
  "tree-sitter/tree-sitter-javascript 44c892e0be055ac465d5eeddae6d3e194424e7de a13fa28148e41bb939b2c2b8cd73c4b4295273b70cbac55a7a589020f52b9611"
  "tree-sitter/tree-sitter-typescript f975a621f4e7f532fe322e13c4f79495e0a7b2e7 4de2e82e557810eecb93cb3b31fbb3bd28ba4c91ba9a6c6164bcaa074125b7b5 typescript=typescript tsx=tsx"
)

# The repository's sources, from its archive (downloaded when missing).
function(lm_tree_sitter_fetch repo commit sha256 out_var)
  string(REGEX REPLACE ".*/" "" name "${repo}")
  set(archive "${LM_TREE_SITTER_DOWNLOAD_DIR}/${name}-${commit}.tar.gz")
  if(EXISTS "${archive}")
    file(SHA256 "${archive}" actual)
  endif()
  if(NOT EXISTS "${archive}" OR NOT actual STREQUAL sha256)
    message(STATUS "Downloading ${repo} at ${commit}")
    file(DOWNLOAD "https://github.com/${repo}/archive/${commit}.tar.gz" "${archive}"
      EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${archive}")
      list(GET status 1 reason)
      message(FATAL_ERROR "Cannot download ${repo} (${reason}). Put "
        "${name}-${commit}.tar.gz in LM_TREE_SITTER_DOWNLOAD_DIR, or configure "
        "with -DLM_TREE_SITTER_GRAMMARS=OFF to build without the grammars.")
    endif()
  endif()
  set(src "${LM_TREE_SITTER_SOURCE_ROOT}/${name}")
  set(stamp "${src}/.lm-${commit}")
  if(NOT EXISTS "${stamp}")
    file(REMOVE_RECURSE "${src}" "${LM_TREE_SITTER_SOURCE_ROOT}/.extract")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${LM_TREE_SITTER_SOURCE_ROOT}/.extract")
    file(RENAME "${LM_TREE_SITTER_SOURCE_ROOT}/.extract/${name}-${commit}" "${src}")
    file(REMOVE_RECURSE "${LM_TREE_SITTER_SOURCE_ROOT}/.extract")
    file(TOUCH "${stamp}")
  endif()
  set(${out_var} "${src}" PARENT_SCOPE)
endfunction()

# build-grammars.ps1's Resolve-QueryFiles: the files a tree-sitter.json
# entry names for one kind of query, in its order (a node_modules path is
# another repository's file), or the grammar's queries/<kind>.scm.
function(lm_tree_sitter_query_files entry kind source_dir repo_dir out_var)
  set(resolved "")
  set(candidates "")
  if(entry)
    string(JSON type ERROR_VARIABLE missing TYPE "${entry}" ${kind})
    if(NOT missing)
      if(type STREQUAL "ARRAY")
        string(JSON count LENGTH "${entry}" ${kind})
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
          string(JSON candidate GET "${entry}" ${kind} ${i})
          list(APPEND candidates "${candidate}")
        endforeach()
      elseif(type STREQUAL "STRING")
        string(JSON candidate GET "${entry}" ${kind})
        list(APPEND candidates "${candidate}")
      endif()
    endif()
  endif()
  foreach(candidate IN LISTS candidates)
    set(tries "${repo_dir}/${candidate}" "${source_dir}/${candidate}")
    if(candidate MATCHES "^node_modules/(@[^/]+/)?([^/]+)/(.+)$")
      list(APPEND tries "${LM_TREE_SITTER_SOURCE_ROOT}/${CMAKE_MATCH_2}/${CMAKE_MATCH_3}")
    endif()
    foreach(try IN LISTS tries)
      if(EXISTS "${try}")
        list(FIND resolved "${try}" known)
        if(known EQUAL -1)
          list(APPEND resolved "${try}")
          break()
        endif()
      endif()
    endforeach()
  endforeach()
  if(NOT resolved)
    foreach(fallback "${source_dir}/queries/${kind}.scm" "${repo_dir}/queries/${kind}.scm")
      if(EXISTS "${fallback}")
        list(APPEND resolved "${fallback}")
        break()
      endif()
    endforeach()
  endif()
  set(${out_var} "${resolved}" PARENT_SCOPE)
endfunction()

# build-grammars.ps1's Write-QueryBundle: the files one after the other,
# a blank line between them, trailing whitespace trimmed, one newline last.
function(lm_tree_sitter_bundle_queries grammar source_dir repo_dir)
  file(READ "${repo_dir}/tree-sitter.json" json)
  string(JSON count LENGTH "${json}" grammars)
  math(EXPR last "${count} - 1")
  set(entry "")
  foreach(i RANGE ${last})
    string(JSON name GET "${json}" grammars ${i} name)
    if(name STREQUAL grammar)
      string(JSON entry GET "${json}" grammars ${i})
      break()
    endif()
  endforeach()
  foreach(kind highlights locals tags injections)
    lm_tree_sitter_query_files("${entry}" ${kind} "${source_dir}" "${repo_dir}" files)
    set(target "${LM_TREE_SITTER_GRAMMARS_DIR}/${grammar}-${kind}.scm")
    if(NOT files)
      file(REMOVE "${target}")
      continue()
    endif()
    set(bundle "")
    set(first TRUE)
    foreach(query IN LISTS files)
      file(READ "${query}" content)
      if(NOT first)
        string(APPEND bundle "\n\n")
      endif()
      string(APPEND bundle "${content}")
      set(first FALSE)
    endforeach()
    string(REGEX REPLACE "[ \t\r\n]+$" "" bundle "${bundle}")
    # written as it is (the queries' @captures are no CMake variables),
    # and only over a bundle that changed
    file(WRITE "${target}.new" "${bundle}\n")
    file(COPY_FILE "${target}.new" "${target}" ONLY_IF_DIFFERENT)
    file(REMOVE "${target}.new")
  endforeach()
endfunction()

set(LM_TREE_SITTER_MODULES "")
if(LM_TREE_SITTER_GRAMMARS)
  file(MAKE_DIRECTORY "${LM_TREE_SITTER_DOWNLOAD_DIR}" "${LM_TREE_SITTER_GRAMMARS_DIR}/licenses")
  # every repository first: a grammar's queries may take another's in
  set(lm_ts_grammars "")
  foreach(spec IN LISTS LM_TREE_SITTER_REPOS)
    string(REPLACE " " ";" fields "${spec}")
    list(POP_FRONT fields repo commit sha256)
    lm_tree_sitter_fetch(${repo} ${commit} ${sha256} src)
    string(REGEX REPLACE ".*/" "" name "${repo}")
    file(GLOB licenses "${src}/LICENSE*" "${src}/COPYING*")
    foreach(license IN LISTS licenses)
      get_filename_component(license_name "${license}" NAME)
      configure_file("${license}" "${LM_TREE_SITTER_GRAMMARS_DIR}/licenses/${name}-${license_name}" COPYONLY)
    endforeach()
    foreach(grammar IN LISTS fields)
      list(APPEND lm_ts_grammars "${grammar}=${src}")
    endforeach()
  endforeach()
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Externals/tree-sitter/LICENSE"
    "${LM_TREE_SITTER_GRAMMARS_DIR}/licenses/tree-sitter-LICENSE" COPYONLY)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Externals/tree-sitter/lib/src/unicode/LICENSE"
    "${LM_TREE_SITTER_GRAMMARS_DIR}/licenses/tree-sitter-unicode-LICENSE" COPYONLY)

  foreach(item IN LISTS lm_ts_grammars)
    # <name>=<path>=<repository folder>
    string(REPLACE "=" ";" parts "${item}")
    list(GET parts 0 grammar)
    list(GET parts 1 path)
    list(GET parts 2 repo_dir)
    if(path STREQUAL ".")
      set(source_dir "${repo_dir}")
    else()
      set(source_dir "${repo_dir}/${path}")
    endif()
    set(sources "${source_dir}/src/parser.c")
    if(EXISTS "${source_dir}/src/scanner.c")
      list(APPEND sources "${source_dir}/src/scanner.c")
    endif()
    add_library(tree-sitter-${grammar} MODULE ${sources})
    set_target_properties(tree-sitter-${grammar} PROPERTIES
      PREFIX ""
      SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY "${LM_TREE_SITTER_GRAMMARS_DIR}"
      C_VISIBILITY_PRESET default)
    target_include_directories(tree-sitter-${grammar} PRIVATE "${source_dir}/src")
    # generated code: its warnings are not ours to fix
    target_compile_options(tree-sitter-${grammar} PRIVATE -w)
    list(APPEND LM_TREE_SITTER_MODULES tree-sitter-${grammar})
    lm_tree_sitter_bundle_queries(${grammar} "${source_dir}" "${repo_dir}")
  endforeach()
endif()
add_custom_target(tree-sitter-grammars ALL DEPENDS ${LM_TREE_SITTER_MODULES})
