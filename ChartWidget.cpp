// ChartWidget.cpp
#include "ChartWidget.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>
#include <QToolTip>

#include <algorithm>

static const QColor kMuted(0x9d, 0xa0, 0xa8);
static const QColor kGrid(0x34, 0x37, 0x3c);
// Miejsce nad słupkiem na etykietę "+N" z liczbą wykonanych zadań.
static const qreal kBadgeHeight = 20;
// Dłuższa lista w dymku wychodziłaby poza ekran.
static const int kMaxListedTodos = 12;

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

void ChartWidget::setSubject(int subjectId) {
    subjectFilter = subjectId;
    hoverTime = -1;
    reload();
}

void ChartWidget::setInlineDetails(bool enabled) {
    inlineDetails = enabled;
}

void ChartWidget::reload() {
    // Narysowane szczegóły nie odświeżają się same, więc nie mogą przeżyć nowych danych.
    if (inlineDetails) {
        detailsText.clear();
        hoverTime = -1;
    }

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
    if (subjectFilter != -1) {
        subjects.removeIf([this](const SubjectTotal& s) { return s.id != subjectFilter; });
        sessions.removeIf([this](const Session& s) { return s.subjectId != subjectFilter; });
    }
    doneTodos = todosDoneWithSessions(rangeStart, rangeEnd);
    if (subjectFilter != -1)
        doneTodos.removeIf([this](const DoneTodo& t) { return t.subjectId != subjectFilter; });

    totals.clear();
    for (const Session& s : sessions)
        totals[s.subjectId] += s.end - s.start;

    dayTotals.clear();
    if (range == Range::Week) {
        // Odcinek przechodzący przez północ liczy się do obu dni.
        for (int day = 0; day < 7; ++day) {
            const qint64 dayStart = firstDate.addDays(day).startOfDay().toSecsSinceEpoch();
            const qint64 dayEnd = firstDate.addDays(day + 1).startOfDay().toSecsSinceEpoch();
            QHash<int, qint64> dayTotal;
            for (const Session& s : sessions) {
                const qint64 overlap = std::min(s.end, dayEnd) - std::max(s.start, dayStart);
                if (overlap > 0)
                    dayTotal[s.subjectId] += overlap;
            }
            dayTotals.append(dayTotal);
        }
    }

    update();
}

QRectF ChartWidget::plotRect() const {
    qreal top = 88 + summaryHeight();
    if (mode != Mode::Totals && !subjects.isEmpty())
        top += legendPositions().last().y() - 53 - summaryHeight();
    // Etykieta "+N" nad najwyższym słupkiem dnia nie może wejść na legendę.
    if (mode == Mode::Daily)
        top += 8;
    return QRectF(72, top, width() - 72 - 28, height() - top - 44);
}

// Lewy górny róg każdego wpisu; na wąskim ekranie legenda zawija się do kolejnych wierszy.
QList<QPointF> ChartWidget::legendPositions() const {
    const QFontMetricsF fm(font());
    QList<QPointF> result;
    qreal x = 16;
    qreal y = 53 + summaryHeight();
    for (const SubjectTotal& s : subjects) {
        const qreal entryWidth = 18 + fm.horizontalAdvance(s.name + "  ")
                                 + fm.horizontalAdvance(formatDuration(totals.value(s.id)));
        if (x > 16 && x + entryWidth > width() - 16) {
            x = 16;
            y += 20;
        }
        result.append({x, y});
        x += entryWidth + 24;
    }
    return result;
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
    switch (mode) {
        case Mode::Totals:     return week + " · time per subject";
        case Mode::Cumulative: return week + " · cumulative time by day";
        case Mode::Daily:      return week + " · time per subject each day";
    }
    return week;
}

qreal ChartWidget::summaryHeight() const {
    return range == Range::Week ? 34 : 0;
}

void ChartWidget::drawWeekSummary(QPainter& p) const {
    qint64 sum = 0;
    for (qint64 v : totals)
        sum += v;
    // W trwającym tygodniu średnia tylko z dni, które już były, razem z dzisiejszym.
    const qint64 days = std::clamp<qint64>(firstDate.daysTo(QDate::currentDate()) + 1, 1, 7);
    const QString note =
        days < 7 ? QString("(%1 %2 so far)").arg(days).arg(days == 1 ? "day" : "days") : QString();

    QFont valueFont = font();
    valueFont.setBold(true);
    valueFont.setPointSizeF(QFontInfo(valueFont).pointSizeF() + 4);
    const QFontMetricsF labelFm(font());
    const QFontMetricsF valueFm(valueFont);

    // Podpis zwykłym szarym pismem, wartość dużą i pogrubioną, w jednej linii bazowej.
    const struct { QString label; QString value; } stats[] = {
        {"Total", formatDuration(sum)},
        {"Daily average", formatDuration(sum / days)},
    };
    const qreal baseline = 40 + summaryHeight() / 2 + valueFm.ascent() / 2 - 2;
    qreal x = 16;
    for (const auto& stat : stats) {
        p.setFont(font());
        p.setPen(kMuted);
        p.drawText(QPointF(x, baseline), stat.label);
        x += labelFm.horizontalAdvance(stat.label + " ");
        p.setFont(valueFont);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QPointF(x, baseline), stat.value);
        x += valueFm.horizontalAdvance(stat.value) + 22;
    }
    // Dopisek o niepełnym tygodniu tylko wtedy, gdy się mieści.
    p.setFont(font());
    if (!note.isEmpty() && x - 16 + labelFm.horizontalAdvance(note) <= width() - 16) {
        p.setPen(kMuted);
        p.drawText(QPointF(x - 16, baseline), note);
    }
}

QList<const SubjectTotal*> ChartWidget::barOrder() const {
    QList<const SubjectTotal*> result;
    for (const SubjectTotal& s : subjects)
        result.append(&s);
    if (range == Range::Week) {
        std::stable_sort(result.begin(), result.end(),
                         [this](const SubjectTotal* a, const SubjectTotal* b) {
                             return totals.value(a->id) > totals.value(b->id);
                         });
    }
    return result;
}

qint64 ChartWidget::maxValue() const {
    qint64 result = 0;
    if (mode == Mode::Daily) {
        // Słupki dnia stoją jeden na drugim, więc liczy się suma dnia.
        for (const QHash<int, qint64>& dayTotal : dayTotals) {
            qint64 daySum = 0;
            for (qint64 v : dayTotal)
                daySum += v;
            result = std::max(result, daySum);
        }
    } else {
        for (qint64 v : totals)
            result = std::max(result, v);
    }
    return result;
}

qint64 ChartWidget::axisStep() const {
    const qint64 maxValue = this->maxValue();

    const qint64 steps[] = {60, 120, 300, 600, 900, 1800, 3600, 7200,
                            4 * 3600, 6 * 3600, 12 * 3600, 24 * 3600};
    for (qint64 step : steps) {
        if (maxValue <= step * 5)
            return step;
    }
    return 48 * 3600;
}

qint64 ChartWidget::axisTop() const {
    const qint64 maxValue = this->maxValue();
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

// Przedmioty z nauką danego dnia, od najdłuższej (dół słupka) do najkrótszej.
QList<const SubjectTotal*> ChartWidget::subjectsByDayTime(int day) const {
    QList<const SubjectTotal*> result;
    for (const SubjectTotal& s : subjects) {
        if (dayTotals[day].value(s.id) > 0)
            result.append(&s);
    }
    std::stable_sort(result.begin(), result.end(), [&](const SubjectTotal* a, const SubjectTotal* b) {
        return dayTotals[day].value(a->id) > dayTotals[day].value(b->id);
    });
    return result;
}

QList<const DoneTodo*> ChartWidget::doneTodosFor(int subjectId, int day) const {
    QList<const DoneTodo*> result;
    for (const DoneTodo& t : doneTodos) {
        if (subjectId != -1 && t.subjectId != subjectId) continue;
        if (day != -1 && firstDate.daysTo(QDateTime::fromSecsSinceEpoch(t.at).date()) != day)
            continue;
        result.append(&t);
    }
    return result;
}

QString ChartWidget::doneTodosHtml(const QList<const DoneTodo*>& todos) const {
    if (todos.isEmpty()) return {};

    QHash<int, QColor> colors;
    for (const SubjectTotal& s : subjects)
        colors.insert(s.id, s.color);

    const QLocale en(QLocale::English);
    QString html = QString("<br><br><b>+%1 %2 done</b>")
                       .arg(todos.size()).arg(todos.size() == 1 ? "task" : "tasks");
    for (int i = 0; i < todos.size() && i < kMaxListedTodos; ++i) {
        const DoneTodo& t = *todos[i];
        const QDateTime done = QDateTime::fromSecsSinceEpoch(t.done);
        // W tygodniu sama godzina nie mówi, którego dnia to było.
        const QString when = en.toString(done, range == Range::Today && done.date() == firstDate
                                                   ? "HH:mm" : "ddd HH:mm");
        html += QString("<br><span style='color:%1'>✓</span> %2 <span style='color:%3'>%4</span>")
                    .arg(colors.value(t.subjectId, kMuted).name(), t.text.toHtmlEscaped(),
                         kMuted.name(), when);
    }
    if (todos.size() > kMaxListedTodos)
        html += QString("<br><span style='color:%1'>…and %2 more</span>")
                    .arg(kMuted.name()).arg(todos.size() - kMaxListedTodos);
    return html;
}

void ChartWidget::drawTodoBadge(QPainter& p, const QPointF& bottomCenter, int count,
                                const QColor& color) const {
    if (count <= 0) return;
    QFont bold = font();
    bold.setBold(true);
    const QString text = QString("+%1").arg(count);
    const qreal w = QFontMetricsF(bold).horizontalAdvance(text) + 14;
    const QRectF pill(bottomCenter.x() - w / 2, bottomCenter.y() - kBadgeHeight, w, kBadgeHeight);

    p.save();
    p.setPen(QPen(color, 1));
    QColor fill = color;
    fill.setAlpha(60);
    p.setBrush(fill);
    p.drawRoundedRect(pill, kBadgeHeight / 2, kBadgeHeight / 2);
    p.setFont(bold);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(pill, Qt::AlignCenter, text);
    p.restore();
}

void ChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(QFontInfo(titleFont).pointSizeF() + 3);
    p.setFont(titleFont);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(16, 10, width() - 32, 30), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(title(), Qt::ElideRight, width() - 32));
    p.setFont(font());
    if (range == Range::Week)
        drawWeekSummary(p);

    const QRectF plot = plotRect();
    if (plot.width() < 50 || plot.height() < 50)
        return;

    drawValueAxis(p, plot);
    if (mode == Mode::Totals) {
        drawTotals(p, plot);
    } else {
        drawLegend(p);
        drawTimeAxis(p, plot);
        if (mode == Mode::Cumulative)
            drawCumulative(p, plot);
        else
            drawDaily(p, plot);
    }

    if (sessions.isEmpty()) {
        p.setPen(kMuted);
        p.drawText(plot, Qt::AlignCenter, "No study time recorded in this period");
    }

    if (!detailsText.isEmpty())
        drawDetails(p);
}

void ChartWidget::drawDetails(QPainter& p) const {
    QTextDocument text;
    text.setDefaultFont(font());
    text.setDocumentMargin(8);
    text.setHtml(QString("<div style='color:%1'>%2</div>")
                     .arg(palette().color(QPalette::WindowText).name(), detailsText));

    // Nad palcem, żeby dłoń nie zasłaniała treści, ale zawsze w granicach wykresu.
    QRectF box(QPointF(), text.size());
    box.moveCenter(detailsPos);
    box.moveBottom(detailsPos.y() - 28);
    box.moveLeft(std::clamp(box.left(), 4.0, std::max(4.0, width() - 4 - box.width())));
    box.moveTop(std::max(box.top(), 4.0));

    p.setPen(QPen(kMuted, 1));
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(box, 6, 6);
    p.translate(box.topLeft());
    text.drawContents(&p);
}

void ChartWidget::drawLegend(QPainter& p) const {
    const QFontMetricsF fm(font());
    const QList<QPointF> positions = legendPositions();
    for (int i = 0; i < subjects.size(); ++i) {
        const SubjectTotal& s = subjects[i];
        qreal x = positions[i].x();
        const qreal y = positions[i].y();

        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRoundedRect(QRectF(x, y, 12, 12), 3, 3);
        x += 18;

        const QString name = s.name + "  ";
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QPointF(x, y + 11), name);
        x += fm.horizontalAdvance(name);

        p.setPen(kMuted);
        p.drawText(QPointF(x, y + 11), formatDuration(totals.value(s.id)));
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

    const QList<const SubjectTotal*> order = barOrder();
    for (int i = 0; i < order.size(); ++i) {
        const SubjectTotal& s = *order[i];
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
        drawTodoBadge(p, QPointF(center, bar.top() - 28), doneTodosFor(s.id, -1).size(), s.color);
    }
}

void ChartWidget::drawTimeAxis(QPainter& p, const QRectF& plot) const {
    // Oś czasu: godziny w widoku dnia, dni tygodnia w widoku tygodnia.
    if (range == Range::Today) {
        // Na wąskim ekranie podpisy co trzy godziny nachodziłyby na siebie.
        const int hourStep = plot.width() < 420 ? 6 : 3;
        for (int hour = 0; hour <= 24; hour += hourStep) {
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
}

void ChartWidget::drawCumulative(QPainter& p, const QRectF& plot) const {
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

void ChartWidget::drawDaily(QPainter& p, const QRectF& plot) const {
    if (subjects.isEmpty() || dayTotals.isEmpty()) return;

    const qint64 top = axisTop();
    const qreal dayWidth = plot.width() / 7;
    const qreal barWidth = std::min(dayWidth * 0.55, 48.0);

    // Góra każdego słupka, nad którą stoi liczba wykonanych zadań.
    QList<qreal> tops;
    p.save();
    p.setClipRect(plot);
    p.setPen(Qt::NoPen);
    for (int day = 0; day < dayTotals.size(); ++day) {
        const QList<const SubjectTotal*> order = subjectsByDayTime(day);
        const qreal left = plot.left() + dayWidth * (day + 0.5) - barWidth / 2;
        qreal bottom = plot.bottom();
        for (int i = 0; i < order.size(); ++i) {
            const qint64 value = dayTotals[day].value(order[i]->id);
            // Nawet bardzo krótka nauka ma być widoczna.
            const qreal height = std::max(plot.height() * qreal(value) / qreal(top), 2.0);
            const QRectF segment(left, bottom - height, barWidth, height);
            p.setBrush(order[i]->color);
            if (i == order.size() - 1) {
                // Zaokrąglona jest tylko góra całego słupka.
                p.drawRoundedRect(segment.adjusted(0, 0, 0, 4), 3, 3);
            } else {
                p.drawRect(segment);
            }
            bottom -= height;
        }
        tops.append(bottom);
    }
    p.restore();

    for (int day = 0; day < tops.size(); ++day) {
        drawTodoBadge(p, QPointF(plot.left() + dayWidth * (day + 0.5), tops[day] - 4),
                      doneTodosFor(-1, day).size(), kMuted);
    }
}

void ChartWidget::mousePressEvent(QMouseEvent* event) {
    // Na ekranie dotykowym nie ma najechania: szczegóły pokazuje dotknięcie.
    mouseMoveEvent(event);
}

void ChartWidget::mouseMoveEvent(QMouseEvent* event) {
    const QRectF plot = plotRect();
    const QPointF pos = event->position();
    QString text;
    qint64 newHover = -1;

    // Etykiety "+N" wysokich słupków wystają ponad wykres, a też mają pokazywać szczegóły.
    const QRectF hitArea = mode == Mode::Cumulative ? plot : plot.adjusted(0, -48, 0, 0);
    if (hitArea.contains(pos) && !subjects.isEmpty()) {
        if (mode == Mode::Totals) {
            const int index = std::min(int((pos.x() - plot.left()) / (plot.width() / subjects.size())),
                                       int(subjects.size()) - 1);
            const SubjectTotal& s = *barOrder()[index];
            text = QString("<b>%1</b><br>%2").arg(s.name.toHtmlEscaped(),
                                                  formatDuration(totals.value(s.id)))
                   + doneTodosHtml(doneTodosFor(s.id, -1));
        } else if (mode == Mode::Daily) {
            const int day = std::min(int((pos.x() - plot.left()) / (plot.width() / 7)), 6);
            if (day < dayTotals.size()) {
                qint64 daySum = 0;
                for (qint64 v : dayTotals[day])
                    daySum += v;
                text = "<b>" + QLocale(QLocale::English).toString(firstDate.addDays(day),
                                                                  "dddd, d MMM")
                       + " · " + formatDuration(daySum) + "</b>";
                // Ta sama kolejność co w słupku, tylko od góry.
                const QList<const SubjectTotal*> order = subjectsByDayTime(day);
                for (auto it = order.crbegin(); it != order.crend(); ++it) {
                    text += QString("<br><span style='color:%1'>■</span> %2: %3")
                                .arg((*it)->color.name(), (*it)->name.toHtmlEscaped(),
                                     formatDuration(dayTotals[day].value((*it)->id)));
                }
                text += doneTodosHtml(doneTodosFor(-1, day));
            }
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
    // Osobne okno dymka otwierane w trakcie dotyku wywraca Qt na Androidzie.
    if (inlineDetails) {
        detailsText = text;
        detailsPos = pos;
        update();
        return;
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
