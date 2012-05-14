/* Copyright (C) 2006 - 2012 Jan Kundrát <jkt@gentoo.org>

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

#ifndef IMAPACCESS_H
#define IMAPACCESS_H

#include <QObject>

#include "Imap/Model/MailboxModel.h"
#include "Imap/Model/Model.h"
#include "Imap/Model/MsgListModel.h"
#include "Imap/Model/OneMessageModel.h"
#include "Imap/Model/VisibleTasksModel.h"

class QNetworkAccessManager;

class ImapAccess : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *imapModel READ imapModel NOTIFY modelsChanged)
    Q_PROPERTY(QObject *mailboxModel READ mailboxModel NOTIFY modelsChanged)
    Q_PROPERTY(QObject *msgListModel READ msgListModel NOTIFY modelsChanged)
    Q_PROPERTY(QObject *visibleTasksModel READ visibleTasksModel NOTIFY modelsChanged)
    Q_PROPERTY(QObject *oneMessageModel READ oneMessageModel NOTIFY modelsChanged)
    Q_PROPERTY(QNetworkAccessManager *msgQNAM READ msgQNAM NOTIFY modelsChanged)
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(int port READ port WRITE setPort)
    Q_PROPERTY(QString username READ username WRITE setUsername)
    Q_PROPERTY(QString password READ password WRITE setPassword)
    Q_PROPERTY(QString sslMode READ sslMode WRITE setSslMode)

public:
    explicit ImapAccess(QObject *parent = 0);

    QObject *imapModel() const;
    QObject *mailboxModel() const;
    QObject *msgListModel() const;
    QObject *visibleTasksModel() const;
    QObject *oneMessageModel() const;
    QNetworkAccessManager *msgQNAM() const;

    QString server() const;
    void setServer(const QString &server);
    int port() const;
    void setPort(const int port);
    QString username() const;
    void setUsername(const QString &username);
    QString password() const;
    void setPassword(const QString &password);
    QString sslMode() const;
    void setSslMode(const QString &sslMode);

    Q_INVOKABLE void openMessage(const QString &mailbox, const uint uid);
    Q_INVOKABLE QString prettySize(const uint bytes) const;

signals:
    void serverChanged();
    void modelsChanged();

public slots:
    void alertReceived(const QString &message);
    void connectionError(const QString &message);
    void slotLogged(uint parserId, const Imap::Mailbox::LogMessage &message);

private:
    Imap::Mailbox::Model *m_imapModel;
    Imap::Mailbox::AbstractCache *cache;
    Imap::Mailbox::MailboxModel *m_mailboxModel;
    Imap::Mailbox::MsgListModel *m_msgListModel;
    Imap::Mailbox::VisibleTasksModel *m_visibleTasksModel;
    Imap::Mailbox::OneMessageModel *m_oneMessageModel;
    QNetworkAccessManager *m_msgQNAM;

    QString m_server;
    int m_port;
    QString m_username;
    QString m_password;
    QString m_sslMode;
};

#endif // IMAPACCESS_H