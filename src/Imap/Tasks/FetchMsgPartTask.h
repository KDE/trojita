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

#ifndef IMAP_FETCHMSGPARTTASK_H
#define IMAP_FETCHMSGPARTTASK_H

#include <functional>
#include <QPersistentModelIndex>
#include "ImapTask.h"

namespace Imap {
namespace Mailbox {

class TreeItemPart;

/** @short Fetch a message part */
class FetchMsgPartTask : public ImapTask
{
    Q_OBJECT
public:
    FetchMsgPartTask(Model *model, const QModelIndex &mailbox, const Imap::Uids &uids, const QList<QByteArray> &parts);
    void perform() override;

    bool handleFetch(const Imap::Responses::Fetch *const resp) override;
    bool handleStateHelper(const Imap::Responses::State *const resp) override;

    QString debugIdentification() const override;
    QVariant taskData(const int role) const override;
    bool needsMailbox() const override {return true;}
protected:
    void markPendingItemsUnavailable();
    void doForAllParts(const std::function<void(TreeItemPart *, const QByteArray &, const uint)> &f);
    void markPartUnavailable(TreeItemPart *part);
    void handleUnknownCTE(TreeItemPart *part, const QByteArray &partId, const uint uid);
private:
    CommandHandle tag;
    ImapTask *conn;
    Imap::Uids uids;
    QList<QByteArray> parts;
    QPersistentModelIndex mailboxIndex;
};

}
}

#endif // IMAP_FETCHMSGPARTTASK_H
