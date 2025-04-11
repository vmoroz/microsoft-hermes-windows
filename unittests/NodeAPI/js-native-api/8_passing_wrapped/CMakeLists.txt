add_library(8_passing_wrapped MODULE 8_passing_wrapped.cc myobject.cc)
target_link_libraries(8_passing_wrapped PRIVATE nodeApi)
