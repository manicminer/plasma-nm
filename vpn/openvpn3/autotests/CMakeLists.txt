include(ECMAddTests)

find_package(Qt6 ${QT_MIN_VERSION} NO_MODULE REQUIRED Test)

# Where the synthetic example.org fixtures live.
add_definitions(-DOPENVPN3_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\")

ecm_add_tests(
    openvpn3profiletest.cpp
    openvpn3storagetest.cpp
    NAME_PREFIX openvpn3-
    LINK_LIBRARIES Qt::Test plasmanetworkmanagement_openvpn3_core
)

ecm_add_tests(
    openvpn3directivestest.cpp
    openvpn3availabilitytest.cpp
    openvpn3importtest.cpp
    openvpn3widgettest.cpp
    openvpn3authtest.cpp
    NAME_PREFIX openvpn3-
    GUI
    LINK_LIBRARIES Qt::Test Qt::Widgets Qt::DBus KF6::CoreAddons plasmanetworkmanagement_openvpn3_core
)

ecm_add_test(
    openvpn3plugintest.cpp
    TEST_NAME openvpn3plugintest
    NAME_PREFIX openvpn3-
    GUI
    LINK_LIBRARIES Qt::Test Qt::Widgets KF6::CoreAddons plasmanetworkmanagement_openvpn3_core
)
add_dependencies(openvpn3plugintest plasmanetworkmanagement_openvpn3ui mirror_openvpn_plugin_for_openvpn3_tests)
set_tests_properties(openvpn3-openvpn3plugintest PROPERTIES
    ENVIRONMENT_MODIFICATION QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin
)

# Not a test: renders the editor for review. See openvpn3screenshot.cpp.
add_executable(openvpn3screenshot openvpn3screenshot.cpp)
target_link_libraries(openvpn3screenshot Qt::Widgets plasmanetworkmanagement_openvpn3_core)

add_dependencies(openvpn3availabilitytest plasmanetworkmanagement_openvpn3ui)
set_tests_properties(openvpn3-openvpn3availabilitytest PROPERTIES
    ENVIRONMENT_MODIFICATION QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin
)
