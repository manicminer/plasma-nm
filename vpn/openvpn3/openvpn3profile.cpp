/*
    SPDX-FileCopyrightText: 2026 Tom Bamford <tom@bamford.io>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "openvpn3profile.h"

namespace
{

/** Splits @p text into lines that still carry their terminator, so that
 * joining them again reproduces the input exactly -- CRLF, a missing final
 * newline and all. */
QStringList linesWithTerminators(const QString &text)
{
    QStringList lines;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'\n') {
            lines.append(text.mid(start, i - start + 1));
            start = i + 1;
        }
    }
    if (start < text.size()) {
        lines.append(text.mid(start));
    }
    return lines;
}

/** The whitespace a directive line is made of words with.
 *
 * Only ASCII whitespace separates anything: that is all openvpn3's own lexer
 * breaks on, and all @c g_ascii_isspace() -- what the backend's ovpn-import.c
 * uses throughout -- counts. @c QChar::isSpace() also takes the Unicode
 * separators, @c U+00A0 and friends, which are literal bytes of the value
 * they sit in; treating one as a separator would delete it from a value or
 * let a @c # glued behind it pass for the start of a comment. */
bool isAsciiSpace(QChar c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\v' || c == u'\f' || c == u'\r';
}

bool isOpeningTag(const QString &stripped)
{
    return stripped.size() > 2 && stripped.startsWith(u'<') && stripped.endsWith(u'>') && !stripped.startsWith(QLatin1String("</"));
}

/** @c {</tag>} on a line of its own, the only thing openvpn3 closes a block
 * with. */
bool isClosingTag(const QString &stripped)
{
    return stripped.size() > 3 && stripped.startsWith(QLatin1String("</")) && stripped.endsWith(u'>');
}

/** Blocks that hold options rather than an opaque payload. The backend's
 * option_scopes, which is the list openvpn3 itself goes by. */
bool isOptionScope(const QString &name)
{
    return name == QLatin1String("connection");
}

/**
 * Where the comment on @p line starts, or -1 if it has none.
 *
 * The two OpenVPN lexers do not agree on this, so what counts here is what
 * both of them read as a comment:
 *
 *  - OpenVPN 2 (@c parse_line(), src/openvpn/options_parse.c) only looks for
 *    a @c # or @c ; at the start of a parameter, and not inside quotes, so
 *    @c a#b is the literal value a#b.
 *  - openvpn3 (@c OptionList::LexComment, openvpn/common/options.hpp) looks
 *    anywhere outside quotes, but a backslash escapes the character: @c a#b
 *    is the value @c a, and @c \# is a literal @c #.
 *
 * Where they disagree -- a character glued to the middle of a word, an
 * escaped one -- the line is left alone. openvpn3 is what reads the stored
 * profile and it already ignores whatever it takes for a comment, so keeping
 * those characters cannot change what an option means, while cutting them
 * off could. Keep this in step with comment_start() in the backend's
 * ovpn-import.c.
 *
 * "Outside quotes" has to satisfy both of them too, and they do not even
 * agree on where a quote ends: a backslash inside single quotes is a literal
 * character for OpenVPN 2 (@c parse_line() skips its escape handling in
 * @c STATE_READING_SQUOTED_PARM), so the apostrophe after it closes the
 * quote, while openvpn3 lets it escape that apostrophe and stays inside. Both
 * quote states are therefore tracked, and a character counts as outside
 * quotes only when neither lexer has it in one. That is what keeps
 * @c {setenv a 'x\' # literal'} whole -- the value openvpn3 reads.
 */
int commentStart(const QString &line)
{
    bool ov2Single = false;
    bool ov2Double = false;
    bool ov2Escaped = false;
    bool ov3Single = false;
    bool ov3Double = false;
    bool ov3Escaped = false;
    bool wordStart = true;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        const bool quoted = ov2Single || ov2Double || ov3Single || ov3Double;
        const bool escaped = ov2Escaped || ov3Escaped;

        if (wordStart && !quoted && !escaped && (c == u'#' || c == u';')) {
            return i;
        }

        // OpenVPN 2: a backslash is not an escape inside single quotes.
        if (ov2Escaped) {
            ov2Escaped = false;
        } else if (c == u'\\' && !ov2Single) {
            ov2Escaped = true;
        } else if (c == u'"' && !ov2Single) {
            ov2Double = !ov2Double;
        } else if (c == u'\'' && !ov2Double) {
            ov2Single = !ov2Single;
        }

        // openvpn3: a backslash escapes everywhere, quotes included.
        if (ov3Escaped) {
            ov3Escaped = false;
        } else if (c == u'\\') {
            ov3Escaped = true;
        } else if (c == u'"' && !ov3Single) {
            ov3Double = !ov3Double;
        } else if (c == u'\'' && !ov3Double) {
            ov3Single = !ov3Single;
        }

        // Only unquoted, unescaped whitespace starts the next word.
        wordStart = !quoted && !escaped && isAsciiSpace(c);
    }
    return -1;
}

/** @p line without the comment starting at @p cut, its terminator kept. An
 * empty string when nothing but whitespace came before the comment, which is
 * how a line that is only a comment is recognised.
 *
 * What goes with the comment is the unescaped whitespace that separated it,
 * and no more: backslash-escaped whitespace is part of the value in front of
 * it for both lexers, so @c {setenv a value\  # c} keeps the value
 * @c {value\ }. An even number of backslashes before the whitespace are
 * escaped backslashes and leave it a separator again. Unicode whitespace is
 * not a separator at all and stays where it is. */
QString withoutComment(const QString &line, int cut)
{
    QString terminator;
    if (line.endsWith(QLatin1String("\r\n"))) {
        terminator = QStringLiteral("\r\n");
    } else if (line.endsWith(u'\n')) {
        terminator = QStringLiteral("\n");
    }

    QString kept = line.left(cut);
    while (!kept.isEmpty() && isAsciiSpace(kept.back())) {
        int backslashes = 0;
        while (backslashes < kept.size() - 1 && kept.at(kept.size() - 2 - backslashes) == u'\\') {
            ++backslashes;
        }
        if (backslashes % 2 != 0) {
            break; // escaped: part of the value, not a separator
        }
        kept.chop(1);
    }
    return kept.isEmpty() ? QString() : kept + terminator;
}

/** A @c <connection> body with the comments among its directives dropped.
 * Any other block's body is opaque payload -- a certificate, a key, a
 * credential -- and comes back exactly as it was given. */
QString bodyWithoutComments(const QString &name, const QString &body)
{
    return isOptionScope(name) ? Openvpn3Profile::fromText(body).toText() : body;
}

/** Directives naming a file the backend inlines when it normalises. Its
 * file_directives, plus pkcs12, which it inlines as base64. */
bool isFileDirective(const QString &name)
{
    static const QStringList names{
        QStringLiteral("ca"),
        QStringLiteral("cert"),
        QStringLiteral("key"),
        QStringLiteral("extra-certs"),
        QStringLiteral("tls-auth"),
        QStringLiteral("tls-crypt"),
        QStringLiteral("tls-crypt-v2"),
        QStringLiteral("crl-verify"),
        QStringLiteral("pkcs12"),
    };
    return names.contains(name);
}

}

quint64 Openvpn3Entry::nextId()
{
    static quint64 counter = 0;
    return ++counter;
}

QStringList Openvpn3Profile::splitArguments(const QString &line)
{
    QStringList words;
    const int length = line.size();
    int i = 0;

    while (i < length) {
        while (i < length && isAsciiSpace(line.at(i))) {
            ++i;
        }
        if (i >= length || line.at(i) == u'#' || line.at(i) == u';') {
            break;
        }
        QString word;
        while (i < length && !isAsciiSpace(line.at(i))) {
            const QChar c = line.at(i);
            if (c == u'"' || c == u'\'') {
                const QChar quote = c;
                ++i;
                while (i < length && line.at(i) != quote) {
                    // Only double quotes take backslash escapes, as in OpenVPN.
                    if (quote == u'"' && line.at(i) == u'\\' && i + 1 < length) {
                        ++i;
                    }
                    word += line.at(i);
                    ++i;
                }
                if (i < length) {
                    ++i; // the closing quote
                }
            } else {
                word += c;
                ++i;
            }
        }
        words.append(word);
    }
    return words;
}

QString Openvpn3Profile::quoteArgument(const QString &argument)
{
    bool needsQuotes = argument.isEmpty();
    for (const QChar c : argument) {
        // Quoting asks what could be lost, not what separates: Unicode
        // whitespace separates nothing, but a value that ends in it comes
        // back short of it, because reading a line strips it off along with
        // the terminator. So @c QChar::isSpace() here, where @c isAsciiSpace()
        // reads the line.
        if (c.isSpace() || c == u'"' || c == u'\'' || c == u'\\' || c == u'#' || c == u';') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return argument;
    }

    QString quoted;
    quoted.reserve(argument.size() + 2);
    quoted += u'"';
    for (const QChar c : argument) {
        if (c == u'"' || c == u'\\') {
            quoted += u'\\';
        }
        quoted += c;
    }
    quoted += u'"';
    return quoted;
}

Openvpn3Profile Openvpn3Profile::fromText(const QString &text)
{
    Openvpn3Profile profile;
    const QStringList lines = linesWithTerminators(text);

    for (int i = 0; i < lines.size(); ++i) {
        const QString &raw = lines.at(i);
        // A comment is not an entry and never reaches one: a line that is
        // only a comment is skipped, and one on a directive is cut off the
        // source the entry keeps.  The lines of an opaque <tag> payload are
        // content rather than directives and are not read as lines at all --
        // the block below swallows them whole.
        const int comment = commentStart(raw);
        QString line = raw;
        if (comment >= 0) {
            const QString cut = withoutComment(raw, comment);
            // Cutting a comment must not turn a line into a closing tag the
            // raw line was not one: openvpn3 matches every closing tag
            // against the raw line, so {</connection> # x} is no boundary for
            // it even when a real closer follows below. Cutting here would
            // invent one there and move every directive up to the real closer
            // out of the scope. Such a line is kept exactly as it stands
            // instead -- the comment stays, and openvpn3 ignores it where it
            // is. Keep this in step with the backend's ovpn-import.c.
            if (!isClosingTag(cut.trimmed())) {
                line = cut;
            }
        }
        if (line.isEmpty()) {
            continue;
        }
        const QString stripped = line.trimmed();
        Openvpn3Entry entry;
        entry.m_verbatim = true;
        entry.m_source = line;

        if (stripped.isEmpty()) {
            entry.kind = Openvpn3Entry::Blank;
        } else if (isOpeningTag(stripped)) {
            entry.kind = Openvpn3Entry::Block;
            entry.name = stripped.mid(1, stripped.size() - 2);
            const QString closing = QLatin1String("</") + entry.name + QLatin1Char('>');
            QString body;
            int end = i + 1;
            for (; end < lines.size(); ++end) {
                // openvpn3 matches a closing tag against the raw line, so a
                // comment after one does not close the block for it either.
                if (lines.at(end).trimmed() == closing) {
                    break;
                }
                body += lines.at(end);
            }
            if (end < lines.size()) {
                // A scope's lines are directives, so the comments among them
                // are comments. Parsing the body is how they go, and it
                // leaves a block nested in here opaque, payload and all.
                body = bodyWithoutComments(entry.name, body);
                entry.body = body;
                entry.m_source = line + body + lines.at(end);
                i = end;
            } else {
                // Unterminated: everything that is left belongs to this block
                // and is kept exactly as it is rather than repaired -- a scope
                // whose closing tag carries a comment is not closed for
                // openvpn3 either, and repairing it here would invent one.
                entry.body = body;
                entry.m_source = line + body;
                i = lines.size() - 1;
            }
        } else {
            const QStringList words = splitArguments(stripped);
            entry.name = words.value(0);
            entry.arguments = words.mid(1);
        }
        profile.m_entries.append(entry);
    }
    return profile;
}

QString Openvpn3Profile::render(const Openvpn3Entry &entry)
{
    switch (entry.kind) {
    case Openvpn3Entry::Blank:
        return QStringLiteral("\n");
    case Openvpn3Entry::Block: {
        QString text = u'<' + entry.name + QLatin1String(">\n");
        text += entry.body;
        if (!entry.body.isEmpty() && !entry.body.endsWith(u'\n')) {
            text += u'\n';
        }
        text += QLatin1String("</") + entry.name + QLatin1String(">\n");
        return text;
    }
    case Openvpn3Entry::Directive:
        break;
    }

    QString text = entry.name;
    for (const QString &argument : entry.arguments) {
        text += u' ' + quoteArgument(argument);
    }
    text += u'\n';
    return text;
}

QString Openvpn3Profile::toText() const
{
    // A formerly final, unterminated entry may now have a successor. Keep
    // its source intact, but separate entries using the original convention.
    QString newline = QStringLiteral("\n");
    for (const Openvpn3Entry &entry : m_entries) {
        const int end = entry.m_source.indexOf(u'\n');
        if (end >= 0) {
            if (end > 0 && entry.m_source.at(end - 1) == u'\r') {
                newline = QStringLiteral("\r\n");
            }
            break;
        }
    }
    QString text;
    for (const Openvpn3Entry &entry : m_entries) {
        if (!text.isEmpty() && !text.endsWith(u'\n')) {
            text += newline;
        }
        text += entry.m_verbatim ? entry.m_source : render(entry);
    }
    return text;
}

QList<int> Openvpn3Profile::indexesOf(const QString &name) const
{
    QList<int> indexes;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).name == name) {
            indexes.append(i);
        }
    }
    return indexes;
}

int Openvpn3Profile::indexOf(const QString &name) const
{
    const QList<int> indexes = indexesOf(name);
    return indexes.isEmpty() ? -1 : indexes.first();
}

int Openvpn3Profile::indexOfId(quint64 id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id() == id) {
            return i;
        }
    }
    return -1;
}

QList<Openvpn3Entry> Openvpn3Profile::optionEntries() const
{
    QList<Openvpn3Entry> options;
    for (const Openvpn3Entry &entry : m_entries) {
        if (entry.isBlock() && isOptionScope(entry.name)) {
            // Its lines are options in their own right; the parser reads them
            // the same way it read the document, so a block nested in here
            // stays one entry and its payload stays inside it.
            options += fromText(entry.body).optionEntries();
            continue;
        }
        options.append(entry);
    }
    return options;
}

QList<Openvpn3Entry> Openvpn3Profile::optionsNamed(const QString &name) const
{
    QList<Openvpn3Entry> found;
    const QList<Openvpn3Entry> options = optionEntries();
    for (const Openvpn3Entry &entry : options) {
        if (entry.name == name) {
            found.append(entry);
        }
    }
    return found;
}

QStringList Openvpn3Profile::remoteHosts() const
{
    QStringList hosts;
    const QList<Openvpn3Entry> remotes = optionsNamed(QStringLiteral("remote"));
    for (const Openvpn3Entry &entry : remotes) {
        hosts.append(entry.value());
    }
    return hosts;
}

bool Openvpn3Profile::needsNormalization() const
{
    const QList<Openvpn3Entry> options = optionEntries();
    for (const Openvpn3Entry &entry : options) {
        if (entry.isBlock()) {
            // Credentials typed into the editor: the username and password
            // belong with the connection, not in the profile.
            if (entry.name == QLatin1String("auth-user-pass")) {
                return true;
            }
            continue;
        }
        if (!entry.isDirective() || entry.arguments.isEmpty()) {
            continue;
        }
        if (entry.name == QLatin1String("auth-user-pass")) {
            return true; // a credentials file
        }
        if (!isFileDirective(entry.name) || entry.value() == QLatin1String("[inline]")) {
            continue;
        }
        if (entry.name == QLatin1String("crl-verify") && entry.arguments.value(1) == QLatin1String("dir")) {
            continue; // a directory openvpn reads itself, not a file to inline
        }
        return true;
    }
    return false;
}

bool Openvpn3Profile::usesCrlf() const
{
    int crlf = 0;
    int lf = 0;
    for (const Openvpn3Entry &entry : m_entries) {
        const QString text = entry.m_verbatim ? entry.m_source : render(entry);
        for (int i = text.indexOf(u'\n'); i >= 0; i = text.indexOf(u'\n', i + 1)) {
            (i > 0 && text.at(i - 1) == u'\r') ? ++crlf : ++lf;
        }
    }
    return crlf > lf;
}

QString Openvpn3Profile::value(const QString &name) const
{
    return arguments(name).value(0);
}

QStringList Openvpn3Profile::arguments(const QString &name) const
{
    const int index = indexOf(name);
    return index < 0 ? QStringList() : m_entries.at(index).arguments;
}

QString Openvpn3Profile::blockBody(const QString &name) const
{
    for (const Openvpn3Entry &entry : m_entries) {
        if (entry.isBlock() && entry.name == name) {
            return entry.body;
        }
    }
    return QString();
}

QString Openvpn3Profile::sourceAt(int index) const
{
    const Openvpn3Entry &entry = m_entries.at(index);
    return entry.m_verbatim ? entry.m_source : render(entry);
}

bool Openvpn3Profile::mayNeedPrivateKeyPassphrase() const
{
    for (const Openvpn3Entry &entry : optionEntries()) {
        if (entry.name != QLatin1String("key") && entry.name != QLatin1String("pkcs12")) {
            continue;
        }
        if (!entry.isBlock()) {
            // A file reference: nothing here can look inside it.
            return !entry.arguments.isEmpty();
        }
        if (entry.name == QLatin1String("pkcs12")) {
            // Whether a bundle is encrypted only shows on opening it, which
            // is the service's job, not the editor's.
            return true;
        }
        // The two ways OpenSSL marks an encrypted private key.
        if (entry.body.contains(QLatin1String("ENCRYPTED PRIVATE KEY")) || entry.body.contains(QLatin1String("Proc-Type: 4,ENCRYPTED"))) {
            return true;
        }
    }
    return false;
}

void Openvpn3Profile::setArguments(int index, const QStringList &arguments)
{
    Openvpn3Entry &entry = m_entries[index];
    if (entry.arguments == arguments) {
        return; // unchanged: keep the source, and with it the original quoting
    }
    entry.arguments = arguments;
    entry.m_verbatim = false;
}

void Openvpn3Profile::setBody(int index, const QString &body)
{
    Openvpn3Entry &entry = m_entries[index];
    // A <connection> body is the scope's directives however it arrives, so a
    // comment put in by hand goes the same way one read from a file does.
    // Keeping it would save an edit that vanishes on the next load.
    const QString kept = bodyWithoutComments(entry.name, body);
    if (entry.body == kept) {
        return;
    }
    entry.body = kept;
    entry.m_verbatim = false;
}

void Openvpn3Profile::replace(int index, const Openvpn3Entry &entry)
{
    m_entries[index] = entry;
}

void Openvpn3Profile::removeAt(int index)
{
    m_entries.removeAt(index);
}

void Openvpn3Profile::removeAll(const QString &name)
{
    const QList<int> indexes = indexesOf(name);
    for (auto it = indexes.crbegin(); it != indexes.crend(); ++it) {
        m_entries.removeAt(*it);
    }
}

int Openvpn3Profile::append(const Openvpn3Entry &entry)
{
    m_entries.append(entry);
    return m_entries.size() - 1;
}

void Openvpn3Profile::insert(int index, const Openvpn3Entry &entry)
{
    m_entries.insert(index, entry);
}

void Openvpn3Profile::move(int from, int to)
{
    m_entries.move(from, to);
}

void Openvpn3Profile::setDirective(const QString &name, const QStringList &arguments)
{
    const int index = indexOf(name);
    if (index < 0) {
        append(directive(name, arguments));
    } else {
        setArguments(index, arguments);
    }
}

void Openvpn3Profile::setPresent(const QString &name, bool present)
{
    const int index = indexOf(name);
    if (present && index < 0) {
        append(directive(name));
    } else if (!present && index >= 0) {
        removeAll(name);
    }
}

void Openvpn3Profile::setBlock(const QString &name, const QString &body)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).isBlock() && m_entries.at(i).name == name) {
            setBody(i, body);
            return;
        }
    }
    append(block(name, body));
}

Openvpn3Entry Openvpn3Profile::directive(const QString &name, const QStringList &arguments)
{
    Openvpn3Entry entry;
    entry.kind = Openvpn3Entry::Directive;
    entry.name = name;
    entry.arguments = arguments;
    return entry;
}

Openvpn3Entry Openvpn3Profile::block(const QString &name, const QString &body)
{
    Openvpn3Entry entry;
    entry.kind = Openvpn3Entry::Block;
    entry.name = name;
    entry.body = bodyWithoutComments(name, body);
    return entry;
}

Openvpn3Entry Openvpn3Profile::blank()
{
    Openvpn3Entry entry;
    entry.kind = Openvpn3Entry::Blank;
    return entry;
}
