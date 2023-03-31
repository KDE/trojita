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

#ifndef IMAP_EXPUNGEMAILBOX_TASK_H
#define IMAP_EXPUNGEMAILBOX_TASK_H

#include <QPersistentModelIndex>
#include "ImapTask.h"

namespace Imap
{
namespace Mailbox
{

/** @short Expunge all messages marked as \\Deleted in the mailbox

Please note that this task simply sends the EXPUNGE command; no careful flag
synchronization is performed.
 */
class ExpungeMailboxTask : public ImapTask
{
    Q_OBJECT
public:
    ExpungeMailboxTask(Model *model, const QModelIndex &mailbox);
    void perform() override;

    bool handleStateHelper(const Imap::Responses::State *const resp) override;
    QVariant taskData(const int role) const override;
    bool needsMailbox() const override {return true;}
private:
    CommandHandle tag;
    ImapTask *conn;
    QPersistentModelIndex mailboxIndex;
};

}
}

#endif // IMAP_EXPUNGEMAILBOX_TASK_H
