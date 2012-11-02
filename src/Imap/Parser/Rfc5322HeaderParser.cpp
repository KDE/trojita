/* Copyright (C) 2006 - 2012 Jan Kundrát <jkt@flaska.net>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or the version 3 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include <QDebug>
#include "Imap/Parser/Rfc5322HeaderParser.h"

#define DBG(X) do {qDebug() << X << "(current char:" << *p << ")";} while(false);
//#define RAGEL_DEBUG

namespace Imap {
namespace Parser {

%%{
    machine rfc5322;

    action push_current_char {
#ifdef RAGEL_DEBUG
        qDebug() << "push_current_char " << *p;
#endif
        str.append(*p);
    }

    action clear_str {
#ifdef RAGEL_DEBUG
        qDebug() << "clear_str";
#endif
        str.clear();
    }

    action push_current_backslashed {
        switch (*p) {
            case 'n':
                str += "\n";
                break;
            case 'r':
                str += "\r";
                break;
            case '0':
                str += "\0";
                break;
            case '\\':
                str += "\\";
                break;
            default:
                str += '\\' + *p;
        }
#ifdef RAGEL_DEBUG
        qDebug() << "push_current_backslashed \\" << *p;
#endif
    }

    action push_string_list {
#ifdef RAGEL_DEBUG
        qDebug() << "push_string_list " << str.data();
#endif
        list.append(str);
        str.clear();
    }

    action clear_list {
#ifdef RAGEL_DEBUG
        qDebug() << "clear_list";
#endif
        list.clear();
        str.clear();
    }

    action got_message_id_header {
        if (list.size() == 1) {
#ifdef RAGEL_DEBUG
            qDebug() << "Message-Id: " << list[0].data();
#endif
        } else {
#ifdef RAGEL_DEBUG
            qDebug() "invalid Message-Id";
#endif
        }
    }

    action got_in_reply_to_header {
#ifdef RAGEL_DEBUG
        qDebug() << "In-Reply-To: " << list;
#endif
    }

    action got_references_header {
        references += list;
#ifdef RAGEL_DEBUG
        qDebug() << "got_references_header:" << references;
#endif
    }

    action header_error {
#ifdef RAGEL_DEBUG
        qDebug() << "Error when parsing RFC5322 headers";
#endif
    }


    include rfc5322 "rfc5322.rl";

    main := (optional_field | references | obs_references)* @err(header_error) CRLF*;

    write data;
}%%

Rfc5322HeaderParser::Rfc5322HeaderParser():
    m_error(false)
{
}

void Rfc5322HeaderParser::clear()
{
    m_error = false;
    references.clear();
}

bool Rfc5322HeaderParser::parse(const QByteArray &data)
{
    clear();

    const char *p = data.data();
    const char *pe = p + data.length();
    const char *eof = pe;
    int cs;

    QByteArray str;
    QList<QByteArray> list;

    %% write init;
    %% write exec;

    return !m_error;
}

}
}

#if 0
#include <iostream>
int main()
{
    QByteArray data;
    std::string line;

    while (std::getline(std::cin, line)) {
        data += line.c_str();
        data += '\n';
    }

    Imap::Parser::Rfc5322HeaderParser parser;
    bool res = parser.parse(data);
    if (!res) {
        qDebug() << "Parsing error.";
        return 1;
    }

    qDebug() << "References:" << parser.references;

    return 0;
}
#endif
