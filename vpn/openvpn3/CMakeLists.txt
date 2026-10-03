add_definitions(-DTRANSLATION_DOMAIN=\"plasmanetworkmanagement_openvpn3ui\")

# Everything except the plugin entry point lives in a static library so that
# the autotests can link it without loading the plugin.
add_library(plasmanetworkmanagement_openvpn3_core STATIC)
target_sources(plasmanetworkmanagement_openvpn3_core PRIVATE
    openvpn3profile.cpp
    openvpn3profile.h
    openvpn3storage.cpp
    openvpn3storage.h
    openvpn3importer.cpp
    openvpn3importer.h
    openvpn3directiveswidget.cpp
    openvpn3directiveswidget.h
    openvpn3widget.cpp
    openvpn3widget.h
    openvpn3auth.cpp
    openvpn3auth.h
    nm-openvpn3-service.h
)
ecm_qt_declare_logging_category(plasmanetworkmanagement_openvpn3_core
    HEADER plasma_nm_openvpn3.h
    IDENTIFIER PLASMA_NM_OPENVPN3_LOG
    CATEGORY_NAME org.kde.plasma.nm.openvpn3
    DESCRIPTION "Plasma NM (openvpn3 plugin)"
    EXPORT PLASMANM
)
set_property(TARGET plasmanetworkmanagement_openvpn3_core PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(plasmanetworkmanagement_openvpn3_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(plasmanetworkmanagement_openvpn3_core
PUBLIC
    plasmanm_editor
    KF6::NetworkManagerQt
    Qt::Widgets
    Qt::DBus
    PkgConfig::NETWORKMANAGER
PRIVATE
    KF6::I18n
    KF6::WidgetsAddons
    Qt6Keychain::Qt6Keychain
)

add_library(plasmanetworkmanagement_openvpn3ui MODULE)
target_sources(plasmanetworkmanagement_openvpn3ui PRIVATE
    openvpn3.cpp
    openvpn3.h
)
target_link_libraries(plasmanetworkmanagement_openvpn3ui
    plasmanetworkmanagement_openvpn3_core
    plasmanm_internal
    plasmanm_editor
    KF6::CoreAddons
    KF6::I18n
)

install(TARGETS plasmanetworkmanagement_openvpn3ui DESTINATION ${KDE_INSTALL_PLUGINDIR}/plasma/network/vpn)

if(BUILD_TESTING)
    # KPluginMetaData looks for the plugin under
    # <QT_PLUGIN_PATH>/plasma/network/vpn, so mirror the built module there
    # and the load test can find it without an install first.
    add_custom_command(TARGET plasmanetworkmanagement_openvpn3ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/plasma/network/vpn
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:plasmanetworkmanagement_openvpn3ui> ${CMAKE_BINARY_DIR}/bin/plasma/network/vpn/
        COMMENT "Mirroring the openvpn3 VPN plugin where KPluginMetaData looks for it"
    )

    # The OpenVPN 2 plugin goes next to it: both claim .ovpn, and that the
    # import has to ask which one is meant can only be tested with both of
    # them there to be found.  It is built in another directory, so the copy
    # is a target of its own rather than a post-build step on it.
    add_custom_target(mirror_openvpn_plugin_for_openvpn3_tests ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/plasma/network/vpn
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:plasmanetworkmanagement_openvpnui> ${CMAKE_BINARY_DIR}/bin/plasma/network/vpn/
        COMMENT "Mirroring the OpenVPN VPN plugin for the openvpn3 discovery test"
    )
    add_dependencies(mirror_openvpn_plugin_for_openvpn3_tests plasmanetworkmanagement_openvpnui)

    add_subdirectory(autotests)
endif()
