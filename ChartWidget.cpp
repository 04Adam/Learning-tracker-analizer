// ChartWidget.cpp
#include "ChartWidget.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

static const QColor kMuted(0x9d, 0xa0, 0xa8);
static const QColor kGrid(0x34, 0x37, 0x3c);

static QString formatDuration(qint64 seconds) {
    const qint64 h = seconds / 3600;
    const qint64 m = seconds / 60 % 60;
    if (h > 0 && m > 0) return QString("%1h %2m").arg(h).arg(m);
    if (h > 0) return QString("%1h").arg(h);
    if (m > 0 || seconds == 0) return QString("%1m").arg(m);
    return QString("%1s").arg(seconds);
}

ChartWidget::ChartWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    reload();
}

void ChartWidget::setView(Range newRange, Mode newMode, QDate newDate) {
    range = newRange;
    mode = newMode;
    date = newDate;
    hoverTime = -1;
    reload();
}

void ChartWidget::reload() {
    const QDate anchor = date.isValid() ? date : QDate::currentDate();
    if (range == Range::Today) {
        firstDate = anchor;
        rangeEnd = anchor.addDays(1).startOfDay().toSecsSinceEpoch();
    } else {
        firstDate = anchor.addDays(1 - anchor.dayOfWeek());
        rangeEnd = firstDate.addDays(7).startOfDay().toSecsSinceEpoch();
    }
    rangeStart = firstDate.startOfDay().toSecsSinceEpoch();
    lineEnd = std::clamp(QDateTime::currentSecsSinceEpoch(), rangeStart, rangeEnd);

    subjects = subjectsWithTodayTotals();
    sessions = sessionsBetween(rangeStart, rangeEnd);

    totals.clear();
    for (const Session& s : sessions)
        totals[s.subjectId] += s.end - s.start;

    update();
}

QRectF ChartWidget::plotRect() const {
    return QRectF(72, 88, width() - 72 - 28, height() - 88 - 44);
}

QString ChartWidget::title() const {
    const QLocale en(QLocale::English);
    const QDate today = QDate::currentDate();
    if (range == Range::Today) {
        const QString day = firstDate == today ? "Today" : en.toString(firstDate, "dddd, d MMMM yyyy");
        return day + (mode == Mode::Totals ? " · time per subject"
                                           : " · cumulative time through the day");
    }
    const QString week = firstDate.daysTo(today) < 7 && firstDate <= today
        ? "This week"
        : en.toString(firstDate, "d MMM") + " – " + en.toString(firstDate.addDays(6), "d MMM yyyy");
    return week + (mode == Mode::Totals ? " · time per subject" : " · cumulative time by day");
}

qint64 ChartWidget::axisStep() const {
    qint64 maxValue = 0;
    for (qint64 v : totals)
        maxValue = std::max(maxValue, v);

    const qint64 steps[] = {60, 120, 300, 600, 900, 1800, 3600, 7200,
                            4 * 3600, 6 * 3600, 12 * 3600, 24 * 3600};
    for (qint64 step : steps) {
        if (maxValue <= step * 5)
            return step;
    }
    return 48 * 3600;
}

qint64 ChartWidget::axisTop() const {
    qint64 maxValue = 0;
    for (qint64 v : totals)
        maxValue = std::max(maxValue, v);

    const qint64 step = axisStep();
    return std::max(step, (maxValue + step - 1) / step * step);
}

qreal ChartWidget::xForTime(qint64 t, const QRectF& plot) const {
    return plot.left() + plot.width() * qreal(t - rangeStart) / qreal(rangeEnd - rangeStart);
}

qint64 ChartWidget::cumulativeAt(int subjectId, qint64 t) const {
    qint64 sum = 0;
    for (const Session& s : sessions) {
        if (s.subjectId == subjectId && s.start < t)
            sum += std::min(s.end, t) - s.start;
    }
    return sum;
}

void ChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 3);
    p.setFont(titleFont);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(16, 10, width() - 32, 30), Qt::AlignLeft | Qt::AlignVCenter, title());
    p.setFont(font());

    const QRectF plot = plotRect();
    if (plot.width() < 50 || plot.height() < 50)
        return;

    drawValueAxis(p, plot);
    if (mode == Mode::Totals) {
        drawTotals(p, plot);
    } else {
        drawLegend(p);
        drawCumulative(p, plot);
    }

    if (sessions.isEmpty()) {
        p.setPen(kMuted);
        p.drawText(plot, Qt::AlignCenter, "No study time recorded in this period");
    }
}

void ChartWidget::drawLegend(QPainter& p) const {
    const QFontMetricsF fm(font());
    qreal x = 16;
    for (const SubjectTotal& s : subjects) {
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRoundedRect(QRectF(x, 53, 12, 12), 3, 3);
        x += 18;

        const QString name = s.name + "  ";
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QPointF(x, 64), name);
        x += fm.horizontalAdvance(name);

        const QString value = formatDuration(totals.value(s.id));
        p.setPen(kMuted);
        p.drawText(QPointF(x, 64), value);
        x += fm.horizontalAdvance(value) + 24;
    }
}

void ChartWidget::drawValueAxis(QPainter& p, const QRectF& plot) const {
    const qint64 step = axisStep();
    const qint64 top = axisTop();
    for (qint64 v = 0; v <= top; v += step) {
        const qreal y = plot.bottom() - plot.height() * qreal(v) / qreal(top);
        p.setPen(QPen(kGrid, 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(kMuted);
        p.drawText(QRectF(0, y - 10, plot.left() - 10, 20),
                   Qt::AlignRight | Qt::AlignVCenter, formatDuration(v));
    }
}

void ChartWidget::drawTotals(QPainter& p, const QRectF& plot) const {
    if (subjects.isEmpty()) return;

    const qint64 top = axisTop();
    const qreal slot = plot.width() / subjects.size();
    const qreal barWidth = std::min(slot * 0.45, 110.0);

    for (int i = 0; i < subjects.size(); ++i) {
        const SubjectTotal& s = subjects[i];
        const qint64 value = totals.value(s.id);
        const qreal center = plot.left() + slot * (i + 0.5);
        const qreal barHeight = plot.height() * qreal(value) / qreal(top);
        const QRectF bar(center - barWidth / 2, plot.bottom() - barHeight, barWidth, barHeight);

        // Zaokrąglamy tylko górę słupka: dół jest przycięty do linii zera.
        p.save();
        p.setClipRect(plot);
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRoundedRect(bar.adjusted(0, 0, 0, 6), 4, 4);
        p.restore();

        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRectF(center - slot / 2, bar.top() - 24, slot, 20),
                   Qt::AlignHCenter | Qt::AlignBottom, formatDuration(value));
        p.drawText(QRectF(center - slot / 2, plot.bottom() + 8, slot, 20),
                   Qt::AlignHCenter | Qt::AlignTop, s.name);
    }
}

void ChartWidget::drawCumulative(QPainter& p, const QRectF& plot) const {
    // Oś czasu: godziny w widoku dnia, dni tygodnia w widoku tygodnia.
    if (range == Range::Today) {
        for (int hour = 0; hour <= 24; hour += 3) {
            const qreal x = plot.left() + plot.width() * hour / 24.0;
            p.setPen(QPen(kGrid, 1));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(kMuted);
            p.drawText(QRectF(x - 30, plot.bottom() + 8, 60, 20),
                       Qt::AlignHCenter | Qt::AlignTop, QString("%1:00").arg(hour));
        }
    } else {
        const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        const qreal dayWidth = plot.width() / 7;
        for (int day = 0; day <= 7; ++day) {
            const qreal x = plot.left() + dayWidth * day;
            p.setPen(QPen(kGrid, 1));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            if (day < 7) {
                p.setPen(kMuted);
                p.drawText(QRectF(x, plot.bottom() + 8, dayWidth, 20),
                           Qt::AlignHCenter | Qt::AlignTop, days[day]);
            }
        }
    }

    const qint64 top = axisTop();
    const auto yForValue = [&](qint64 v) {
        return plot.bottom() - plot.height() * qreal(v) / qreal(top);
    };

    for (const SubjectTotal& s : subjects) {
        if (totals.value(s.id) == 0) continue;

        // Suma prefiksowa: płasko w przerwach, w górę w trakcie nauki.
        QPolygonF line;
        qint64 sum = 0;
        line << QPointF(plot.left(), plot.bottom());
        for (const Session& session : sessions) {
            if (session.subjectId != s.id) continue;
            line << QPointF(xForTime(session.start, plot), yForValue(sum));
            sum += session.end - session.start;
            line << QPointF(xForTime(session.end, plot), yForValue(sum));
        }
        const QPointF last(xForTime(lineEnd, plot), yForValue(sum));
        line << last;

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(s.color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(line);

        p.setPen(QPen(palette().color(QPalette::Window), 2));
        p.setBrush(s.color);
        p.drawEllipse(last, 5, 5);
    }

    if (hoverTime >= 0) {
        const qreal x = xForTime(hoverTime, plot);
        p.setPen(QPen(kMuted, 1));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
}

void ChartWidget::mouseMoveEvent(QMouseEvent* event) {
    const QRectF plot = plotRect();
    const QPointF pos = event->position();
    QString text;
    qint64 newHover = -1;

    if (plot.contains(pos) && !subjects.isEmpty()) {
        if (mode == Mode::Totals) {
            const int index = std::min(int((pos.x() - plot.left()) / (plot.width() / subjects.size())),
                                       int(subjects.size()) - 1);
            const SubjectTotal& s = subjects[index];
            text = QString("<b>%1</b><br>%2").arg(s.name.toHtmlEscaped(),
                                                  formatDuration(totals.value(s.id)));
        } else {
            const qint64 t = rangeStart
                + qint64((pos.x() - plot.left()) / plot.width() * (rangeEnd - rangeStart));
            if (t <= lineEnd) {
                newHover = t;
                text = "<b>" + QLocale(QLocale::English).toString(
                           QDateTime::fromSecsSinceEpoch(t),
                           range == Range::Today ? "HH:mm" : "dddd HH:mm") + "</b>";
                for (const SubjectTotal& s : subjects) {
                    text += QString("<br><span style='color:%1'>■</span> %2: %3")
                                .arg(s.color.name(), s.name.toHtmlEscaped(),
                                     formatDuration(cumulativeAt(s.id, t)));
                }
            }
        }
    }

    if (newHover != hoverTime) {
        hoverTime = newHover;
        update();
    }
    if (text.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

void ChartWidget::leaveEvent(QEvent*) {
    hoverTime = -1;
    update();
}
