// ChartWidget.h
#ifndef LEARNINGTIMEANALIZER_CHARTWIDGET_H
#define LEARNINGTIMEANALIZER_CHARTWIDGET_H

#include "Database.h"

#include <QHash>
#include <QWidget>

// Wykres czasu nauki: słupki z sumą na przedmiot albo linie narastające w czasie.
class ChartWidget : public QWidget {
public:
    enum class Range { Today, Week };
    enum class Mode { Totals, Cumulative };

    explicit ChartWidget(QWidget* parent = nullptr);

    void setView(Range newRange, Mode newMode);
    void reload();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;
    QString title() const;
    qint64 axisStep() const;
    qint64 axisTop() const;
    qreal xForTime(qint64 t, const QRectF& plot) const;
    qint64 cumulativeAt(int subjectId, qint64 t) const;

    void drawLegend(QPainter& p) const;
    void drawValueAxis(QPainter& p, const QRectF& plot) const;
    void drawTotals(QPainter& p, const QRectF& plot) const;
    void drawCumulative(QPainter& p, const QRectF& plot) const;

    Range range = Range::Today;
    Mode mode = Mode::Totals;

    QList<SubjectTotal> subjects;
    QList<Session> sessions;
    QHash<int, qint64> totals;
    qint64 rangeStart = 0;
    qint64 rangeEnd = 1;
    // Linie kończą się na "teraz", a nie na końcu dnia/tygodnia.
    qint64 lineEnd = 0;
    qint64 hoverTime = -1;
};

#endif
