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

#ifndef IMAP_OFFLINECONNECTIONTASK_H
#define IMAP_OFFLINECONNECTIONTASK_H

#include "ImapTask.h"
#include "../Model/Model.h"

namespace Imap
{
namespace Mailbox
{

/** @short Create new "connection" that will fail immediately

The whole point of this class is to create a Parser because the whole stack depends on its existence too much.
*/
class OfflineConnectionTask : public ImapTask
{
    Q_OBJECT
public:
    explicit OfflineConnectionTask(Model *model);
    void perform() override;
    QVariant taskData(const int role) const override;
    bool needsMailbox() const override {return false;}
protected slots:
    void slotPerform();
    void slotDie();
};

}
}

#endif // IMAP_OFFLINECONNECTIONTASK_H
