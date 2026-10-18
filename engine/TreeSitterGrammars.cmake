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
# Queries may take in another repository's (typescript's take javascript's,
# cpp's take c's). tree-sitter-typescript's "flow" stays out: its module
# would hold tree_sitter_tsx, never loaded.
set(LM_TREE_SITTER_REPOS
  "ionide/tree-sitter-fsharp aefd0c8741bdf3aeb827a228aa4a996a6536697e 5c40b126e9c4641ac8775d2cd9d390503fd3ec1307531412143969f263b0f0ad fsharp=fsharp fsharp_signature=fsharp_signature"
  "justinmk/tree-sitter-ini f0285fe577ad298ad79f8633e643ad60646e3027 349a11172a4b0853a7af8e8e2f9e66811bf718dc742cc76b5798b0dd71239fe2 ini=."
  "tree-sitter-grammars/tree-sitter-markdown f969cd3ae3f9fbd4e43205431d0ae286014c05b5 45ec28324b75ec80788775a6aed26888f55b306981fede9cbe35d7348ae80469 markdown=tree-sitter-markdown markdown_inline=tree-sitter-markdown-inline"
  "tree-sitter-grammars/tree-sitter-properties 579b62f5ad8d96c2bb331f07d1408c92767531d9 97399c48a90c2b6809870e14f34d2ac0bcd8842a1b4c2031d5be242170d4d2e0 properties=."
  "tree-sitter-grammars/tree-sitter-toml 64b56832c2cffe41758f28e05c756a3a98d16f41 feeb2e1cf531588cdcfb9c57292620151a08597f18a98dad26cc17fd4b544dcb toml=."
  "tree-sitter-grammars/tree-sitter-xml 4b64dd3a03ec002258d6268d712fd93716d6ab57 e93293550a10535197c6d682a79fcf2942f6a3b1277118d37beb0dcf0a6255cb xml=xml dtd=dtd"
  "tree-sitter-grammars/tree-sitter-yaml 7708026449bed86239b1cd5bce6e3c34dbca6415 6e8cb69a4f7a05478b1143d38bd10df2163170a6678df1848f3be89c8b7c5f3b yaml=."
  "tree-sitter/tree-sitter-bash a06c2e4415e9bc0346c6b86d401879ffb44058f7 879e8951ea2cc82455407e3eda0293319657ec53e83191e0fb5a67430d10d804 bash=."
  "tree-sitter/tree-sitter-cpp f41e1a044c8a84ea9fa8577fdd2eab92ec96de02 ad4b9b56602e9b14082637988f0042521006819960ed8c308bc24ded9ceb9331 cpp=."
  "tree-sitter/tree-sitter-c-sharp cac6d5fb595f5811a076336682d5d595ac1c9e85 e4cd8a1c118a1e176931e9e0a635fc55e3d0f69e1649e40b74d72bda918e948d c-sharp=."
  "tree-sitter/tree-sitter-css dda5cfc5722c429eaba1c910ca32c2c0c5bb1a3f c47392e483feb9137d8f0acf9ca6e916b820122c260324454a9980c75c98c3c5 css=."
  "tree-sitter/tree-sitter-c b780e47fc780ddc8da13afa35a3f4ed5c157823d dc3979e40fce678c27273ba8ab93b48b5acd6b91d1dfde6a4451222d366d689f c=."
  "tree-sitter/tree-sitter-go 1547678a9da59885853f5f5cc8a99cc203fa2e2c a71543d63f82b917457f47a3f6dbecd8087592fabc49a757da82c3e00c0e93de go=."
  "tree-sitter/tree-sitter-html 5a5ca8551a179998360b4a4ca2c0f366a35acc03 5a198c1bb13ad848ccf2b91dca3d8ac39af63f4da98ad4814b8e27107889eb94 html=."
  "tree-sitter/tree-sitter-javascript 44c892e0be055ac465d5eeddae6d3e194424e7de a13fa28148e41bb939b2c2b8cd73c4b4295273b70cbac55a7a589020f52b9611 javascript=."
  "tree-sitter/tree-sitter-java 94703d5a6bed02b98e438d7cad1136c01a60ba2c 39dea56cc3bb5fb1658f79b195a92b6f08885886bca636adf2dc6243af641c47 java=."
  "tree-sitter/tree-sitter-json ee35a6ebefcef0c5c416c0d1ccec7370cfca5a24 9e4a7391cc3a9f4b87b382956dd01bc1f99fb1afcdc86b2c152c8a1bd14444d5 json=."
  "tree-sitter/tree-sitter-php 92b5271b60bec77fb65b5e5bc41561e8dac81299 2dfa7643857b94a273b6b413b4cb694aef9c9898e17403f97b66a9ba9c163891 php=php php_only=php_only"
  "tree-sitter/tree-sitter-python 293fdc02038ee2bf0e2e206711b69c90ac0d413f b74d8ba6730535354175436208fe1452fd464f235263959ca40b8bf32cb23dc6 python=."
  "tree-sitter/tree-sitter-ruby 71bd32fb7607035768799732addba884a37a6210 e520d4cda2805db82b5a44ec84ba048e30d91770851b4a173490e0d45e4cb555 ruby=."
  "tree-sitter/tree-sitter-rust 77a3747266f4d621d0757825e6b11edcbf991ca5 dee82ddfd01bfc3a8ed201cc03b56448107d3217a4b3a7a8fc7fa6bc32b2405b rust=."
  "tree-sitter/tree-sitter-typescript f975a621f4e7f532fe322e13c4f79495e0a7b2e7 4de2e82e557810eecb93cb3b31fbb3bd28ba4c91ba9a6c6164bcaa074125b7b5 typescript=typescript tsx=tsx"
)
# WinMerge's own queries (its tree-sitter-grammars' custom_queries), which
# build-grammars.ps1 copies over the bundled ones of the same name
set(LM_TREE_SITTER_CUSTOM_QUERIES "${CMAKE_CURRENT_SOURCE_DIR}/Externals/tree-sitter-grammars/custom_queries")

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
    # a custom query takes the place of a bundle there is, as it is
    if(EXISTS "${LM_TREE_SITTER_CUSTOM_QUERIES}/${grammar}-${kind}.scm")
      file(COPY_FILE "${LM_TREE_SITTER_CUSTOM_QUERIES}/${grammar}-${kind}.scm" "${target}" ONLY_IF_DIFFERENT)
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
