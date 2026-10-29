// ChartWidget.h
#ifndef LEARNINGTIMEANALIZER_CHARTWIDGET_H
#define LEARNINGTIMEANALIZER_CHARTWIDGET_H

#include "Database.h"

#include <QDate>
#include <QHash>
#include <QWidget>

// Study time chart: bars with the total per subject, lines accumulating over time,
// or a stacked bar of subjects for each day of the week.
class ChartWidget : public QWidget {
public:
    enum class Range { Today, Week };
    // Daily only makes sense for a week.
    enum class Mode { Totals, Cumulative, Daily };

    explicit ChartWidget(QWidget* parent = nullptr);

    // An invalid date means the current day or week.
    void setView(Range newRange, Mode newMode, QDate newDate = {});
    // -1 shows all subjects.
    void setSubject(int subjectId);
    // Details under the finger drawn on the chart itself instead of in a separate tooltip.
    void setInlineDetails(bool enabled);
    void reload();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;
    QList<QPointF> legendPositions() const;
    QString title() const;
    // Row below the title; takes up space only in the week view.
    qreal summaryHeight() const;
    void drawWeekSummary(QPainter& p) const;
    // Order of subject bars: in a week, longest study first.
    QList<const SubjectTotal*> barOrder() const;
    qint64 maxValue() const;
    qint64 axisStep() const;
    qint64 axisTop() const;
    qreal xForTime(qint64 t, const QRectF& plot) const;
    qint64 cumulativeAt(int subjectId, qint64 t) const;
    QList<const SubjectTotal*> subjectsByDayTime(int day) const;
    // Tasks done while studying a subject (-1: all) on a given weekday (-1: the whole period).
    QList<const DoneTodo*> doneTodosFor(int subjectId, int day) const;
    QString doneTodosHtml(const QList<const DoneTodo*>& todos) const;
    // White "+N" in the middle of the bar, or above it (bottom at aboveY) when it does not fit.
    void drawTodoBadge(QPainter& p, const QRectF& bar, qreal aboveY, int count) const;

    void drawLegend(QPainter& p) const;
    void drawValueAxis(QPainter& p, const QRectF& plot) const;
    void drawTotals(QPainter& p, const QRectF& plot) const;
    void drawTimeAxis(QPainter& p, const QRectF& plot) const;
    void drawCumulative(QPainter& p, const QRectF& plot) const;
    void drawDaily(QPainter& p, const QRectF& plot) const;
    void drawDetails(QPainter& p) const;

    Range range = Range::Today;
    Mode mode = Mode::Totals;
    QDate date;
    // First day of the displayed period.
    QDate firstDate;
    // The only displayed subject; -1 when all are displayed.
    int subjectFilter = -1;

    QList<SubjectTotal> subjects;
    QList<Session> sessions;
    QHash<int, qint64> totals;
    // Per-subject totals for each day of the week; empty in the day view.
    QList<QHash<int, qint64>> dayTotals;
    QList<DoneTodo> doneTodos;
    qint64 rangeStart = 0;
    qint64 rangeEnd = 1;
    // Lines end at "now", not at the end of the day/week.
    qint64 lineEnd = 0;
    qint64 hoverTime = -1;
    bool inlineDetails = false;
    // Content and position of the details drawn on the chart; empty when nothing is shown.
    QString detailsText;
    QPointF detailsPos;
};

#endif
