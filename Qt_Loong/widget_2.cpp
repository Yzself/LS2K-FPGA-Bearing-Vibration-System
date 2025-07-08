#include "widget_2.h"
#include "ui_widget_2.h"
#include <QDebug>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QChart>
#include <QVBoxLayout>
#include <QDateTime>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QGridLayout>
#include <QFontMetrics>
#include <algorithm>
// 使用 Qt Charts 命名空间
QT_CHARTS_USE_NAMESPACE

widget_2::widget_2(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::widget_2)
    , m_colorMapX(nullptr), m_colorScaleX(nullptr)
    , m_colorMapY(nullptr), m_colorScaleY(nullptr)
    , m_colorMapZ(nullptr), m_colorScaleZ(nullptr)
    , m_chartView(nullptr)
    , m_pieSeries(nullptr)
{
    ui->setupUi(this);
    setWindowTitle("MFCC 特征热力图");

    // * 初始化每个热力图
    setupHeatmapPlot(ui->mfccPlotX, m_colorMapX, m_colorScaleX, "X轴 MFCC 特征");
    setupHeatmapPlot(ui->mfccPlotY, m_colorMapY, m_colorScaleY, "Y轴 MFCC 特征");
    setupHeatmapPlot(ui->mfccPlotZ, m_colorMapZ, m_colorScaleZ, "Z轴 MFCC 特征");

    // * 饼图初始化
    setupPieChart();

    // * 时间序列图的初始化
    setupClassTimeChart();

    // * 节流逻辑: 每更新一次数据就进入冷却期，只有冷却期过了才能更新
    // 这样使得每两次更新之间起码有100毫秒的喘息时间。
    m_pieChartUpdateTimer = new QTimer(this);
    m_pieChartUpdateTimer->setSingleShot(true); // 设置为单次触发
    m_pieChartUpdateTimer->setInterval(50);    // 设置50毫秒延迟
    connect(m_pieChartUpdateTimer, &QTimer::timeout, this, [this]() {
        m_isPieChartThrottled = false;
    });
}

widget_2::~widget_2()
{
    delete ui;
}

void widget_2::setupHeatmapPlot(QCustomPlot* customPlot, QCPColorMap* &colorMap, QCPColorScale* &colorScale, const QString& title)
{
    if (!customPlot) return;

    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    customPlot->axisRect()->setupFullAxesBox(true);

    //* 调整轴外观
    QFont axisLabelFont("Arial", 10, QFont::Bold);
    QColor axisLabelColor(80, 80, 80); // 深灰色
    QFont tickLabelFont("Arial", 9);
    QColor tickLabelColor(120, 120, 120); // 中灰色
    QPen axisPen(QColor(180, 180, 180), 1); // 浅灰色轴线
    QPen tickPen(QColor(200, 200, 200), 1); // 更浅的刻度线

    customPlot->xAxis->setLabel("帧索引 (0-8)");
    customPlot->xAxis->setLabelFont(axisLabelFont);
    customPlot->xAxis->setLabelColor(axisLabelColor);
    customPlot->xAxis->setTickLabelFont(tickLabelFont);
    customPlot->xAxis->setTickLabelColor(tickLabelColor);
    customPlot->xAxis->setBasePen(axisPen);
    customPlot->xAxis->setTickPen(tickPen);
    customPlot->xAxis->setSubTickPen(Qt::NoPen); // 隐藏子刻度线以简化

    customPlot->yAxis->setLabel("MFCC 系数索引 (0-12)");
    customPlot->yAxis->setLabelFont(axisLabelFont);
    customPlot->yAxis->setLabelColor(axisLabelColor);
    customPlot->yAxis->setTickLabelFont(tickLabelFont);
    customPlot->yAxis->setTickLabelColor(tickLabelColor);
    customPlot->yAxis->setBasePen(axisPen);
    customPlot->yAxis->setTickPen(tickPen);
    customPlot->yAxis->setSubTickPen(Qt::NoPen);

    // * 轴矩形背景 (使其与图表背景融合)
    customPlot->axisRect()->setBackground(Qt::NoBrush); // 透明背景
    customPlot->axisRect()->setAntialiased(true); // 抗锯齿
    // customPlot->axisRect()->setBasePen(Qt::NoPen); // 不要轴矩形的外框

    if (customPlot->plotLayout()->rowCount() == 1 || !customPlot->plotLayout()->hasElement(0,0) ) {
        customPlot->plotLayout()->insertRow(0); // 插入行用于标题
        QCPTextElement *titleElement = new QCPTextElement(customPlot, title, QFont("sans", 12, QFont::Bold));
        customPlot->plotLayout()->addElement(0, 0, titleElement);
    }


    colorMap = new QCPColorMap(customPlot->xAxis, customPlot->yAxis);

    colorScale = new QCPColorScale(customPlot);
    // * 调整颜色条外观
    colorScale->axis()->setLabel("幅值");
    colorScale->axis()->setLabelFont(QFont("sans", 10, QFont::Bold));
    colorScale->axis()->setLabelColor(Qt::darkGray);
    colorScale->axis()->setTickLabelFont(QFont("sans", 9));
    colorScale->axis()->setTickLabelColor(Qt::gray);
    colorScale->axis()->setBasePen(QPen(Qt::gray));
    colorScale->axis()->setTickPen(QPen(Qt::gray));

    // * 调整颜色条的宽度 (如果颜色条在右侧，这是宽度；如果在顶部/底部，这是高度)
    colorScale->setBarWidth(15); // 设置为15像素宽

    // * 在图表右侧添加颜色条 (元素索引(1,1) 可能需要根据实际布局调整，通常是 (0,1) 或 (1,1))
    // * 检查是否已有元素，避免重复添加
    bool scaleExists = false;
    for (int i = 0; i < customPlot->plotLayout()->elementCount(); ++i) {
        if (dynamic_cast<QCPColorScale*>(customPlot->plotLayout()->elementAt(i))) {
            scaleExists = true;
            break;
        }
    }
    if (!scaleExists) {
        // * 主绘图区域是 (1,0) (在标题之下)，颜色条放在 (1,1)
        if(customPlot->plotLayout()->rowCount() > 1 && customPlot->plotLayout()->columnCount() > 0){
            customPlot->plotLayout()->addElement(1, 1, colorScale); // (row, col)
        } else {
            // * 备用方案，添加到主布局
            customPlot->plotLayout()->addElement(0,1,colorScale); // 如果只有一行，尝试 (0,1)
        }
    }


    colorScale->setType(QCPAxis::atRight);
    colorMap->setColorScale(colorScale);
    colorScale->axis()->setLabel("幅值");

    // QCPColorGradient customGradient;
    // customGradient.clearColorStops();
    // customGradient.setColorStopAt(0.0, QColor(0, 0, 100));    // 深蓝
    // customGradient.setColorStopAt(0.25, QColor(0, 150, 150)); // 青色
    // customGradient.setColorStopAt(0.5, Qt::yellow);         // 黄色
    // customGradient.setColorStopAt(0.75, QColor(255,100,0));  // 橙色
    // customGradient.setColorStopAt(1.0, QColor(150, 0, 0));    // 深红
    // customGradient.setPeriodic(false); // 非周期性
    // colorMap->setGradient(customGradient);
    colorMap->setGradient(QCPColorGradient::gpThermal); // 默认的热成像
    // colorMap->setGradient(QCPColorGradient::gpSpectrum); // 彩虹色，慎用，有时可读性不高
    // colorMap->setGradient(QCPColorGradient::gpJet);     // 科学可视化常用
    // colorMap->setGradient(QCPColorGradient::gpPolar);    // 蓝-白-红，适合表示正负差异
    colorMap->setInterpolate(true);

    // * 初始数据维度 (可以先设为0x0，接收到数据时再调整)
    // 帧有 9 个 (对应x轴)，MFCC 系数有 13 个 (对应y轴)
    int numFrames = 9;          // X轴 (key)
    int numCoefficients = 13;   // Y轴 (value)
    colorMap->data()->setSize(numFrames, numCoefficients); // (key=帧, value=MFCC系数)
    // * 设置轴的初始范围，注意索引从0开始
    colorMap->data()->setKeyRange(QCPRange(0, numFrames -1 ));    // X轴范围是帧 0 到 8
    colorMap->data()->setValueRange(QCPRange(0, numCoefficients -1)); // Y轴范围是系数 0 到 12

    customPlot->rescaleAxes(); // 根据数据和标签重新计算轴范围和布局
    customPlot->replot();
}

void widget_2::displayMfccFeatures(const QVector<QVector<QVector<double>>>& allAxesMfccData)
{
    if (allAxesMfccData.size() != 3) {
        qWarning() << "MFCC数据轴数不为3:" << allAxesMfccData.size();
        // * 清除所有热力图
        if(m_colorMapX) m_colorMapX->data()->clear();
        if(m_colorMapY) m_colorMapY->data()->clear();
        if(m_colorMapZ) m_colorMapZ->data()->clear();
        if(ui->mfccPlotX) ui->mfccPlotX->replot();
        if(ui->mfccPlotY) ui->mfccPlotY->replot();
        if(ui->mfccPlotZ) ui->mfccPlotZ->replot();
        return;
    }

    QCustomPlot* plots[] = {ui->mfccPlotX, ui->mfccPlotY, ui->mfccPlotZ};
    QCPColorMap* colorMaps[] = {m_colorMapX, m_colorMapY, m_colorMapZ};

    for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
        const auto& singleAxisData = allAxesMfccData[axisIdx]; // 这是 QVector<QVector<double>> (帧数 x 系数)

        if (singleAxisData.isEmpty() || singleAxisData[0].isEmpty()) {
            qWarning() << "第" << axisIdx << "轴的MFCC数据为空";
            if(colorMaps[axisIdx]) {
                colorMaps[axisIdx]->data()->clear(); // 清空数据
                colorMaps[axisIdx]->data()->setSize(1,1); // 设为最小尺寸避免错误
                colorMaps[axisIdx]->data()->setCell(0,0,0); // 填充一个虚拟值
                colorMaps[axisIdx]->setDataRange(QCPRange(0,0)); // 重置数据范围
            }
            if(plots[axisIdx]) {
                plots[axisIdx]->replot();
            }
            continue;
        }

        int numFrames = singleAxisData.size();          // 例如 9 帧 (这将是X轴的维度)
        int numCoefficients = singleAxisData[0].size(); // 例如 13 个MFCC系数 (这将是Y轴的维度)

        if (!plots[axisIdx] || !colorMaps[axisIdx]) {
            qWarning() << "Plot or ColorMap for axis" << axisIdx << "is null.";
            continue;
        }

        QCPColorMapData *mapData = colorMaps[axisIdx]->data();

        // * 调整 setSize 和 Range
        // X轴是帧 (key), Y轴是MFCC系数 (value)
        mapData->setSize(numFrames, numCoefficients);
        mapData->setKeyRange(QCPRange(0, numFrames));   // X轴范围：帧索引从 0 到 numFrames-1 (QCPRange 的上限是开区间，所以用 numFrames)
            // 或者用 QCPRange(0, numFrames -1) 如果QCustomPlot处理的是闭区间索引
            // 查阅文档：QCPColorMapData::setKeyRange takes a QCPRange.
            // For a map of size nx*ny, the key coordinates run from keyAxis->pixelToCoord(0)
            // to keyAxis->pixelToCoord(nx-1).
            // 因此，如果setSize(nx, ny)，则key的逻辑范围是0 to nx-1, value是0 to ny-1
        mapData->setKeyRange(QCPRange(0, numFrames -1));    // 对应 setSize 的第一个参数
        mapData->setValueRange(QCPRange(0, numCoefficients -1)); // 对应 setSize 的第二个参数


        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::lowest();

        // * 调整 setCell 的索引顺序
        for (int frame = 0; frame < numFrames; ++frame) { // 遍历帧 (X轴)
            if (singleAxisData[frame].size() != numCoefficients) {
                qWarning() << "数据维度不一致: 轴" << axisIdx << ", 帧" << frame
                           << "期望" << numCoefficients << "个系数, 实际" << singleAxisData[frame].size();
                continue; // 跳过此帧或进行错误处理
            }
            for (int coeff = 0; coeff < numCoefficients; ++coeff) { // 遍历MFCC系数 (Y轴)
                double val = singleAxisData[frame][coeff];
                // setCell(keyIndex, valueIndex, zValue)
                // keyIndex (X轴) 是 frame
                // valueIndex (Y轴) 是 coeff
                mapData->setCell(frame, coeff, val);
                if (val < minVal) minVal = val;
                if (val > maxVal) maxVal = val;
            }
        }

        // * 设置颜色映射的范围
        if (minVal <= maxVal) { // 等于的情况也需要设置
            colorMaps[axisIdx]->setDataRange(QCPRange(minVal, maxVal));
        } else { // 通常发生在没有有效数据或只有一个数据点时
            colorMaps[axisIdx]->setDataRange(QCPRange(0,1)); // 设置一个默认的小范围
            qWarning() << "MinVal > MaxVal for axis" << axisIdx << "after processing data. min:" << minVal << "max:" << maxVal;
        }

        // * 重新缩放轴以适应新的数据范围和标签 (QCustomPlot 1.x QCPColorMap 会自动处理一些，但显式调用更好)
        plots[axisIdx]->xAxis->setRange(0, numFrames > 1 ? numFrames -1 : 1);         // X轴显示从0到最后一帧
        plots[axisIdx]->yAxis->setRange(0, numCoefficients > 1 ? numCoefficients -1 : 1); // Y轴显示从0到最后一个系数
        // * 如果只有一个数据点，范围设为1避免0宽度
        plots[axisIdx]->replot();
    }
}


void widget_2::setupPieChart()
{
    // 1. 创建饼图数据系列 (Series)
    // 使用 QPieSeries 来存储和管理饼图的各个切片
    m_pieSeries = new QPieSeries();
    // 设置中心孔的大小，0.0 为实心饼图, > 0.0 为环形图。0.35 是一个不错的美学选择。
    m_pieSeries->setHoleSize(0.35);

    // 2. 创建图表对象 (Chart)
    // QChart 是所有图表的“画布”，负责管理数据系列、坐标轴、标题、图例等。
    QChart *chart = new QChart();
    // 将数据系列添加到图表中。一个图表可以包含多个系列。
    chart->addSeries(m_pieSeries);
    // 启用动画效果，当数据变化时，图表会有平滑的过渡动画。
    // chart->setAnimationOptions(QChart::SeriesAnimations);

    // 3. 优化空间布局，移除标题和边距
    // [修改] 不再设置标题以节省空间。
    // chart->setTitle("模型预测概率分布");

    // [优化] 将图表的外边距和布局内边距都设置为0，让图表内容填满整个视图。
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    // [优化] 设置图表背景为透明，使其与窗口背景融为一体。
    chart->setBackgroundVisible(false);

    // 4. 配置图例 (Legend)
    // 图例是解释图表中各个系列或切片含义的说明框。
    QLegend *legend = chart->legend();
    legend->setVisible(true);                  // 确保图例是可见的
    legend->setAlignment(Qt::AlignRight);      // 将图例放置在图表的右侧
    legend->setFont(QFont("Arial", 9));        // 设置图例文字的字体和大小
    legend->setBackgroundVisible(false);       // 使图例背景透明

    // 5. 创建图表视图 (ChartView)
    // QChartView 是一个 QWidget，专门用于在UI上显示 QChart 对象。
    m_chartView = new QChartView(chart);
    // 启用抗锯齿渲染，使图表边缘和线条更加平滑美观。
    m_chartView->setRenderHint(QPainter::Antialiasing);

    // 6. 将图表视图嵌入到UI布局中
    // 我们在Qt Designer中放置了一个名为 pieChartPlaceholder 的 QWidget 作为容器。
    // 创建一个垂直布局(QVBoxLayout)来管理这个容器的内容。
    QVBoxLayout *layout = new QVBoxLayout(ui->pieChartPlaceholder);
    // 设置布局的边距为0，确保图表视图能完全填充占位符。
    layout->setContentsMargins(0, 0, 0, 0);
    // 将我们创建的图表视图添加到布局中。
    layout->addWidget(m_chartView);
    // 将此布局设置为占位符的布局。这一步是可选的，因为在创建布局时已经指定了父对象。
    // ui->pieChartPlaceholder->setLayout(layout);
}

/**
 * @brief 更新饼图数据。此函数是公开的槽，可以从外部连接信号。
 * @param probabilities 一个 QMap，键是类别名称(QString)，值是对应的概率(double)
 */
// 方案一：鲜艳 & 清晰的调色板 (高对比度，适合快速区分)
static const QVector<QColor> g_pieSliceColors = {
    // 对应 0.7i, 0.7o
    QColor("#3366cc"), QColor("#0099c6"),
    // 对应 0.9i, 0.9o
    QColor("#22aa99"), QColor("#aaaa11"),
    // 对应 1.1i, 1.1o
    QColor("#ff9900"), QColor("#66aa00"),
    // 对应 1.3i, 1.3o
    QColor("#ff9900"), QColor("#dd4477"), // 橙色/粉色系
    // 对应 1.5i, 1.5o
    QColor("#dc3912"), QColor("#b82e2e"), // 红色系
    // 对应 1.7i, 1.7o
    QColor("#990099"), QColor("#994499"), // 紫色/深红色系 (表示更危险)
    // 对应 healthy -> 绿色
    QColor("#109618")
};

/**
 * @brief (节流阀) 接收概率数据，缓存它并触发一个延迟更新。
 */
void widget_2::updatePieChart(const QMap<QString, double>& probabilities)
{
    // 1. 创建一个新的QMap，用于存放键被缩短后的结果
    QMap<QString, double> shortenedProbabilities;

    // 2. 遍历原始的probabilities，对每个键进行处理
    for (auto it = probabilities.constBegin(); it != probabilities.constEnd(); ++it) {
        QString originalKey = it.key();
        double value = it.value();

        // 复制一份原始键，以便进行修改
        QString shortenedKey = originalKey;

        // --- 在这里应用缩短规则 ---
        if (shortenedKey.contains("healthy")||shortenedKey.contains("Healthy")) {
            shortenedKey = "Heal";
        } else {
            // 替换规则要明确，避免意外替换
            shortenedKey.replace("inner", "i");
            shortenedKey.replace("outer", "o");
            // 移除可能存在的其他部分
            shortenedKey.remove(" without pulley");
        }

        // 将处理后的键和原始的值放入新的Map中
        shortenedProbabilities.insert(shortenedKey, value);
    }

    // --- 键缩短结束 ---


    // 3. 将【处理后】的 shortenedProbabilities 缓存起来
    m_latestProbabilities = shortenedProbabilities;


    // 2. 检查节流标志
    if (m_isPieChartThrottled) {
        // 如果正在“冷却”中，则什么都不做，直接返回
        return;
    }

    // 3. 如果没有在冷却，则立即执行更新
    m_isPieChartThrottled = true; // 将状态设为“正在冷却”
    performDelayedPieChartUpdate(); // 立即更新UI
    m_pieChartUpdateTimer->start(100); // 启动100毫秒的“冷却”计时
}

/**
 * @brief (实际更新) 定时器触发后，用缓存的最新数据更新饼图UI。
 */
void widget_2::performDelayedPieChartUpdate()
{
    if (!m_pieSeries) {
        return;
    }

    // 为了简单和稳定，我们使用 clear + append 的方式
    m_pieSeries->clear();

    if (m_latestProbabilities.isEmpty()) {
        return;
    }

    // 使用缓存的 m_latestProbabilities 来填充饼图
    for (auto it = m_latestProbabilities.constBegin(); it != m_latestProbabilities.constEnd(); ++it) {
        m_pieSeries->append(it.key(), it.value());
    }

    // 调用独立的样式函数来美化饼图
    applyPieChartStyles();
}


/**
 * @brief (样式应用) 为饼图的切片设置颜色、高亮等视觉样式。
 */
void widget_2::applyPieChartStyles()
{
    if (!m_pieSeries) return;

    // 1. 找到概率最高的切片
    double maxProbability = 0;
    QPieSlice *maxSlice = nullptr;
    for (QPieSlice *slice : m_pieSeries->slices()) {
        if (slice->value() > maxProbability) {
            maxProbability = slice->value();
            maxSlice = slice;
        }
    }

    // 2. 遍历所有切片，应用颜色和样式
    int colorIndex = 0;

    for (QPieSlice *slice : m_pieSeries->slices()) {
        // a. 设置标签
        slice->setLabel(QString("%1\n%2%").arg(slice->label()).arg(slice->value(), 0, 'f', 2));

        // b. 从调色板设置颜色
        slice->setColor(g_pieSliceColors[colorIndex % g_pieSliceColors.size()]);

        // c. 高亮最大值的切片
        if (slice == maxSlice) {
            slice->setExploded(true);
            slice->setLabelFont(QFont("Arial", 9, QFont::Bold));
            slice->setPen(QPen(Qt::black, 2));
        } else {
            slice->setExploded(false);
            slice->setPen(QPen(Qt::white, 1));
        }

        // d. 隐藏小切片的标签
        if (slice->value() < 2.0) {
            slice->setLabelVisible(false);
        } else {
            slice->setLabelVisible(true);
        }

        colorIndex++;
    }

    // e. 统一设置标签线
    m_pieSeries->setLabelsPosition(QPieSlice::LabelOutside);
    for(QPieSlice *slice : m_pieSeries->slices()) {
        slice->setLabelArmLengthFactor(0.2);
        slice->setLabelColor(Qt::darkGray);
    }
}

/**
 * @brief (滚动焦点Y轴版) 初始化时间序列图。
 *        Y轴被设置为空，等待addClassTimeData动态填充。
 */
void widget_2::setupClassTimeChart()
{
    // 1. ================== 创建数据系列和图表核心 ==================
    m_classTimeSeries = new QLineSeries();
    m_classTimeSeries->setName("CategoryTimeline");
    m_classTimeSeries->setPen(QPen(QColor(0, 120, 215), 2));
    m_classTimeSeries->setPointsVisible(true);

    QChart *chart = new QChart();
    chart->addSeries(m_classTimeSeries);
    chart->setTitle("");
    chart->legend()->hide();
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    chart->setBackgroundVisible(false);

    // 2. ================== 配置X轴 (时间轴) ==================
    m_timeAxis = new QDateTimeAxis;
    m_timeAxis->setTickCount(8);
    m_timeAxis->setFormat("hh:mm:ss");
    m_timeAxis->setTitleText("");
    m_timeAxis->setLabelsFont(QFont("Arial", 7));
    chart->addAxis(m_timeAxis, Qt::AlignBottom);
    m_classTimeSeries->attachAxis(m_timeAxis);

    // 3. ================== 配置Y轴 (类别轴) - 只做基本设置 ==================
    m_categoryAxis = new QCategoryAxis;
    m_categoryAxis->setTitleText("");
    m_categoryAxis->setLabelsFont(QFont("Arial", 7));
    // Y轴的标签和范围将由 updateVisibleYAxis 动态管理
    chart->addAxis(m_categoryAxis, Qt::AlignLeft);
    m_classTimeSeries->attachAxis(m_categoryAxis);

    // --- 将所有可能的类别标签存储到成员变量中备用 ---
    const QStringList localUiDisplayCategories = {
        "Heal", "0.7i", "0.7o", "0.9i", "0.9o", "1.1i", "1.1o",
        "1.3i", "1.3o", "1.5i", "1.5o", "1.7i", "1.7o"
    };
    m_allCategoryLabels = localUiDisplayCategories;

    // 4. ================== 构建索引映射表 ==================
    const QStringList pythonClassNames = {
        "0.7inner", "0.7outer", "0.9inner", "0.9outer", "1.1inner",
        "1.1outer", "1.3inner", "1.3outer", "1.5inner", "1.5outer",
        "1.7inner", "1.7outer", "healthy"
    };
    m_pythonToUiIndexMap.resize(pythonClassNames.size());
    for (int pythonIdx = 0; pythonIdx < pythonClassNames.size(); ++pythonIdx) {
        const QString& pythonName = pythonClassNames[pythonIdx];
        int uiIdx = -1;
        if (pythonName.contains("healthy")) {
            uiIdx = 0;
        } else {
            QString simplifiedName = pythonName;
            simplifiedName.replace("inner", "i");
            simplifiedName.replace("outer", "o");
            uiIdx = m_allCategoryLabels.indexOf(simplifiedName); // 从m_allCategoryLabels查找
        }
        if (uiIdx != -1) { m_pythonToUiIndexMap[pythonIdx] = uiIdx; }
        else { qWarning() << "Map failed for" << pythonName; m_pythonToUiIndexMap[pythonIdx] = -1; }
    }

    // 5. ================== 创建视图并嵌入UI ==================
    m_classTimeChartView = new QChartView(chart);
    m_classTimeChartView->setRenderHint(QPainter::Antialiasing);
    QVBoxLayout *layout = new QVBoxLayout(ui->ClassTimeWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_classTimeChartView);
}


/**
 * @brief (辅助函数) 根据中心索引和半径，动态更新Y轴上可见的标签和范围。
 * @param centerIndex 最新的预测类别索引，作为显示的中心。
 * @param radius 在中心上下各显示多少个类别。
 */
void widget_2::updateVisibleYAxis(int centerIndex, int radius)
{
    if (!m_categoryAxis || m_allCategoryLabels.isEmpty()) {
        return;
    }

    // 1. 确定要【显示】的标签的最小和最大索引
    int displayMinIndex = qMax(0, centerIndex - radius);
    int displayMaxIndex = qMin(m_allCategoryLabels.size() - 1, centerIndex + radius);

    // --- 边界情况修正：确保总能显示 `2*radius+1` 个标签 (如果可能) ---
    // 如果焦点靠近底部
    if (centerIndex < radius) {
        displayMaxIndex = qMin(m_allCategoryLabels.size() - 1, 2 * radius);
    }
    // 如果焦点靠近顶部
    else if (centerIndex > (m_allCategoryLabels.size() - 1 - radius)) {
        displayMinIndex = qMax(0, m_allCategoryLabels.size() - 1 - (2 * radius));
    }


    // 2. 清空Y轴
    const auto currentCategories = m_categoryAxis->categoriesLabels();
    for (const QString &label : currentCategories) {
        m_categoryAxis->remove(label);
    }

    // 3. 只添加在【要显示的】范围内的类别标签
    for (int i = displayMinIndex; i <= displayMaxIndex; ++i) {
        m_categoryAxis->append(m_allCategoryLabels[i], i);
    }

    // 4. 设置Y轴的【绘图范围】，要比标签范围稍大一点
    //    这里的 -0.5 和 +0.5 是关键，它在标签两侧留出了半个单位的边距。
    //    这会“欺骗”QChart，让它认为边缘标签并不在“最边缘”，从而强制绘制它们。
    m_categoryAxis->setRange(displayMinIndex - 0.3, displayMaxIndex + 0.3);
}


/**
 * @brief (最终版 - 滚动焦点) 添加新数据点，并更新Y轴只显示最新结果附近的几个类别。
 * @param className 预测的类别名称（当前未使用）
 * @param pythonClassIndex 从Python传来的原始类别索引
 */
void widget_2::addClassTimeData(const QString& className, int pythonClassIndex)
{
    [[maybe_unused]] const QString& localClassName = className;

    if (!m_classTimeSeries || !m_timeAxis || !m_categoryAxis) {
        qWarning() << "Class-Time chart is not properly initialized.";
        return;
    }

    // --- 1. 使用映射表转换索引 ---
    if (pythonClassIndex < 0 || pythonClassIndex >= m_pythonToUiIndexMap.size()) {
        qWarning() << "Received invalid python class index:" << pythonClassIndex;
        return;
    }
    const int uiClassIndex = m_pythonToUiIndexMap[pythonClassIndex];
    if (uiClassIndex == -1) {
        qWarning() << "Mapping for python index" << pythonClassIndex << "not found.";
        return;
    }

    // --- 2. 添加新数据点 ---
    const QDateTime currentTime = QDateTime::currentDateTime();
    const qint64 currentTimeMs = currentTime.toMSecsSinceEpoch();
    m_classTimeSeries->append(currentTimeMs, uiClassIndex);

    // --- 3. 设置滚动时间窗口并清理旧数据 ---
    const int SCROLLING_WINDOW_SECONDS = 10;
    QDateTime startTime = currentTime.addSecs(-SCROLLING_WINDOW_SECONDS);
    qint64 startTimeMs = startTime.toMSecsSinceEpoch();

    m_timeAxis->setRange(startTime, currentTime);

    // 从序列的开头开始移除，直到点的时间戳在窗口内
    while (m_classTimeSeries->count() > 0 &&
           m_classTimeSeries->points().first().x() < startTimeMs)
    {
        m_classTimeSeries->remove(0);
    }

    // --- 4. 更新Y轴显示 ---
    const int yAxisRadius = 2; // [可调] 在最新结果的上下各显示 yAxisRadius 个类别
    updateVisibleYAxis(uiClassIndex, yAxisRadius);
}

void widget_2::setStateLabel(QString State)
{
    ui->StateLabel2->setText(State);
}
void widget_2::on_BackButton_clicked()
{
    // this->hide(); // 隐藏自己
    emit backToMainRequested(); // 发射信号
}



