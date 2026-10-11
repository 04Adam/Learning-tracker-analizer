// ChartWidget.h
#ifndef LEARNINGTIMEANALIZER_CHARTWIDGET_H
#define LEARNINGTIMEANALIZER_CHARTWIDGET_H

#include "Database.h"

#include <QDate>
#include <QHash>
#include <QWidget>

// Wykres czasu nauki: słupki z sumą na przedmiot, linie narastające w czasie
// albo małe słupki przedmiotów dla każdego dnia tygodnia.
class ChartWidget : public QWidget {
public:
    enum class Range { Today, Week };
    // Daily ma sens tylko dla tygodnia.
    enum class Mode { Totals, Cumulative, Daily };

    explicit ChartWidget(QWidget* parent = nullptr);

    // Nieprawidłowa data oznacza bieżący dzień albo tydzień.
    void setView(Range newRange, Mode newMode, QDate newDate = {});
    void reload();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;
    QString title() const;
    qint64 maxValue() const;
    qint64 axisStep() const;
    qint64 axisTop() const;
    qreal xForTime(qint64 t, const QRectF& plot) const;
    qint64 cumulativeAt(int subjectId, qint64 t) const;

    void drawLegend(QPainter& p) const;
    void drawValueAxis(QPainter& p, const QRectF& plot) const;
    void drawTotals(QPainter& p, const QRectF& plot) const;
    void drawTimeAxis(QPainter& p, const QRectF& plot) const;
    void drawCumulative(QPainter& p, const QRectF& plot) const;
    void drawDaily(QPainter& p, const QRectF& plot) const;

    Range range = Range::Today;
    Mode mode = Mode::Totals;
    QDate date;
    // Pierwszy dzień pokazywanego okresu.
    QDate firstDate;

    QList<SubjectTotal> subjects;
    QList<Session> sessions;
    QHash<int, qint64> totals;
    // Sumy na przedmiot osobno dla każdego dnia tygodnia; puste w widoku dnia.
    QList<QHash<int, qint64>> dayTotals;
    qint64 rangeStart = 0;
    qint64 rangeEnd = 1;
    // Linie kończą się na "teraz", a nie na końcu dnia/tygodnia.
    qint64 lineEnd = 0;
    qint64 hoverTime = -1;
};

#endif
