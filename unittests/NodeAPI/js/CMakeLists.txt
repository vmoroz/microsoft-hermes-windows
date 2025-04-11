set(testJSFiles
  basics/hello.js
  common/assert.js
  common/common.js
  js-native-api/2_function_arguments/test.js
  js-native-api/3_callbacks/test.js
  js-native-api/4_object_factory/test.js
  js-native-api/5_function_factory/test.js
  js-native-api/6_object_wrap/test-basic-finalizer.js
  js-native-api/6_object_wrap/test-object-wrap-ref.js
  js-native-api/6_object_wrap/test.js
  js-native-api/7_factory_wrap/test.js
  js-native-api/8_passing_wrapped/test.js
  js-native-api/test_array/test.js
  js-native-api/test_bigint/test.js
  js-native-api/test_cannot_run_js/test.js
  js-native-api/test_constructor/test.js
  js-native-api/test_constructor/test2.js
  js-native-api/test_constructor/test_null.js
  js-native-api/test_conversions/test.js
  js-native-api/test_dataview/test.js
  js-native-api/test_date/test.js
  js-native-api/test_error/test.js
  js-native-api/test_exception/test.js
  js-native-api/test_exception/testFinalizerException.js
  js-native-api/test_finalizer/test.js
  js-native-api/test_finalizer/test_fatal_finalize.js
  js-native-api/test_function/test.js
  js-native-api/test_general/test.js
  js-native-api/test_general/testEnvCleanup.js
  js-native-api/test_general/testFinalizer.js
  js-native-api/test_general/testGlobals.js
  js-native-api/test_general/testInstanceOf.js
  js-native-api/test_general/testNapiRun.js
  js-native-api/test_general/testNapiStatus.js
  js-native-api/test_handle_scope/test.js
  js-native-api/test_instance_data/test.js
  js-native-api/test_new_target/test.js
  js-native-api/test_number/test.js
  js-native-api/test_number/test_null.js
  js-native-api/test_object/test.js
  js-native-api/test_object/test_exceptions.js
  js-native-api/test_object/test_null.js
  js-native-api/test_promise/test.js
  js-native-api/test_properties/test.js
  js-native-api/test_reference/test.js
  js-native-api/test_reference/test_finalizer.js
  js-native-api/test_reference_double_free/test.js
  js-native-api/test_reference_double_free/test_wrap.js
  js-native-api/test_string/test.js
  js-native-api/test_string/test_null.js
  js-native-api/test_symbol/test1.js
  js-native-api/test_symbol/test2.js
  js-native-api/test_symbol/test3.js
  js-native-api/test_typedarray/test.js
)

if(WIN32)
  # "npx" interrupts current shell script execution without the "call"
  set(npx "call" "npx")
  set(yarn "call" "yarn")
else()
  set(npx "npx")
  set(yarn "yarn")
endif()

# copy JS Tools package files
add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/babel.config.js
    ${CMAKE_CURRENT_BINARY_DIR}/package.json
    ${CMAKE_CURRENT_BINARY_DIR}/yarn.lock
  COMMAND ${CMAKE_COMMAND} -E copy_if_different 
    ${CMAKE_CURRENT_SOURCE_DIR}/babel.config.js
    ${CMAKE_CURRENT_SOURCE_DIR}/package.json
    ${CMAKE_CURRENT_SOURCE_DIR}/yarn.lock
    ${CMAKE_CURRENT_BINARY_DIR}
)
add_custom_target(copyNodeApiJSToolsFiles)

# run "yarn install"
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_modules.sha1
  DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/package.json
    ${CMAKE_CURRENT_BINARY_DIR}/yarn.lock
  COMMAND ${yarn} install --frozen-lockfile
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_custom_target(installNodeApiTestJsTools
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/node_modules.sha1
)
add_dependencies(installNodeApiTestJsTools copyNodeApiJSToolsFiles)

# TODO: replace "metro-react-native-babel-preset" with a modern alternative.
# add the Babel transform commands for each test JS file
foreach(testJSFile ${testJSFiles})
  # create target directory
  get_filename_component(testJSDir ${testJSFile} DIRECTORY)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${testJSDir})

  # generate Hermes-compatible JavaScript code
  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}
      ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/../${testJSFile}
      ${CMAKE_CURRENT_BINARY_DIR}/babel.config.js
      ${CMAKE_CURRENT_BINARY_DIR}/node_modules.sha1  
    COMMAND ${npx}
      "babel"
      "--retain-lines"
      "--source-maps"
        "true"
      "--out-file"
        "${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}"
      "--source-map-target"
        "${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map"
      "${CMAKE_CURRENT_SOURCE_DIR}/../${testJSFile}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )

  # build a list of all outputs
  list(APPEND transformedJSFiles
    ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}
    ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map
  )
endforeach()

# run the Babel transforms for all required output files
add_custom_target(transformJSFiles
  DEPENDS ${transformedJSFiles}
)
add_dependencies(transformJSFiles installNodeApiTestJsTools copyNodeApiJSToolsFiles)
