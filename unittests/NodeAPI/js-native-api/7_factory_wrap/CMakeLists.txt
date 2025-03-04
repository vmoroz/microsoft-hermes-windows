add_library(7_factory_wrap MODULE 7_factory_wrap.cc myobject.cc)
target_link_libraries(7_factory_wrap PRIVATE nodeApi)
