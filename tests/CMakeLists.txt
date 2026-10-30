find_package(Qt6 REQUIRED COMPONENTS Core Test Network)
set(CMAKE_AUTOMOC ON)

function(fg_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE fg_core Qt6::Core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

fg_test(test_parse)
fg_test(test_samplers)
fg_test(test_trace)
target_link_libraries(test_samplers PRIVATE Qt6::Network)

add_executable(test_scheme test_scheme.cpp)
target_link_libraries(test_scheme PRIVATE fg_appearance Qt6::Test)
add_test(NAME test_scheme COMMAND test_scheme)
