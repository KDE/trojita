/* Copyright (C) 2013 Thomas Lübking <thomas.luebking@gmail.com>

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

#ifndef SPINNER_H
#define SPINNER_H

class QTimer;
#include <QWidget>

namespace Gui {
/**
 * @short A generic Spinner, ie an overlay displaying some rotating stuff to indicate:
 * Hold on, slow action in process...
 * The Spinner fades in and out and automatically positions itself in the visible area of its
 * parent with 2/3 of the smaller dimension of the visible parent rect.
 */
class Spinner : public QWidget {
    Q_OBJECT
public:
    enum Type { Aperture = 0, Scythe, Elastic, Sun };
    /** Providing a @p parent that is NOT Null is mandatory! */
    explicit Spinner(QWidget *parent);

    /**
     * Whether to delete itself when hiding after the stop finished
     */
    bool autoDelete() const;
    void setAutoDelete(bool autoDelete);

    /**
     * Optional progress indicator values
     * Defaulting to 0,0,-1
     */
    int mimimum() const;
    int maximum() const;
    int value() const;
    /** The optional text */
    QString text() const;
    /** Set a type to control the look a bit */
    void setType(const Type t);
    Type type() const;
public slots:
    /**
     * QProgressBar-a-like API
     * When setting minimum != maximum and as soon as value becomes valid, the spinner
     * spins on until the target value is reached and then will stop and wait for further
     * value updates until minimum and maximum are set to an equal value (restarting the spinning)
     *
     * Consider using the "Sun" type if you intend to use the spinner as progress indicator
     * While every mode is supported, the visual result will not reflect a progress indicator
     */
    void setMaximum(const int maximum);
    void setMinimum(const int minimum);
    void setValue(const int value);
    /**
     * Set an optional text in the center of the rotating stuff
     * the text of course does not rotate ;-)
     */
    void setText(const QString &text);
    /**
     * You can start the spinner as much as you want. If it's still visible, this will just keep it
     * alive (or do nothing)
     * The optional @p delay in milliseconds can be used if you've no idea when you'll stop the
     * spinner and want to avoid it if your stop condition triggers in eg. less then 250ms
     * Ie. if you call m_spinner->start(250); QTimer::singleShot(100, m_spinner, SLOT(stop()));
     * the spinner will never actually start (because the stop call arrives before the delay times
     * out)
     */
    void start(uint delay = 0);
    /** Stop the spinner, the spinner will fade out for around a second before it's really hidden */
    void stop();

protected:
    bool event(QEvent *e);
    bool eventFilter(QObject *o, QEvent *e);
    void paintEvent(QPaintEvent *e);
    void timerEvent(QTimerEvent *e);
private slots:
    void updateAncestors();
    void updateGeometry();
private:
    void checkProgressMode();
private:
    uchar m_step;
    char m_fadeStep;
    int m_timer;
    QTimer *m_startTimer;
    QList<QWidget*> m_ancestors;
    QString m_text;
    int m_textCols;
    Type m_type;
    bool m_geometryDirty;
    int m_minimum, m_maximum, m_value;
    uchar m_valueStep;
    bool m_autoDelete;
};

} // namespace

#endif // SPINNER_H
