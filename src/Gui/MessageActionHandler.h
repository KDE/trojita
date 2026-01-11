/* Copyright (C) 2006 - 2016 Jan Kundrát <jkt@kde.org>
   Copyright (C) 2014 - 2015 Stephan Platz <trojita@paalsteek.de>
   Copyright (C) 2026 Espen Sandøy Hustad <espen@ehustad.com>

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
#ifndef GUI_MESSAGEACTIONHANDLER_H
#define GUI_MESSAGEACTIONHANDLER_H

#include "Composer/Recipients.h"

#include <QString>

class QModelIndex;

namespace Gui
{

class MainWindow;

class MessageActionHandler
{
public:
    enum MessageType {
        Message,
        CryptoMessage
    };

    MessageActionHandler(MainWindow *parent);

    void forward(const QModelIndex &messageIndex, const Composer::ForwardMode mode);
    void reply(const QModelIndex &messageIndex, const Composer::ReplyMode mode, const QString &text, const MessageType messageType);

private:
    [[nodiscard]] QString quoteText(const QModelIndex &messageIndex, const QString &text, const MessageType messageType) const;

    MainWindow *m_parent;
};

}

#endif
