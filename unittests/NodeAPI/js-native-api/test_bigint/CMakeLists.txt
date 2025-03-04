add_library(test_bigint MODULE test_bigint.c)
target_link_libraries(test_bigint PRIVATE nodeApi)
