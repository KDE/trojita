/* Copyright (C) 2006 - 2016 Jan Kundrát <jkt@kde.org>
   Copyright (C) 2014        Luke Dashjr <luke+trojita@dashjr.org>
   Copyright (C) 2014 - 2015 Stephan Platz <trojita@paalsteek.de>

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

#include "MessageActionHandler.h"

#include "Composer/SubjectMangling.h"
#include "Imap/Model/ItemRoles.h"

#include "ComposeWidget.h"
#include "Window.h"

#include <QByteArray>
#include <QList>
#include <QModelIndex>

namespace Gui
{

MessageActionHandler::MessageActionHandler(MainWindow *parent)
    : m_parent(parent)
{
}

void MessageActionHandler::forward(const QModelIndex &messageIndex, const Composer::ForwardMode mode)
{
    if (!messageIndex.isValid())
        return;

    // The Message-Id of the original message might have been empty; be sure we can handle that
    QByteArray messageId = messageIndex.data(Imap::Mailbox::RoleMessageMessageId).toByteArray();
    QList<QByteArray> messageIdList;
    if (!messageId.isEmpty()) {
        messageIdList.append(messageId);
    }

    ComposeWidget::warnIfMsaNotConfigured(
        ComposeWidget::createForward(m_parent,
                                     mode,
                                     messageIndex,
                                     Composer::Util::forwardSubject(messageIndex.data(Imap::Mailbox::RoleMessageSubject).toString()),
                                     messageIdList,
                                     messageIndex.data(Imap::Mailbox::RoleMessageHeaderReferences).value<QList<QByteArray>>() + messageIdList),
        m_parent);
}

void MessageActionHandler::reply(const QModelIndex &messageIndex, const Composer::ReplyMode mode, const QString &quoteText)
{
    if (!messageIndex.isValid())
        return;

    // The Message-Id of the original message might have been empty; be sure we can handle that
    QByteArray messageId = messageIndex.data(Imap::Mailbox::RoleMessageMessageId).toByteArray();
    QList<QByteArray> messageIdList;
    if (!messageId.isEmpty()) {
        messageIdList.append(messageId);
    }

    ComposeWidget::warnIfMsaNotConfigured(
        ComposeWidget::createReply(m_parent,
                                   mode,
                                   messageIndex,
                                   QList<QPair<Composer::RecipientKind, QString>>(),
                                   Composer::Util::replySubject(messageIndex.data(Imap::Mailbox::RoleMessageSubject).toString()),
                                   quoteText,
                                   messageIdList,
                                   messageIndex.data(Imap::Mailbox::RoleMessageHeaderReferences).value<QList<QByteArray>>() + messageIdList),
        m_parent);
}

}
