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

#include "TagHandler.h"

#include "Imap/Model/ItemRoles.h"
#include "Imap/Model/Model.h"

#include <QModelIndex>
#include <QString>

namespace Gui
{

void TagHandler::addTag(const QModelIndex &messageIndex, const QString &tag)
{
    if (!messageIndex.isValid()) {
        return;
    }

    Imap::Mailbox::Model *model = dynamic_cast<Imap::Mailbox::Model *>(const_cast<QAbstractItemModel *>(messageIndex.model()));
    model->setMessageFlags(QModelIndexList() << messageIndex, tag, Imap::Mailbox::FLAG_ADD);
}

void TagHandler::removeTag(const QModelIndex &messageIndex, const QString &tag)
{
    if (!messageIndex.isValid()) {
        return;
    }

    Imap::Mailbox::Model *model = dynamic_cast<Imap::Mailbox::Model *>(const_cast<QAbstractItemModel *>(messageIndex.model()));
    model->setMessageFlags(QModelIndexList() << messageIndex, tag, Imap::Mailbox::FLAG_REMOVE);
}

QStringList TagHandler::tags(const QModelIndex &messageIndex) const
{
    if (!messageIndex.isValid()) {
        return QStringList();
    }

    return messageIndex.data(Imap::Mailbox::RoleMessageFlags).toStringList();
}

}
