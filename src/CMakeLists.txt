set(src_headers)
set(src_sources)
set(LIBS _poco_json _poco_xml _poco_net _poco_foundation _fmt)

file(GLOB src_headers "Client/*.h" "Server/*.h")
file(GLOB src_sources "Client/*.cpp" "Server/*.cpp")

add_library (db STATIC ${src_headers} ${src_sources})
target_link_libraries(db ${LIBS})
target_include_directories(db PUBLIC ${CMAKE_SOURCE_DIR}/src)