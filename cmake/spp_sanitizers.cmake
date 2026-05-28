function(spp_apply_sanitizers target)
  if(NOT SPP_ENABLE_SANITIZERS)
    return()
  endif()
  if(MSVC)
    return()
  endif()
  target_compile_options(${target} PRIVATE
    -fsanitize=address
    -fsanitize=undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all
  )
  target_link_options(${target} PRIVATE
    -fsanitize=address
    -fsanitize=undefined
  )
endfunction()
