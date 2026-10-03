/*
    SPDX-FileCopyrightText: 2026 Tom Bamford <tom@bamford.io>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include <QSet>
#include <QTest>

#include "openvpn3profile.h"

using namespace Qt::Literals::StringLiterals;

namespace
{
const auto kRich = QStringLiteral(
    "##\n"
    "# An office profile\n"
    "##\n"
    "client\n"
    "dev tun\n"
    "proto udp\n"
    "remote vpn1.example.net 1194 udp\n"
    "remote vpn2.example.net 443 tcp\n"
    "remote vpn1.example.net 1194 udp\n"
    "\n"
    "; a semicolon comment\n"
    "auth-user-pass\n"
    "pull-filter ignore \"redirect-gateway\"\n"
    "setenv opt 'single quoted value'\n"
    "verify-x509-name \"C=NO, O=Example, CN=server\" subject\n"
    "<connection>\n"
    "remote fallback.example.net 1194 udp\n"
    "http-proxy proxy.example.net 8080\n"
    "</connection>\n"
    "<ca>\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBsyntheticTESTDATA\n"
    "-----END CERTIFICATE-----\n"
    "</ca>\n"
    "key-direction 1\n"
    "some-directive-we-have-never-heard-of 1 2 3\n");
}

class Openvpn3ProfileTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unterminatedEntryBoundaries_data()
    {
        QTest::addColumn<QString>("newline");
        QTest::addColumn<bool>("move");
        QTest::newRow("append-lf") << u"\n"_s << false;
        QTest::newRow("append-crlf") << u"\r\n"_s << false;
        QTest::newRow("move-lf") << u"\n"_s << true;
        QTest::newRow("move-crlf") << u"\r\n"_s << true;
    }
    void unterminatedEntryBoundaries()
    {
        QFETCH(QString, newline);
        QFETCH(bool, move);
        const QString original = u"client"_s + newline + u"remote first.example.org"_s;
        auto profile = Openvpn3Profile::fromText(original);
        QCOMPARE(profile.toText(), original);
        if (move) {
            profile.move(0, 1);
            QCOMPARE(profile.toText(), u"remote first.example.org"_s + newline + u"client"_s + newline);
        } else {
            profile.append(Openvpn3Profile::directive(u"remote"_s, {u"second.example.org"_s}));
            QVERIFY(profile.toText().startsWith(original + newline + u"remote second.example.org"_s));
            QCOMPARE(Openvpn3Profile::fromText(profile.toText()).remoteHosts().size(), 2);
        }
    }

    void roundTripIsLossless_data();
    void roundTripIsLossless();
    void parsesEntryKinds();
    void keepsDuplicatesInOrder();
    void keepsBlocksVerbatim();
    void doesNotDescendIntoConnectionBlocks();
    void parsesQuoting_data();
    void parsesQuoting();
    void untouchedEntriesKeepTheirQuoting();
    void editingOneEntryLeavesTheOthersAlone();
    void editingKeepsArgumentsItWasNotToldAbout();
    void setDirectiveAppendsWhenMissing();
    void setPresentKeepsExistingArguments();
    void removeAllRemovesEveryDuplicate();
    void blocksCanBeReplacedAndAdded();
    void entriesCanBeInsertedMovedAndRemoved();
    void quotingRoundTrips_data();
    void quotingRoundTrips();
    void unterminatedBlockIsKeptVerbatim();
    void commentsAreNeverMatchedAsDirectives();
    void spotsKeyMaterialThatCouldNeedAPassphrase_data();
    void spotsKeyMaterialThatCouldNeedAPassphrase();

    void optionsAreFoundInsideConnectionBlocks();
    void opaqueBlocksAreNeverDescendedInto();
    void nestedInlineCredentialsAreFoundAsABlock();
    void remoteHostsComeFromEveryScope_data();
    void remoteHostsComeFromEveryScope();
    void normalizationIsNeededOnlyForFilesAndCredentials_data();
    void normalizationIsNeededOnlyForFilesAndCredentials();
    void entriesKeepTheirIdentityWhileTheDocumentChanges();
    void parsedEntriesHaveDistinctIdentities();
    void crlfIsVisibleAsTheDocumentsConvention_data();
    void crlfIsVisibleAsTheDocumentsConvention();
};

void Openvpn3ProfileTest::roundTripIsLossless_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("rich") << kRich;
    QTest::newRow("empty") << QString();
    QTest::newRow("no trailing newline") << QStringLiteral("client\nremote a.example.net 1194");
    QTest::newRow("crlf") << QStringLiteral("client\r\nremote a.example.net 1194\r\n<ca>\r\nPEM\r\n</ca>\r\n");
    QTest::newRow("odd whitespace") << QStringLiteral("  client   \n\t remote   a.example.net    1194  \n\n\n");
    QTest::newRow("only comments") << QStringLiteral("# one\n; two\n");
    QTest::newRow("block without trailing newline") << QStringLiteral("client\n<ca>\nPEM\n</ca>");
}

void Openvpn3ProfileTest::roundTripIsLossless()
{
    QFETCH(QString, text);
    QCOMPARE(Openvpn3Profile::fromText(text).toText(), text);
}

void Openvpn3ProfileTest::parsesEntryKinds()
{
    const Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);

    QCOMPARE(profile.at(0).kind, Openvpn3Entry::Comment);
    QCOMPARE(profile.at(3).kind, Openvpn3Entry::Directive);
    QCOMPARE(profile.at(3).name, QStringLiteral("client"));
    QVERIFY(profile.at(3).arguments.isEmpty());
    QCOMPARE(profile.at(9).kind, Openvpn3Entry::Blank);
    QCOMPARE(profile.at(10).kind, Openvpn3Entry::Comment);
    QCOMPARE(profile.value(QStringLiteral("dev")), QStringLiteral("tun"));
    QVERIFY(profile.contains(QStringLiteral("auth-user-pass")));
    QVERIFY(!profile.contains(QStringLiteral("tls-crypt")));
    QCOMPARE(profile.arguments(QStringLiteral("some-directive-we-have-never-heard-of")),
             QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
}

void Openvpn3ProfileTest::keepsDuplicatesInOrder()
{
    const Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);
    const QList<int> remotes = profile.indexesOf(QStringLiteral("remote"));

    // Three top-level remotes, two of them identical, in document order.
    QCOMPARE(remotes.size(), 3);
    QCOMPARE(profile.at(remotes.at(0)).value(), QStringLiteral("vpn1.example.net"));
    QCOMPARE(profile.at(remotes.at(1)).value(), QStringLiteral("vpn2.example.net"));
    QCOMPARE(profile.at(remotes.at(2)).value(), QStringLiteral("vpn1.example.net"));
    QCOMPARE(profile.at(remotes.at(1)).arguments,
             QStringList({QStringLiteral("vpn2.example.net"), QStringLiteral("443"), QStringLiteral("tcp")}));
}

void Openvpn3ProfileTest::keepsBlocksVerbatim()
{
    const Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);

    QCOMPARE(profile.blockBody(QStringLiteral("ca")),
             QStringLiteral("-----BEGIN CERTIFICATE-----\nMIIBsyntheticTESTDATA\n-----END CERTIFICATE-----\n"));
    const int index = profile.indexOf(QStringLiteral("ca"));
    QVERIFY(index >= 0);
    QVERIFY(profile.at(index).isBlock());
}

void Openvpn3ProfileTest::doesNotDescendIntoConnectionBlocks()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);

    // The remote inside <connection> is part of that block's body, so it is
    // neither listed nor touched when the top-level remotes are edited.
    QCOMPARE(profile.indexesOf(QStringLiteral("remote")).size(), 3);
    QVERIFY(profile.blockBody(QStringLiteral("connection")).contains(QStringLiteral("fallback.example.net")));

    profile.setArguments(profile.indexOf(QStringLiteral("remote")), {QStringLiteral("new.example.net")});
    QVERIFY(profile.toText().contains(QStringLiteral("remote fallback.example.net 1194 udp")));
    QVERIFY(profile.toText().contains(QStringLiteral("http-proxy proxy.example.net 8080")));
}

void Openvpn3ProfileTest::parsesQuoting_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QStringList>("words");

    QTest::newRow("plain") << QStringLiteral("remote a.example.net 1194") << QStringList({u"remote"_s, u"a.example.net"_s, u"1194"_s});
    QTest::newRow("double quotes") << QStringLiteral("setenv x \"a b\"") << QStringList({u"setenv"_s, u"x"_s, u"a b"_s});
    QTest::newRow("single quotes") << QStringLiteral("setenv x 'a b'") << QStringList({u"setenv"_s, u"x"_s, u"a b"_s});
    QTest::newRow("escape in double") << QStringLiteral("setenv x \"a\\\"b\"") << QStringList({u"setenv"_s, u"x"_s, u"a\"b"_s});
    QTest::newRow("no escape in single") << QStringLiteral("setenv x 'a\\b'") << QStringList({u"setenv"_s, u"x"_s, u"a\\b"_s});
    QTest::newRow("trailing comment") << QStringLiteral("dev tun # a comment") << QStringList({u"dev"_s, u"tun"_s});
    QTest::newRow("empty argument") << QStringLiteral("setenv x \"\"") << QStringList({u"setenv"_s, u"x"_s, QString()});
    QTest::newRow("comma inside quotes") << QStringLiteral("verify-x509-name \"C=NO, O=X\" subject")
                                         << QStringList({u"verify-x509-name"_s, u"C=NO, O=X"_s, u"subject"_s});
}

void Openvpn3ProfileTest::parsesQuoting()
{
    QFETCH(QString, line);
    QFETCH(QStringList, words);
    QCOMPARE(Openvpn3Profile::splitArguments(line), words);
}

void Openvpn3ProfileTest::untouchedEntriesKeepTheirQuoting()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);
    const int index = profile.indexOf(QStringLiteral("setenv"));

    // Assigning the same values is not a change, so the single quotes stay.
    profile.setArguments(index, profile.at(index).arguments);
    QVERIFY(profile.toText().contains(QStringLiteral("setenv opt 'single quoted value'")));
}

void Openvpn3ProfileTest::editingOneEntryLeavesTheOthersAlone()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);
    const QList<int> remotes = profile.indexesOf(QStringLiteral("remote"));

    profile.setArguments(remotes.at(1), {QStringLiteral("vpn3.example.net"), QStringLiteral("443"), QStringLiteral("tcp")});
    QString text = profile.toText();

    QCOMPARE(text.count(QStringLiteral("remote vpn1.example.net 1194 udp\n")), 2);
    QVERIFY(text.contains(QStringLiteral("remote vpn3.example.net 443 tcp\n")));
    QVERIFY(!text.contains(QStringLiteral("vpn2.example.net")));
    // Everything else, including the comments and the unknown directive, is
    // still byte for byte what it was.
    QCOMPARE(text.replace(QStringLiteral("remote vpn3.example.net 443 tcp"), QStringLiteral("remote vpn2.example.net 443 tcp")), kRich);
}

void Openvpn3ProfileTest::editingKeepsArgumentsItWasNotToldAbout()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(QStringLiteral("client\nremote old.example.net 1194 udp\n"));
    const int index = profile.indexOf(QStringLiteral("remote"));

    // Changing only the host: the port and the transport are still there.
    QStringList arguments = profile.at(index).arguments;
    arguments[0] = QStringLiteral("new.example.net");
    profile.setArguments(index, arguments);

    QCOMPARE(profile.toText(), QStringLiteral("client\nremote new.example.net 1194 udp\n"));
}

void Openvpn3ProfileTest::setDirectiveAppendsWhenMissing()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(QStringLiteral("client\nremote a.example.net\n"));

    profile.setDirective(QStringLiteral("cipher"), {QStringLiteral("AES-256-GCM")});
    QCOMPARE(profile.toText(), QStringLiteral("client\nremote a.example.net\ncipher AES-256-GCM\n"));

    profile.setDirective(QStringLiteral("cipher"), {QStringLiteral("AES-128-GCM")});
    QCOMPARE(profile.toText(), QStringLiteral("client\nremote a.example.net\ncipher AES-128-GCM\n"));
}

void Openvpn3ProfileTest::setPresentKeepsExistingArguments()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(QStringLiteral("client\nauth-user-pass creds.txt\n"));

    profile.setPresent(QStringLiteral("auth-user-pass"), true);
    QCOMPARE(profile.toText(), QStringLiteral("client\nauth-user-pass creds.txt\n"));

    profile.setPresent(QStringLiteral("auth-user-pass"), false);
    QCOMPARE(profile.toText(), QStringLiteral("client\n"));

    profile.setPresent(QStringLiteral("auth-user-pass"), true);
    QCOMPARE(profile.toText(), QStringLiteral("client\nauth-user-pass\n"));
}

void Openvpn3ProfileTest::removeAllRemovesEveryDuplicate()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);

    profile.removeAll(QStringLiteral("remote"));
    QVERIFY(profile.indexesOf(QStringLiteral("remote")).isEmpty());
    // The one inside <connection> is untouched.
    QVERIFY(profile.toText().contains(QStringLiteral("remote fallback.example.net")));
}

void Openvpn3ProfileTest::blocksCanBeReplacedAndAdded()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);

    profile.setBlock(QStringLiteral("ca"), QStringLiteral("NEW-CA\n"));
    QCOMPARE(profile.blockBody(QStringLiteral("ca")), QStringLiteral("NEW-CA\n"));
    QVERIFY(profile.toText().contains(QStringLiteral("<ca>\nNEW-CA\n</ca>\n")));
    QVERIFY(!profile.toText().contains(QStringLiteral("MIIBsyntheticTESTDATA")));

    profile.setBlock(QStringLiteral("tls-crypt"), QStringLiteral("KEY"));
    // A body without its own newline still gets a well-formed closing tag.
    QVERIFY(profile.toText().endsWith(QStringLiteral("<tls-crypt>\nKEY\n</tls-crypt>\n")));
}

void Openvpn3ProfileTest::entriesCanBeInsertedMovedAndRemoved()
{
    Openvpn3Profile profile = Openvpn3Profile::fromText(QStringLiteral("client\nremote a.example.net\nremote b.example.net\n"));

    const QList<int> remotes = profile.indexesOf(QStringLiteral("remote"));
    profile.move(remotes.at(1), remotes.at(0));
    QCOMPARE(profile.toText(), QStringLiteral("client\nremote b.example.net\nremote a.example.net\n"));

    profile.insert(0, Openvpn3Profile::comment(QStringLiteral("edited by Plasma")));
    QVERIFY(profile.toText().startsWith(QStringLiteral("# edited by Plasma\n")));

    profile.append(Openvpn3Profile::directive(QStringLiteral("remote"), {QStringLiteral("c.example.net")}));
    QCOMPARE(profile.indexesOf(QStringLiteral("remote")).size(), 3);

    profile.removeAt(profile.indexesOf(QStringLiteral("remote")).at(1));
    QCOMPARE(profile.indexesOf(QStringLiteral("remote")).size(), 2);
    QVERIFY(!profile.toText().contains(QStringLiteral("a.example.net")));
}

void Openvpn3ProfileTest::quotingRoundTrips_data()
{
    QTest::addColumn<QString>("argument");

    QTest::newRow("plain") << QStringLiteral("value");
    QTest::newRow("spaces") << QStringLiteral("a value with spaces");
    QTest::newRow("double quote") << QStringLiteral("a \"quoted\" value");
    QTest::newRow("single quote") << QStringLiteral("it's here");
    QTest::newRow("backslash") << QStringLiteral("C:\\path\\to");
    QTest::newRow("hash") << QStringLiteral("not#a#comment");
    QTest::newRow("empty") << QString();
}

void Openvpn3ProfileTest::quotingRoundTrips()
{
    QFETCH(QString, argument);

    const QString line = QStringLiteral("setenv x ") + Openvpn3Profile::quoteArgument(argument);
    QCOMPARE(Openvpn3Profile::splitArguments(line).value(2), argument);

    // And the same through a whole document.
    Openvpn3Profile profile;
    profile.append(Openvpn3Profile::directive(QStringLiteral("setenv"), {QStringLiteral("x"), argument}));
    QCOMPARE(Openvpn3Profile::fromText(profile.toText()).arguments(QStringLiteral("setenv")).value(1), argument);
}

void Openvpn3ProfileTest::unterminatedBlockIsKeptVerbatim()
{
    const auto text = QStringLiteral("client\n<ca>\nPEM-LINE\nanother\n");
    const Openvpn3Profile profile = Openvpn3Profile::fromText(text);

    QCOMPARE(profile.count(), 2);
    QCOMPARE(profile.toText(), text);
}

void Openvpn3ProfileTest::commentsAreNeverMatchedAsDirectives()
{
    const Openvpn3Profile profile = Openvpn3Profile::fromText(QStringLiteral("client\n#remote commented.example.net\n"));

    QVERIFY(!profile.contains(QStringLiteral("remote")));
    QVERIFY(!profile.contains(QStringLiteral("#remote")));
}

void Openvpn3ProfileTest::spotsKeyMaterialThatCouldNeedAPassphrase_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");

    QTest::newRow("nothing") << u"client\nremote a.example.org\n"_s << false;
    QTest::newRow("certificate only") << u"client\n<ca>\nPEM\n</ca>\n"_s << false;
    QTest::newRow("plain key") << u"client\n<key>\n-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----\n</key>\n"_s << false;
    QTest::newRow("pkcs8 encrypted key")
        << u"client\n<key>\n-----BEGIN ENCRYPTED PRIVATE KEY-----\nAAAA\n-----END ENCRYPTED PRIVATE KEY-----\n</key>\n"_s << true;
    QTest::newRow("traditional encrypted key") << u"client\n<key>\nProc-Type: 4,ENCRYPTED\nDEK-Info: AES-256-CBC,00\nAAAA\n</key>\n"_s << true;
    QTest::newRow("pkcs12 block") << u"client\n<pkcs12>\nAAAA\n</pkcs12>\n"_s << true;
    // A file reference cannot be inspected, so it has to be assumed.
    QTest::newRow("key file reference") << u"client\nkey /etc/openvpn/client.key\n"_s << true;
    QTest::newRow("pkcs12 file reference") << u"client\npkcs12 /etc/openvpn/client.p12\n"_s << true;
}

void Openvpn3ProfileTest::spotsKeyMaterialThatCouldNeedAPassphrase()
{
    QFETCH(QString, text);
    QFETCH(bool, expected);

    QCOMPARE(Openvpn3Profile::fromText(text).mayNeedPrivateKeyPassphrase(), expected);
}

// -- what openvpn3 sees, as opposed to what the top level of the document says --

void Openvpn3ProfileTest::optionsAreFoundInsideConnectionBlocks()
{
    const Openvpn3Profile profile = Openvpn3Profile::fromText(
        u"client\n<connection>\nremote fallback.example.net 1194 udp\nauth-user-pass\n</connection>\n"_s);

    // The top level is what the editor edits, and it has no auth-user-pass.
    QVERIFY(!profile.contains(u"auth-user-pass"_s));
    QVERIFY(profile.indexesOf(u"remote"_s).isEmpty());

    // What the connection actually needs is another question, and the one
    // that decides whether a username and password are stored for it.
    QVERIFY(profile.containsOption(u"auth-user-pass"_s));
    QCOMPARE(profile.optionsNamed(u"remote"_s).size(), 1);
    QCOMPARE(profile.optionsNamed(u"remote"_s).constFirst().value(), u"fallback.example.net"_s);
}

void Openvpn3ProfileTest::opaqueBlocksAreNeverDescendedInto()
{
    // A certificate whose base64 happens to begin with a word the editor
    // knows is payload, not a directive.
    const Openvpn3Profile profile = Openvpn3Profile::fromText(
        u"client\n<connection>\n<ca>\nremote this.is.payload\nauth-user-pass\n</ca>\n</connection>\n"_s);

    QVERIFY(!profile.containsOption(u"remote"_s));
    QVERIFY(!profile.containsOption(u"auth-user-pass"_s));
    // The block itself is one entry, carried across as it stands.
    QCOMPARE(profile.optionsNamed(u"ca"_s).size(), 1);
    QVERIFY(profile.optionsNamed(u"ca"_s).constFirst().isBlock());
    QVERIFY(profile.optionsNamed(u"ca"_s).constFirst().body.contains(u"this.is.payload"_s));
}

void Openvpn3ProfileTest::nestedInlineCredentialsAreFoundAsABlock()
{
    const Openvpn3Profile profile =
        Openvpn3Profile::fromText(u"client\nremote a.example.net\n<connection>\n<auth-user-pass>\nalice\npw\n</auth-user-pass>\n</connection>\n"_s);

    const QList<Openvpn3Entry> found = profile.optionsNamed(u"auth-user-pass"_s);
    QCOMPARE(found.size(), 1);
    QVERIFY(found.constFirst().isBlock());
    QVERIFY(profile.needsNormalization());
}

void Openvpn3ProfileTest::remoteHostsComeFromEveryScope_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("hosts");

    QTest::newRow("top level") << u"client\nremote a.example.net 1194\n"_s << QStringList{u"a.example.net"_s};
    QTest::newRow("inside a connection block")
        << u"client\n<connection>\nremote b.example.net 1194\n</connection>\n"_s << QStringList{u"b.example.net"_s};
    QTest::newRow("both") << u"client\nremote a.example.net\n<connection>\nremote b.example.net\n</connection>\n"_s
                          << QStringList{u"a.example.net"_s, u"b.example.net"_s};
    // A row the user added and never filled in is a remote with no host; it
    // is listed, because whether that is good enough is not this class's call.
    QTest::newRow("blank row") << u"client\nremote\n"_s << QStringList{QString()};
    QTest::newRow("none") << u"client\ndev tun\n"_s << QStringList{};
}

void Openvpn3ProfileTest::remoteHostsComeFromEveryScope()
{
    QFETCH(QString, text);
    QFETCH(QStringList, hosts);

    QCOMPARE(Openvpn3Profile::fromText(text).remoteHosts(), hosts);
}

void Openvpn3ProfileTest::normalizationIsNeededOnlyForFilesAndCredentials_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");

    QTest::newRow("self-contained") << u"client\nremote a.example.net\n<ca>\nPEM\n</ca>\n"_s << false;
    QTest::newRow("unknown directives are not our business") << u"client\nremote a.example.net\nsomething /a/path\n"_s << false;
    QTest::newRow("ca file") << u"client\nca /etc/openvpn/ca.crt\n"_s << true;
    QTest::newRow("key file") << u"client\nkey client.key\n"_s << true;
    QTest::newRow("pkcs12 file") << u"client\npkcs12 client.p12\n"_s << true;
    QTest::newRow("tls-crypt file") << u"client\ntls-crypt tc.key\n"_s << true;
    QTest::newRow("credentials file") << u"client\nauth-user-pass creds.txt\n"_s << true;
    QTest::newRow("inline credentials") << u"client\n<auth-user-pass>\nalice\npw\n</auth-user-pass>\n"_s << true;
    QTest::newRow("bare auth-user-pass") << u"client\nauth-user-pass\n"_s << false;
    QTest::newRow("explicit inline marker") << u"client\nca [inline]\n<ca>\nPEM\n</ca>\n"_s << false;
    QTest::newRow("a crl directory is not inlined") << u"client\ncrl-verify /etc/openvpn/crls dir\n"_s << false;
    QTest::newRow("a file named inside a connection block") << u"client\n<connection>\nca ca.crt\n</connection>\n"_s << true;
}

void Openvpn3ProfileTest::normalizationIsNeededOnlyForFilesAndCredentials()
{
    QFETCH(QString, text);
    QFETCH(bool, expected);

    QCOMPARE(Openvpn3Profile::fromText(text).needsNormalization(), expected);
}

void Openvpn3ProfileTest::entriesKeepTheirIdentityWhileTheDocumentChanges()
{
    // Rows of the server table are remembered by entry, not by position, so
    // an entry has to stay the same entry while the document around it moves.
    Openvpn3Profile profile = Openvpn3Profile::fromText(u"client\nremote a.example.net\ndev tun\nremote b.example.net\n"_s);
    const QList<int> remotes = profile.indexesOf(u"remote"_s);
    const quint64 first = profile.at(remotes.at(0)).id();
    const quint64 second = profile.at(remotes.at(1)).id();
    QVERIFY(first != second);

    profile.setArguments(remotes.at(0), {u"c.example.net"_s});
    QCOMPARE(profile.at(remotes.at(0)).id(), first);

    profile.insert(0, Openvpn3Profile::comment(u"new"_s));
    QCOMPARE(profile.indexOfId(first), 2);
    QCOMPARE(profile.indexOfId(second), 4);

    profile.removeAt(profile.indexOfId(first));
    QCOMPARE(profile.indexOfId(first), -1);
    QCOMPARE(profile.indexOfId(second), 3);
}

void Openvpn3ProfileTest::parsedEntriesHaveDistinctIdentities()
{
    const Openvpn3Profile profile = Openvpn3Profile::fromText(kRich);

    QSet<quint64> ids;
    for (const Openvpn3Entry &entry : profile.entries()) {
        QVERIFY(entry.id() != 0);
        ids.insert(entry.id());
    }
    QCOMPARE(ids.size(), profile.count());

    // Two documents never share one, either: a reload is a different document.
    const Openvpn3Profile other = Openvpn3Profile::fromText(kRich);
    for (const Openvpn3Entry &entry : other.entries()) {
        QVERIFY(!ids.contains(entry.id()));
    }
}

void Openvpn3ProfileTest::crlfIsVisibleAsTheDocumentsConvention_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");

    QTest::newRow("unix") << u"client\nremote a.example.net\n"_s << false;
    QTest::newRow("dos") << u"client\r\nremote a.example.net\r\n"_s << true;
    QTest::newRow("dos with a block") << u"client\r\n<ca>\r\nPEM\r\n</ca>\r\n"_s << true;
    // One stray terminator does not make it a DOS file.
    QTest::newRow("mostly unix") << u"client\nremote a.example.net\r\ndev tun\n"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void Openvpn3ProfileTest::crlfIsVisibleAsTheDocumentsConvention()
{
    QFETCH(QString, text);
    QFETCH(bool, expected);

    QCOMPARE(Openvpn3Profile::fromText(text).usesCrlf(), expected);
}

QTEST_GUILESS_MAIN(Openvpn3ProfileTest)

#include "openvpn3profiletest.moc"
