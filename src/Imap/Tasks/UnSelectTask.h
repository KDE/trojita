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

#ifndef IMAP_UNSELECTTASK_H
#define IMAP_UNSELECTTASK_H

#include "ImapTask.h"

namespace Imap
{
namespace Mailbox
{

/** @short Get out from a mailbox as soon as possible */
class UnSelectTask : public ImapTask
{
    Q_OBJECT
public:
    UnSelectTask(Model *model, ImapTask *parentTask);
    void perform() override;

    bool handleStateHelper(const Imap::Responses::State *const resp) override;
    bool handleNumberResponse(const Imap::Responses::NumberResponse *const resp) override;
    bool handleFlags(const Imap::Responses::Flags *const resp) override;
    bool handleSearch(const Imap::Responses::Search *const resp) override;
    bool handleFetch(const Imap::Responses::Fetch *const resp) override;
    QVariant taskData(const int role) const override;
    bool needsMailbox() const override {return true;}
private slots:
    /** @short Try to guess a non-existing mailbox name */
    void doFakeSelect();
    void resetConnectionState();
private:
    CommandHandle unSelectTag;
    CommandHandle selectMissingTag;
    ImapTask *conn;
};

}
}

#endif // IMAP_UNSELECTTASK_H
