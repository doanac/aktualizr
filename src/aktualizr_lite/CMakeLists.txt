if(BUILD_OSTREE)
add_executable(aktualizr-lite main.cc)
target_link_libraries(aktualizr-lite aktualizr_static_lib ${AKTUALIZR_EXTERNAL_LIBS})

aktualizr_source_file_checks(main.cc)

install(TARGETS aktualizr-lite RUNTIME DESTINATION bin COMPONENT aktualizr-lite)

endif(BUILD_OSTREE)
# vim: set tabstop=4 shiftwidth=4 expandtab:
