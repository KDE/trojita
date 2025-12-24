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

#ifndef SENDERIDENTITIESMODEL_H
#define SENDERIDENTITIESMODEL_H

#include <QAbstractTableModel>

class QSettings;

namespace Composer
{

class ItemSenderIdentity
{
public:
    ItemSenderIdentity(const QString &realName, const QString &emailAddress,
                       const QString &organisation, const QString &signature);
    ItemSenderIdentity();

    QString realName;
    QString emailAddress;
    QString organisation;
    QString signature;
};

/** @short Model for a list of available sender identities */
class SenderIdentitiesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum { COLUMN_NAME, COLUMN_EMAIL, COLUMN_ORGANIZATION, COLUMN_SIGNATURE, COLUMN_LAST };

    explicit SenderIdentitiesModel(QObject *parent = 0);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    /** @short Reimplemented from QAbstractTableModel; required for QDataWidgetMapper. */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void appendIdentity(const ItemSenderIdentity &item);
    void removeIdentityAt(const int position);
    void moveIdentity(const int from, const int to);

    /** @short Replace the contents of this model by data read from a QSettings instance */
    void loadFromSettings(QSettings &s);
    /** @short Save the data into a QSettings instance */
    void saveToSettings(QSettings &s) const;

private:
    QList<ItemSenderIdentity> m_identities;
};

}

#endif // SENDERIDENTITIESMODEL_H
