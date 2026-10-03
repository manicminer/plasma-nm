/*
    SPDX-FileCopyrightText: 2026 Tom Bamford <tom@bamford.io>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include <memory>

#include <QTest>
#include <QCheckBox>
#include "openvpn3widget.h"

#include <KPluginMetaData>

#include <NetworkManagerQt/VpnSetting>

#include "connectioneditorbase.h"
#include "nm-openvpn3-service.h"
#include "settingwidget.h"
#include "vpnuiplugin.h"

using namespace Qt::Literals::StringLiterals;

namespace
{
NetworkManager::VpnSetting::Ptr someSetting()
{
    NetworkManager::VpnSetting source;
    source.setServiceType(QLatin1String(NM_DBUS_SERVICE_OPENVPN3));
    source.setData({{u"profile"_s, QString::fromLatin1(QByteArray("client\nremote vpn.example.org 1194\n").toBase64())}});

    auto setting = NetworkManager::VpnSetting::Ptr(new NetworkManager::VpnSetting);
    setting->fromMap(source.toMap());
    return setting;
}
}

/**
 * The plugin as the rest of Plasma sees it: found by service type, loaded out
 * of the shared object, and able to produce both of its widgets.
 *
 * This is the step the unit tests cannot cover, because they link the code
 * directly; here the metadata, the factory macro and the install location all
 * have to be right.
 */
class Openvpn3PluginTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void omittedProfileFlagsStillRequestSecrets()
    {
        auto vpn = someSetting();
        vpn->setData({{u"profile-storage"_s, u"secret"_s}});
        QVERIFY(ConnectionEditorBase::shouldRequestVpnSecrets(vpn));
        vpn->setData({});
        QVERIFY(!ConnectionEditorBase::shouldRequestVpnSecrets(vpn));
        vpn->setData({{u"password-flags"_s, u"2"_s}});
        QVERIFY(!ConnectionEditorBase::shouldRequestVpnSecrets(vpn));
        vpn->setData({{u"password-flags"_s, u"1"_s}});
        QVERIFY(ConnectionEditorBase::shouldRequestVpnSecrets(vpn));
    }

    void hostPreservesConnectionPropertiesAcrossReimport()
    {
        class Editor : public ConnectionEditorBase {
        public:
            explicit Editor(const NetworkManager::ConnectionSettings::Ptr &cs) : ConnectionEditorBase(cs) { initialize(); }
        protected:
            void addWidget(QWidget *widget, const QString &) override { widget->setParent(this); }
            QString connectionName() const override { return u"edited name"_s; }
        };
        auto cs = NetworkManager::ConnectionSettings::Ptr(new NetworkManager::ConnectionSettings(NetworkManager::ConnectionSettings::Vpn));
        cs->setId(u"original name"_s);
        cs->setUuid(u"24f1ab6c-72a1-4a01-a898-de153b0f7564"_s);
        cs->setInterfaceName(u"reviewtun"_s);
        auto map = cs->toMap();
        map[u"connection"_s][u"autoconnect-retries"_s] = 7;
        map[u"connection"_s][u"gateway-ping-timeout"_s] = uint(1234);
        map[u"connection"_s][u"mdns"_s] = 2;
        map[u"vpn"_s] = someSetting()->toMap();
        cs->fromMap(map);
        const auto original = cs->toMap().value(u"connection"_s);
        Editor editor(cs);
        OpenVpn3SettingWidget *page = nullptr;
        for (auto widget : editor.findChildren<SettingWidget *>()) {
            if (widget->type() == u"vpn"_s) {
                QCOMPARE(QString::fromLatin1(widget->metaObject()->className()), u"OpenVpn3SettingWidget"_s);
                page = static_cast<OpenVpn3SettingWidget *>(widget);
            }
        }
        QVERIFY(page); // Created through actual host plugin loading.
        QString error;
        QVERIFY2(page->importProfile(QString::fromLatin1(OPENVPN3_TEST_DATA_DIR) + u"/office.ovpn"_s, &error), qPrintable(error));
        const auto replacement = page->setting();
        auto lateReply = someSetting();
        lateReply->setSecrets({{u"profile"_s, QString::fromLatin1(QByteArray("client\nremote old.example.org\nauth-user-pass\n").toBase64())},
                               {u"password"_s, u"old-password"_s}});
        page->loadSecrets(lateReply);
        QCOMPARE(page->setting(), replacement);
        auto allUsers = editor.findChild<QCheckBox *>(u"allUsers"_s);
        QVERIFY(allUsers);
        allUsers->setChecked(false);
        const auto saved = editor.setting().value(u"connection"_s);
        for (const auto &key : {u"interface-name"_s, u"autoconnect-retries"_s, u"gateway-ping-timeout"_s, u"mdns"_s, u"uuid"_s}) {
            QVERIFY2(original.contains(key), qPrintable(key));
            QCOMPARE(saved.value(key), original.value(key));
        }
        QCOMPARE(saved.value(u"id"_s).toString(), u"edited name"_s);
        QVERIFY(saved.value(u"permissions"_s) != original.value(u"permissions"_s));
    }

    void emptySecretLoadsFailClosed_data()
    {
        QTest::addColumn<bool>("emptyValue");
        QTest::addColumn<bool>("alreadyLoaded");
        QTest::newRow("missing-locked") << false << false;
        QTest::newRow("empty-locked") << true << false;
        QTest::newRow("missing-loaded") << false << true;
        QTest::newRow("empty-loaded") << true << true;
    }
    void emptySecretLoadsFailClosed()
    {
        QFETCH(bool, emptyValue);
        QFETCH(bool, alreadyLoaded);
        const auto result = VpnUiPlugin::loadPluginForType(nullptr, QLatin1String(NM_DBUS_SERVICE_OPENVPN3));
        QVERIFY(result);
        std::unique_ptr<VpnUiPlugin> plugin(result.plugin);
        auto vpn = someSetting();
        const QString original = vpn->data().value(u"profile"_s);
        // A stale public value must not become a fallback.
        vpn->setData({{u"profile-storage"_s, u"secret"_s}, {u"profile-flags"_s, u"0"_s}, {u"profile"_s, original}});
        if (alreadyLoaded) {
            vpn->setSecrets({{u"profile"_s, original}});
        }
        std::unique_ptr<SettingWidget> widget(plugin->widget(vpn, nullptr));
        const auto before = widget->setting();
        auto reply = someSetting();
        reply->setSecrets(emptyValue ? NMStringMap{{u"profile"_s, QString()}} : NMStringMap{});
        widget->loadSecrets(reply);
        const auto after = widget->setting();
        if (alreadyLoaded) {
            QVERIFY(widget->isValid());
            QCOMPARE(qdbus_cast<NMStringMap>(after.value(u"secrets"_s)).value(u"profile"_s), original);
        } else {
            QVERIFY(!widget->isValid());
            QCOMPARE(after, before);
            QVERIFY(!qdbus_cast<NMStringMap>(after.value(u"secrets"_s)).contains(u"profile"_s));
        }
    }

    void theMetadataNamesTheService();
    void thePluginLoadsForItsServiceType();
    void thePluginProducesItsWidgets();
    void itClaimsTheSameFilesAsOpenVpn();
    void exportIsRefusedRatherThanLeakingTheKeys();
    void bothOpenVpnPluginsAreFoundForAnOvpnFile();
    void anExtensionNobodyClaimsFindsNoPlugin();
    void theConnectionEditorOffersAnIpv6PageForThisServiceType();
};

void Openvpn3PluginTest::theMetadataNamesTheService()
{
    const QList<KPluginMetaData> plugins = KPluginMetaData::findPlugins(u"plasma/network/vpn"_s, [](const KPluginMetaData &data) {
        return data.value(u"X-NetworkManager-Services"_s) == QLatin1String(NM_DBUS_SERVICE_OPENVPN3);
    });

    QCOMPARE(plugins.size(), 1);
    QCOMPARE(plugins.constFirst().name(), u"OpenVPN 3"_s);
}

void Openvpn3PluginTest::thePluginLoadsForItsServiceType()
{
    const auto result = VpnUiPlugin::loadPluginForType(nullptr, QLatin1String(NM_DBUS_SERVICE_OPENVPN3));

    QVERIFY2(result, qPrintable(result.errorString));
    delete result.plugin;
}

void Openvpn3PluginTest::thePluginProducesItsWidgets()
{
    const auto result = VpnUiPlugin::loadPluginForType(nullptr, QLatin1String(NM_DBUS_SERVICE_OPENVPN3));
    QVERIFY2(result, qPrintable(result.errorString));
    std::unique_ptr<VpnUiPlugin> plugin(result.plugin);

    const auto setting = someSetting();
    std::unique_ptr<SettingWidget> editor(plugin->widget(setting, nullptr));
    QVERIFY(editor);
    QCOMPARE(editor->type(), u"vpn"_s);
    QVERIFY(editor->isValid());

    std::unique_ptr<SettingWidget> prompt(plugin->askUser(setting, {u"challenge-response"_s}, nullptr));
    QVERIFY(prompt);
}

void Openvpn3PluginTest::itClaimsTheSameFilesAsOpenVpn()
{
    const auto result = VpnUiPlugin::loadPluginForType(nullptr, QLatin1String(NM_DBUS_SERVICE_OPENVPN3));
    QVERIFY(result);
    std::unique_ptr<VpnUiPlugin> plugin(result.plugin);

    // Deliberately the same as the OpenVPN 2 plugin: both read .ovpn files,
    // which is why the import dialog has to ask which one is meant.
    QCOMPARE(plugin->supportedFileExtensions(), (QStringList{u"*.ovpn"_s, u"*.conf"_s}));
}

void Openvpn3PluginTest::exportIsRefusedRatherThanLeakingTheKeys()
{
    const auto result = VpnUiPlugin::loadPluginForType(nullptr, QLatin1String(NM_DBUS_SERVICE_OPENVPN3));
    QVERIFY(result);
    std::unique_ptr<VpnUiPlugin> plugin(result.plugin);

    auto connection = NetworkManager::ConnectionSettings::Ptr(new NetworkManager::ConnectionSettings(NetworkManager::ConnectionSettings::Vpn));
    const auto exported = plugin->exportConnectionSettings(connection, u"/nonexistent/should-not-be-written.ovpn"_s);

    QVERIFY(!exported);
    QVERIFY(!exported.errorMessage().isEmpty());
}

void Openvpn3PluginTest::bothOpenVpnPluginsAreFoundForAnOvpnFile()
{
    // The import has to ask which one was meant, and it can only ask if
    // finding them both is what discovery does rather than stopping at the
    // first plugin that claims the extension.
    const QList<KPluginMetaData> claiming = VpnUiPlugin::pluginsForFileExtension(u"ovpn"_s);

    QStringList services;
    for (const KPluginMetaData &metaData : claiming) {
        services.append(metaData.value(u"X-NetworkManager-Services"_s));
    }
    QVERIFY2(services.contains(QLatin1String(NM_DBUS_SERVICE_OPENVPN3)), qPrintable(services.join(u", "_s)));
    QVERIFY2(services.contains(u"org.freedesktop.NetworkManager.openvpn"_s), qPrintable(services.join(u", "_s)));
    QVERIFY(claiming.size() >= 2);
}

void Openvpn3PluginTest::anExtensionNobodyClaimsFindsNoPlugin()
{
    QVERIFY(VpnUiPlugin::pluginsForFileExtension(u"not-a-vpn-profile"_s).isEmpty());
}

void Openvpn3PluginTest::theConnectionEditorOffersAnIpv6PageForThisServiceType()
{
    // openvpn3 hands IPv6 over to NetworkManager, so the page has something
    // to set; without this the editor would offer IPv4 and nothing else.
    QVERIFY(ConnectionEditorBase::hasIpv6Page(NetworkManager::ConnectionSettings::Vpn, QLatin1String(NM_DBUS_SERVICE_OPENVPN3)));
    QVERIFY(ConnectionEditorBase::hasIpv6Page(NetworkManager::ConnectionSettings::Vpn, u"org.freedesktop.NetworkManager.openvpn"_s));
    // And it is still not offered for the VPN services that do not.
    QVERIFY(!ConnectionEditorBase::hasIpv6Page(NetworkManager::ConnectionSettings::Vpn, u"org.freedesktop.NetworkManager.pptp"_s));
}

QTEST_MAIN(Openvpn3PluginTest)

#include "openvpn3plugintest.moc"
