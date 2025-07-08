#ifndef WIDGET_2_H
#define WIDGET_2_H

#include <QWidget>
#include "qcustomplot.h"
#include <QVector>
#include <QMap>
#include <QList>
#include <QTimer>
namespace Ui {
class widget_2;
}

QT_BEGIN_NAMESPACE
namespace QtCharts {
    class QChartView;
    class QPieSeries;
    class QLineSeries; // 用于新的时间序列图
    class QDateTimeAxis; // 时间轴
    class QValueAxis; // 数值轴
    class QCategoryAxis; // 类别轴
}
QT_END_NAMESPACE

class widget_2 : public QWidget
{
    Q_OBJECT

public:
    explicit widget_2(QWidget *parent = nullptr);
    ~widget_2();
public slots:
    void displayMfccFeatures(const QVector<QVector<QVector<double>>>& allAxesMfccData);
    void updatePieChart(const QMap<QString, double>& probabilities);
    void addClassTimeData(const QString& className, int classIndex);
    void setStateLabel(QString state);
private slots:
    void on_BackButton_clicked();
    void performDelayedPieChartUpdate();
private:
    Ui::widget_2 *ui;

    // 为每个QCustomPlot准备ColorMap和ColorScale
    QCPColorMap *m_colorMapX;
    QCPColorScale *m_colorScaleX;
    QCPColorMap *m_colorMapY;
    QCPColorScale *m_colorScaleY;
    QCPColorMap *m_colorMapZ;
    QCPColorScale *m_colorScaleZ;

    QtCharts::QChartView *m_chartView; // 用于显示图表的视图
    QtCharts::QPieSeries *m_pieSeries; // 用于存储饼图数据的系列
    // 新增节流相关的成员变量
    QTimer *m_pieChartUpdateTimer;
    QMap<QString, double> m_latestProbabilities;
    bool m_isPieChartThrottled; // 冷却标志位

    // --- 新增：时间序列图相关成员 ---
    QtCharts::QChartView *m_classTimeChartView; // 时间图的视图
    QtCharts::QLineSeries *m_classTimeSeries;    // 时间图的数据系列
    QtCharts::QDateTimeAxis *m_timeAxis;         // X轴 (时间)
    QtCharts::QCategoryAxis *m_categoryAxis;     // Y轴 (类别)
    QList<int> m_recentClassIndices;            // 用于追踪最近类别
    QVector<int> m_pythonToUiIndexMap;         // 用于存储原始索引到新UI索引的映射
    QStringList m_allCategoryLabels; // 我们仍然需要这个列表
    void updateVisibleYAxis(int centerIndex, int radius);

    void setupHeatmapPlot(QCustomPlot* customPlot, QCPColorMap* &colorMap, QCPColorScale* &colorScale, const QString& title);
    void setupPieChart();
    void applyPieChartStyles();
    void setupClassTimeChart();
signals:
    void backToMainRequested(); // 信号：请求返回主界面
};

#endif // WIDGET_2_H
