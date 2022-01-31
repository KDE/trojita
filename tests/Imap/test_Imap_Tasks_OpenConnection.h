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

#ifndef TEST_IMAP_MODEL_OPENCONNECTIONTASK
#define TEST_IMAP_MODEL_OPENCONNECTIONTASK

#include "Imap/Tasks/OpenConnectionTask.h"
#include "Streams/SocketFactory.h"
#include "Utils/LibMailboxSync.h"

class QSignalSpy;

class ImapModelOpenConnectionTest : public LibMailboxSync
{
    Q_OBJECT
private slots:
    void init();
    void initTestCase();

    void testPreauth();
    void testPreauthWithCapability();
    void testPreauthWithStartTlsWanted();

    void testOk();
    void testOkWithCapability();
    void testOkMissingImap4rev1();

    void testOkLogindisabled();
    void testOkLogindisabledWithoutStarttls();
    void testOkLogindisabledLater();

    void testOkStartTls();
    void testOkStartTlsForbidden();
    void testOkStartTlsDiscardCaps();
    void testCapabilityAfterLogin();

    void testCompressDeflateOk();
    void testCompressDeflateNo();

    void testOpenConnectionShallBlock();

    void testLoginDelaysOtherTasks();

    void testInitialBye();

    void testInitialGarbage();
    void testInitialGarbage_data();

    void testAuthFailure();
    void testAuthFailureNoRespCode();

    void testExcessivePasswordPrompts();

    void dataBug432353();
    void testNoListStatusUponConnect();
    void testNoListStatusUponConnect_data();
    void testNoListStatusStartTls();
    void testNoListStatusStartTls_data();
    void testNoListStatusBeforeAuthenticated();
    void testNoListStatusBeforeAuthenticated_data();
    void testListStatusUnsolicited();

    void provideAuthDetails();
    void acceptSsl(const QList<QSslCertificate> &certificateChain, const QList<QSslError> &sslErrors);

protected:
    enum class TlsRequired { No, Yes };
    void reinit(const TlsRequired tlsRequired = TlsRequired::No);

private:
    QPointer<Imap::Mailbox::OpenConnectionTask> task;
    std::unique_ptr<QSignalSpy> completedSpy;
    std::unique_ptr<QSignalSpy> failedSpy;
    std::unique_ptr<QSignalSpy> authSpy;
    std::unique_ptr<QSignalSpy> connErrorSpy;
    std::unique_ptr<QSignalSpy> startTlsUpgradeSpy;
    std::unique_ptr<QSignalSpy> authErrorSpy;

    bool m_enableAutoLogin;
};

#endif
