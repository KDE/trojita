/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License or (at your option) version 3 or any later version
   accepted by the membership of KDE e.V. (or its successor approved
   by the membership of KDE e.V.), which shall act as a proxy
   defined in Section 14 of version 3 of the license.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <typeinfo>

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QTextDocument>
#include <QUrl>
#include <QUrlQuery>
#include <QTextCodec>
#include "MailAddress.h"
#include "../Model/MailboxTree.h"
#include "../Encoders.h"
#include "../Parser/Rfc5322HeaderParser.h"
#include "UiUtils/Formatting.h"

namespace Imap
{
namespace Message
{

bool MailAddress::fromPrettyString(MailAddress &into, const QString &address)
{
    int offset = 0;

    if (!parseOneAddress(into, address, offset))
        return false;

    if (offset < address.size())
        return false;

    return true;
}

/* Regexpes to match an address typed into the input field. */
static QRegularExpression mailishRx1(QLatin1String("^\\s*([\\w!#$%&'*+-/=?^_`{|}~]+)\\s*\\@"
                                                   "\\s*([\\w_.-]+|(?:\\[[^][\\\\\\\"\\s]+\\]))\\s*$"));
static QRegularExpression mailishRx2(QLatin1String("\\s*<([\\w!#$%&'*+-/=?^_`{|}~]+)\\s*\\@"
                                                   "\\s*([\\w_.-]+|(?:\\[[^][\\\\\\\"\\s]+\\]))>\\s*$"));

/*
   This is of course far from complete, but at least catches "Real
   Name" <foo@bar>.  It needs to recognize the things people actually
   type, and it should also recognize anything that's a valid
   rfc2822 address.
*/
bool MailAddress::parseOneAddress(Imap::Message::MailAddress &into, const QString &address, int &startOffset)
{
    for (QRegularExpression mailishRx : {mailishRx2, mailishRx1}) {
        QRegularExpressionMatch match = mailishRx.match(address, startOffset);
        int offset = match.capturedStart();
        if (match.hasMatch()) {
            QString before = address.mid(startOffset, offset - startOffset);
            into = MailAddress(before.simplified(), QString(), match.captured(1), match.captured(2));

            offset += match.capturedLength();

            startOffset = offset;
            return true;
        }
    }
    return false;
}

MailAddress::MailAddress(const QVariantList &input, const QByteArray &line, const int start)
{
    // FIXME: all offsets are wrong here
    if (input.size() != 4)
        throw ParseError("MailAddress: not four items", line, start);

    if (input[0].type() != QVariant::ByteArray)
        throw UnexpectedHere("MailAddress: item#1 not a QByteArray", line, start);
    if (input[1].type() != QVariant::ByteArray)
        throw UnexpectedHere("MailAddress: item#2 not a QByteArray", line, start);
    if (input[2].type() != QVariant::ByteArray)
        throw UnexpectedHere("MailAddress: item#3 not a QByteArray", line, start);
    if (input[3].type() != QVariant::ByteArray)
        throw UnexpectedHere("MailAddress: item#4 not a QByteArray", line, start);

    name = Imap::decodeRFC2047String(input[0].toByteArray());
    adl = Imap::decodeRFC2047String(input[1].toByteArray());
    mailbox = Imap::decodeRFC2047String(input[2].toByteArray());
    host = Imap::decodeRFC2047String(input[3].toByteArray());
}

QUrl MailAddress::asUrl() const
{
    QUrl url;
    url.setScheme(QStringLiteral("mailto"));
    url.setPath(QStringLiteral("%1@%2").arg(mailbox, host));
    if (!name.isEmpty()) {
        QUrlQuery q(url);
        q.addQueryItem(QStringLiteral("X-Trojita-DisplayName"), name);
        url.setQuery(q);
    }
    return url;
}

QString MailAddress::prettyName(FormattingMode mode) const
{
    bool hasNiceName = !name.isEmpty();

    if (!hasNiceName && mode == FORMAT_JUST_NAME)
        mode = FORMAT_READABLE;

    if (mode == FORMAT_JUST_NAME) {
        return name;
    } else {
        QString address = mailbox + QLatin1Char('@') + host;
        QString niceName;
        if (hasNiceName) {
            niceName = name;
        } else {
            niceName = address;
        }
        if (mode == FORMAT_READABLE) {
            if (hasNiceName) {
                return name + QLatin1String(" <") + address + QLatin1Char('>');
            } else {
                return address;
            }
        } else {
            if (mode == FORMAT_SHORT_CLICKABLE)
                UiUtils::elideAddress(niceName);
            return QStringLiteral("<a href=\"%1\">%2</a>").arg(asUrl().toString().toHtmlEscaped(), niceName.toHtmlEscaped());
        }
    }
}

QString MailAddress::prettyList(const QList<MailAddress> &list, FormattingMode mode)
{
    QStringList buf;
    for (QList<MailAddress>::const_iterator it = list.begin(); it != list.end(); ++it)
        buf << it->prettyName(mode);
    return buf.join(QStringLiteral(", "));
}

QString MailAddress::prettyList(const QVariantList &list, FormattingMode mode)
{
    QStringList buf;
    for (QVariantList::const_iterator it = list.begin(); it != list.end(); ++it) {
        Q_ASSERT(it->type() == QVariant::StringList);
        QStringList item = it->toStringList();
        Q_ASSERT(item.size() == 4);
        MailAddress a(item[0], item[1], item[2], item[3]);
        buf << a.prettyName(mode);
    }
    return buf.join(QStringLiteral(", "));
}

static QRegularExpression dotAtomRx(QLatin1String("^[A-Za-z0-9!#$&'*+/=?^_`{}|~-]+(?:\\.[A-Za-z0-9!#$&'*+/=?^_`{}|~-]+)*$"));

/* This returns the address formatted for use in an SMTP MAIL or RCPT command; specifically, it matches the "Mailbox" production of RFC2821. The surrounding angle-brackets are not included. */
QByteArray MailAddress::asSMTPMailbox() const
{
    QByteArray result;

    /* Check whether the local-part contains any characters
       preventing it from being a dot-atom. */
    if (dotAtomRx.match(mailbox).hasMatch()) {
        /* Using .toLatin1() here even though we know it only contains
           ASCII, because QString.toAscii() does not necessarily convert
           to ASCII (despite the name). .toLatin1() always converts to
           Latin-1. */
        result = mailbox.toLatin1();
    } else {
        /* The other syntax allowed for local-parts is a double-quoted string.
           Note that RFC2047 tokens aren't allowed there --- local-parts are
           fundamentally bytestrings, apparently, whose interpretation is
           up to the receiving system. If someone types non-ASCII characters
           into the address field we'll generate non-conforming headers, but
           it's the best we can do. */
        result = Imap::quotedString(mailbox.toUtf8());
    }

    result.append("@");

    QByteArray domainpart;

    if (!(host.startsWith(QLatin1Char('[')) || host.endsWith(QLatin1Char(']')))) {
        /* IDN-encode the hostname part of the address */
        domainpart = QUrl::toAce(host);

        /* TODO: QUrl::toAce() is documented to return an empty result if
           the string isn't a valid hostname --- for example, if it's a
           domain literal containing an IP address. In that case, we'll
           need to encode it ourselves (making sure there are square
           brackets, no forbidden characters, appropriate backslashes, and so on). */
    }

    if (domainpart.isEmpty()) {
        /* Either the domainpart looks like a domain-literal, or toAce() failed. */

        domainpart = host.toUtf8();
        if (domainpart.startsWith('[')) {
            domainpart.remove(0, 1);
        }
        if (domainpart.endsWith(']')) {
            domainpart.remove(domainpart.size()-1, 1);
        }

        result.append(Imap::quotedString(domainpart, Imap::SquareBrackets));
    } else {
        result.append(domainpart);
    }

    return result;
}

QByteArray MailAddress::asMailHeader() const
{
    QByteArray result = Imap::encodeRFC2047Phrase(name);

    if (!result.isEmpty())
        result.append(" ");

    result.append("<");
    result.append(asSMTPMailbox());
    result.append(">");

    return result;
}

/** @short The mail address usable for manipulation by user */
QString MailAddress::asPrettyString() const
{
    return name.isEmpty() ?
                QString::fromUtf8(asSMTPMailbox()) :
                name + QLatin1Char(' ') + QLatin1Char('<') + QString::fromUtf8(asSMTPMailbox()) + QLatin1Char('>');
}

/** @short Is the human-readable part "useful", i.e. does it contain something else besides the e-mail address? */
bool MailAddress::hasUsefulDisplayName() const
{
    return !name.isEmpty() && name.trimmed().toUtf8().toLower() != asSMTPMailbox().toLower();
}

/** @short Convert a QUrl into a MailAddress instance */
bool MailAddress::fromUrl(MailAddress &into, const QUrl &url, const QString &expectedScheme)
{
    if (url.scheme().toLower() != expectedScheme.toLower())
        return false;

    QStringList list = url.path().split(QLatin1Char('@'));
    if (list.size() != 2)
        return false;

    QUrlQuery q(url);
    Imap::Message::MailAddress addr(q.queryItemValue(QStringLiteral("X-Trojita-DisplayName")), QString(),
                                    list[0], list[1]);

    if (!addr.hasUsefulDisplayName())
        addr.name.clear();
    into = addr;
    return true;
}

/** @short Helper to construct this from a pair of (human readable name, e-mail address)

This is mainly useful to prevent reimplementing the @-based joining all the time.
*/
MailAddress MailAddress::fromNameAndMail(const QString &name, const QString &email)
{
    auto components = email.split(QLatin1Char('@'));
    if (components.size() == 2) {
        return MailAddress(name, QString(), components[0], components[1]);
    } else {
        // garbage in, garbage out
        return MailAddress(name, QString(), email, QString());
    }
}

QTextStream &operator<<(QTextStream &stream, const MailAddress &address)
{
    stream << '"' << address.name << "\" <";
    if (!address.host.isNull())
        stream << address.mailbox << '@' << address.host;
    else
        stream << address.mailbox;
    stream << '>';
    return stream;
}

bool operator==(const MailAddress &a, const MailAddress &b)
{
    return a.name == b.name && a.adl == b.adl && a.mailbox == b.mailbox && a.host == b.host;
}

bool MailAddressesEqualByMail(const MailAddress &a, const MailAddress &b)
{
    // FIXME: fancy stuff like the IDN?
    return a.mailbox.toLower() == b.mailbox.toLower() && a.host.toLower() == b.host.toLower();
}

bool MailAddressesEqualByDomain(const MailAddress &a, const MailAddress &b)
{
    // FIXME: fancy stuff like the IDN?
    return a.host.toLower() == b.host.toLower();
}


bool MailAddressesEqualByDomainSuffix(const MailAddress &a, const MailAddress &b)
{
    // FIXME: fancy stuff like the IDN?
    auto aHost = a.host.toLower();
    auto bHost = b.host.toLower();
    return aHost == bHost || aHost.endsWith(QLatin1Char('.') + bHost);
}

}
}

QDataStream &operator>>(QDataStream &stream, Imap::Message::MailAddress &a)
{
    return stream >> a.adl >> a.host >> a.mailbox >> a.name;
}

QDataStream &operator<<(QDataStream &stream, const Imap::Message::MailAddress &a)
{
    return stream << a.adl << a.host << a.mailbox << a.name;
}
