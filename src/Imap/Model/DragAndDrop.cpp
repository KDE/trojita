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

#include "DragAndDrop.h"
#include <QIODevice>
#include <QDataStream>
#include <QMimeData>
#include "Imap/Model/ItemRoles.h"

namespace Imap {
namespace Mailbox {

QMimeData *mimeDataForDragAndDrop(const QModelIndex &index)
{
    if (index.data(RoleMessageUid) == 0) {
        return 0;
    }

    QByteArray buf;
    QDataStream stream(&buf, QIODevice::WriteOnly);
    stream << index.data(RoleMailboxName).toString() <<
              index.data(RoleMailboxUidValidity).toUInt() <<
              index.data(RoleMessageUid).toUInt() <<
              index.data(RolePartPathToPart).toByteArray();

    QMimeData *mimeData = new QMimeData;
    mimeData->setData(MimeTypes::xTrojitaImapPart, buf);
    return mimeData;
}

}

const QString MimeTypes::xTrojitaAttachmentList = QStringLiteral("application/x-trojita-attachments-list");
const QString MimeTypes::xTrojitaMessageList = QStringLiteral("application/x-trojita-message-list");
const QString MimeTypes::xTrojitaImapPart = QStringLiteral("application/x-trojita-imap-part");

}
