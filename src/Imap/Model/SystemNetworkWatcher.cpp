/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>
   Copyright (c) 2025 Espen Sandøy Hustad <espen@ehustad.com>

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

#include "SystemNetworkWatcher.h"
#include "Model.h"

#include <QTimer>

namespace Imap {
namespace Mailbox {


QString policyToString(const NetworkPolicy policy)
{
    switch (policy) {
    case NETWORK_OFFLINE:
        return QStringLiteral("NETWORK_OFFLINE");
    case NETWORK_EXPENSIVE:
        return QStringLiteral("NETWORK_EXPENSIVE");
    case NETWORK_ONLINE:
        return QStringLiteral("NETWORK_ONLINE");
    }
    Q_ASSERT(false);
    return QString();
}

QString meteredToString(bool isMetered)
{
    if (isMetered) {
        return QStringLiteral("IS_METERED");
    }

    return QStringLiteral("IS_NOT_METERED");
}

QString reachabilityToString(QNetworkInformation::Reachability reachability)
{
    switch (reachability) {
    case QNetworkInformation::Reachability::Disconnected:
        return QStringLiteral("DISCONNECTED");
    case QNetworkInformation::Reachability::Local:
        return QStringLiteral("LOCAL");
    case QNetworkInformation::Reachability::Online:
        return QStringLiteral("ONLINE");
    case QNetworkInformation::Reachability::Site:
        return QStringLiteral("SITE");
    case QNetworkInformation::Reachability::Unknown:
        return QStringLiteral("UNKNOWN");
    }
    Q_ASSERT(false);
    return QStringLiteral("UNKNOWN");
}

QString transportMediumToString(QNetworkInformation::TransportMedium current)
{
    switch (current) {
    case QNetworkInformation::TransportMedium::Unknown:
        return QStringLiteral("UNKNOWN");
    case QNetworkInformation::TransportMedium::Ethernet:
        return QStringLiteral("ETHERNET");
    case QNetworkInformation::TransportMedium::Cellular:
        return QStringLiteral("CELLULAR");
    case QNetworkInformation::TransportMedium::WiFi:
        return QStringLiteral("WIFI");
    case QNetworkInformation::TransportMedium::Bluetooth:
        return QStringLiteral("BLUETOOTH");
    }
    Q_ASSERT(false);
    return QStringLiteral("UNKNOWN");
}

SystemNetworkWatcher::SystemNetworkWatcher(ImapAccess *parent, Model *model):
    NetworkWatcher(parent, model)
{
    // Wait with logging until the app is fully constructed,
    // and all signals are connected.
    // Otherwise we won't see this in the log.
    QTimer::singleShot(0, this, [this]() {
            this->detectCapabilities();
        });
}

bool SystemNetworkWatcher::init()
{
    QNetworkInformation::loadDefaultBackend();
    return true;
}

void SystemNetworkWatcher::setDesiredNetworkPolicy(const NetworkPolicy policy)
{
    if (policy != m_desiredPolicy) {
        m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"),
                          QStringLiteral("User's preference changed: %1").arg(policyToString(policy)));
        m_desiredPolicy = policy;
    }
    if (m_model->networkPolicy() == NETWORK_OFFLINE && policy != NETWORK_OFFLINE) {
        // We are asked to connect, the model is not connected yet
        if (isOnline()) {
            m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"), QStringLiteral("Network is online -> connecting"));
            reconnectModelNetwork();
        } else {
            // We aren't online yet, but we will become online at some point. When that happens, reconnectModelNetwork() will
            // be called, so there is nothing to do from this place.
            m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"), QStringLiteral("Opening network session"));
        }
    } else if (m_model->networkPolicy() != NETWORK_OFFLINE && policy == NETWORK_OFFLINE) {
        // This is pretty easy -- just disconnect the model
        m_model->setNetworkPolicy(NETWORK_OFFLINE);
        m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"), QStringLiteral("Closing network session"));
    } else {
        // No need to connect/disconnect/reconnect, yay!
        m_model->setNetworkPolicy(m_desiredPolicy);
    }
}

void SystemNetworkWatcher::detectCapabilities()
{
    auto ni = QNetworkInformation::instance();
    QStringList caps;

    if (ni->supports(QNetworkInformation::Feature::Metered)) {
        connect(ni, &QNetworkInformation::isMeteredChanged, this, &SystemNetworkWatcher::onIsMeteredChanged);
        caps.append(QStringLiteral("- METERED: current value (%0)").arg(meteredToString(ni->isMetered())));
    }

    if (ni->supports(QNetworkInformation::Feature::Reachability)) {
        connect(ni, &QNetworkInformation::reachabilityChanged, this, &SystemNetworkWatcher::onReachabilityChanged);
        caps.append(QStringLiteral("- REACHABILITY: current value (%0)").arg(reachabilityToString(ni->reachability())));
    }

    if (ni->supports(QNetworkInformation::Feature::TransportMedium)) {
        connect(ni, &QNetworkInformation::transportMediumChanged, this, &SystemNetworkWatcher::onTransportMediumChanged);
        caps.append(QStringLiteral("- TRANSPORTMEDIUM: current value (%0)").arg(transportMediumToString(
                                                                                              ni->transportMedium())));
    }

    m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"),
                      QStringLiteral("Starting, features detected:"));

    caps.append(QStringLiteral("- BACKEND: %1").arg(ni->backendName()));
    for (const QString &cap : caps) {
        m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"), cap);
    }
}

void SystemNetworkWatcher::onIsMeteredChanged(bool isMetered)
{
    m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"),
                      QStringLiteral("Metered is changed to %0").arg(meteredToString(isMetered)));
    if (isMetered) {
        m_model->setNetworkPolicy(NETWORK_EXPENSIVE);
    }
}

void SystemNetworkWatcher::onReachabilityChanged(QNetworkInformation::Reachability newReachability)
{
    Q_UNUSED(newReachability)

    if (isOnline()) {
        m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"), QStringLiteral("System is back online"));
        setDesiredNetworkPolicy(m_desiredPolicy);
    } else {
        m_model->setNetworkPolicy(NETWORK_OFFLINE);
        // The session remains open, so that we indicate our intention to reconnect after the connectivity is restored
        // (or when a configured AP comes back to range, etc).
    }
}

void SystemNetworkWatcher::onTransportMediumChanged(QNetworkInformation::TransportMedium current)
{
    m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"),
                      QStringLiteral("Transport medium changed to %0").arg(transportMediumToString(current)));
}

bool SystemNetworkWatcher::isOnline()
{
    // This might not be strictly accurate, as the IMAP server
    // could be running on a local or intra network, but we
    // can't really know that
    auto ni = QNetworkInformation::instance();
    return ni->reachability() == QNetworkInformation::Reachability::Online;
}

void SystemNetworkWatcher::reconnectModelNetwork()
{
    m_model->logTrace(0, Common::LOG_OTHER, QStringLiteral("Network Session"),
                        QStringLiteral("Session is %1").arg(
                          QLatin1String(isOnline() ? "online" : "offline")
                          ));
    if (!isOnline()) {
        m_model->setNetworkPolicy(NETWORK_OFFLINE);
        return;
    }

    m_model->setNetworkPolicy(m_desiredPolicy);
}
}
}
