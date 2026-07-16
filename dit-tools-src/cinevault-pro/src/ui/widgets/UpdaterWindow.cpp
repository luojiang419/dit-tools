#include "ui/widgets/UpdaterWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QEasingCurve>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace {
constexpr auto kNeutralAccent = "#c5cbd3";
constexpr auto kSuccessAccent = "#69b783";
constexpr auto kErrorAccent = "#e06464";

QString chipStyle(bool active, const QString &accent)
{
    return active
        ? QStringLiteral(
              "QLabel { color: #e8eaed; background: %1; border: 1px solid %2; "
              "border-radius: 8px; padding: 8px 12px; font-size: 12px; font-weight: 700; }")
              .arg(accent + QStringLiteral("1f"), accent + QStringLiteral("52"))
        : QStringLiteral(
              "QLabel { color: #9aa1aa; background: #20242a; border: 1px solid #30343a; "
              "border-radius: 8px; padding: 8px 12px; font-size: 12px; font-weight: 500; }");
}
}

UpdaterWindow::UpdaterWindow(const UpdaterInstallSession &session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_runner(new UpdaterSessionRunner(this))
{
    setWindowTitle(QStringLiteral("影资管家正在更新"));
    setWindowIcon(QApplication::windowIcon());
    resize(760, 610);
    setMinimumSize(640, 560);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(QStringLiteral(
        "QWidget { background: #101418; color: #ffffff; font-family: 'Microsoft YaHei UI'; }"
        "QFrame#panel { background: #171b20; border: 1px solid #30343a; border-radius: 10px; }"
        "QProgressBar { background: #20242a; border: 1px solid #30343a; border-radius: 4px; height: 8px; }"
        "QProgressBar::chunk { background: #c5cbd3; border-radius: 3px; }"
        "QPlainTextEdit { background: #11151a; color: #b8c0ca; border: 1px solid #30343a; "
        "border-radius: 6px; padding: 8px; font-family: 'Consolas', 'Microsoft YaHei UI'; font-size: 12px; }"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(24, 24, 24, 24);
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("panel"));
    outerLayout->addWidget(panel);

    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(28, 28, 28, 28);
    panelLayout->setSpacing(22);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(16);
    m_iconLabel = new QLabel(QStringLiteral("↻"), panel);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(52, 52);
    m_iconLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #c5cbd3; background: #242a30; border: 1px solid #444b54; "
        "border-radius: 8px; font-size: 30px; font-weight: 700; }"));
    headerLayout->addWidget(m_iconLabel);

    auto *titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(4);
    m_titleLabel = new QLabel(QStringLiteral("影资管家正在更新"), panel);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 800;"));
    titleLayout->addWidget(m_titleLabel);
    auto *versionLabel = new QLabel(QStringLiteral("目标版本 %1").arg(session.versionTag), panel);
    versionLabel->setStyleSheet(QStringLiteral("color: #9aa1aa; font-size: 13px;"));
    titleLayout->addWidget(versionLabel);
    headerLayout->addLayout(titleLayout, 1);
    panelLayout->addLayout(headerLayout);

    auto *stepsLayout = new QHBoxLayout;
    stepsLayout->setSpacing(10);
    const QStringList steps{
        QStringLiteral("准备安装"),
        QStringLiteral("关闭旧版本"),
        QStringLiteral("安装新版本"),
        QStringLiteral("启动新版本"),
        QStringLiteral("完成")
    };
    for (int index = 0; index < steps.size(); ++index) {
        auto *chip = createStepChip(index, steps.at(index));
        m_stepChips.append(chip);
        stepsLayout->addWidget(chip);
    }
    stepsLayout->addStretch(1);
    panelLayout->addLayout(stepsLayout);

    auto *messageLayout = new QVBoxLayout;
    messageLayout->setSpacing(8);
    m_messageLabel = new QLabel(QStringLiteral("正在准备独立更新器..."), panel);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    messageLayout->addWidget(m_messageLabel);
    m_substepLabel = new QLabel(panel);
    m_substepLabel->setWordWrap(true);
    m_substepLabel->setStyleSheet(QStringLiteral("color: #b4bac2; font-size: 14px;"));
    messageLayout->addWidget(m_substepLabel);
    panelLayout->addLayout(messageLayout);

    auto *progressHeaderLayout = new QHBoxLayout;
    progressHeaderLayout->setSpacing(8);
    auto *progressTitleLabel = new QLabel(QStringLiteral("总进度"), panel);
    progressTitleLabel->setStyleSheet(QStringLiteral("color: #b4bac2; font-size: 13px;"));
    progressHeaderLayout->addWidget(progressTitleLabel);
    progressHeaderLayout->addStretch(1);
    m_percentageLabel = new QLabel(QStringLiteral("0%"), panel);
    m_percentageLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_percentageLabel->setStyleSheet(QStringLiteral(
        "color: #e8eaed; font-size: 22px; font-weight: 800;"));
    progressHeaderLayout->addWidget(m_percentageLabel);
    panelLayout->addLayout(progressHeaderLayout);

    m_progressBar = new QProgressBar(panel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    panelLayout->addWidget(m_progressBar);

    auto *detailTitleLabel = new QLabel(QStringLiteral("更新详情"), panel);
    detailTitleLabel->setStyleSheet(QStringLiteral(
        "color: #b4bac2; font-size: 13px; font-weight: 700;"));
    panelLayout->addWidget(detailTitleLabel);

    m_detailView = new QPlainTextEdit(panel);
    m_detailView->setObjectName(QStringLiteral("updateDetailView"));
    m_detailView->setReadOnly(true);
    m_detailView->setUndoRedoEnabled(false);
    m_detailView->document()->setMaximumBlockCount(120);
    m_detailView->setMinimumHeight(120);
    m_detailView->setPlaceholderText(QStringLiteral("更新步骤和安装器实际进度会显示在这里。"));
    panelLayout->addWidget(m_detailView);

    panelLayout->addStretch(1);
    m_footerLabel = new QLabel(
        QStringLiteral("请勿关闭该窗口，安装过程会自动完成并重新打开影资管家。"), panel);
    m_footerLabel->setWordWrap(true);
    m_footerLabel->setStyleSheet(QStringLiteral("color: #9aa1aa; font-size: 13px;"));
    panelLayout->addWidget(m_footerLabel);

    updateStepChips(0, QString::fromLatin1(kNeutralAccent));

    m_progressAnimation = new QVariantAnimation(this);
    m_progressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_progressAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
        setDisplayedPercentage(value.toInt());
    });

    connect(m_runner, &UpdaterSessionRunner::progressChanged,
            this, &UpdaterWindow::applyProgress);
    connect(m_runner, &UpdaterSessionRunner::finished, this, [this](bool success, const QString &) {
        m_canClose = true;
        if (success) {
            QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
        }
    });
    QTimer::singleShot(0, this, [this]() { m_runner->start(m_session); });

    if (const auto *screen = QGuiApplication::primaryScreen()) {
        move(screen->availableGeometry().center() - rect().center());
    }
}

void UpdaterWindow::closeEvent(QCloseEvent *event)
{
    if (m_canClose) {
        event->accept();
        return;
    }
    event->ignore();
}

QLabel *UpdaterWindow::createStepChip(int index, const QString &label)
{
    auto *chip = new QLabel(QStringLiteral("%1. %2").arg(index + 1).arg(label), this);
    chip->setAlignment(Qt::AlignCenter);
    chip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return chip;
}

void UpdaterWindow::applyProgress(const UpdaterProgressEvent &event)
{
    const auto accent = event.isError
        ? QString::fromLatin1(kErrorAccent)
        : event.isSuccess
            ? QString::fromLatin1(kSuccessAccent)
            : QString::fromLatin1(kNeutralAccent);

    m_messageLabel->setText(event.message);
    m_substepLabel->setText(event.substep);
    m_substepLabel->setVisible(!event.substep.trimmed().isEmpty());
    appendUpdateDetail(event);
    animatePercentageTo(event.percentage, event.isError);
    updateStepChips(event.stepIndex, accent);

    if (event.isError) {
        m_titleLabel->setText(QStringLiteral("更新失败"));
        m_iconLabel->setText(QStringLiteral("!"));
        m_footerLabel->setText(QStringLiteral("本次自动更新已停止，安装包和会话日志仍保留在更新目录中。"));
        m_percentageLabel->setText(QStringLiteral("%1% · 已停止").arg(m_displayedPercentage));
    } else if (event.isSuccess) {
        m_titleLabel->setText(QStringLiteral("更新完成"));
        m_iconLabel->setText(QStringLiteral("✓"));
        m_footerLabel->setText(QStringLiteral("新版本已启动，更新窗口即将自动关闭。"));
        animatePercentageTo(100);
    }

    m_iconLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: #242a30; border: 1px solid %2; "
        "border-radius: 8px; font-size: 30px; font-weight: 700; }")
        .arg(accent, accent));
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: #20242a; border: 1px solid #30343a; border-radius: 4px; height: 8px; }"
        "QProgressBar::chunk { background: %1; border-radius: 3px; }").arg(accent));
}

void UpdaterWindow::animatePercentageTo(int percentage, bool immediate)
{
    const auto target = qBound(m_displayedPercentage, percentage, 100);
    if (!m_progressAnimation || immediate || target == m_displayedPercentage) {
        if (m_progressAnimation) {
            m_progressAnimation->stop();
        }
        setDisplayedPercentage(target);
        return;
    }

    m_progressAnimation->stop();
    m_progressAnimation->setStartValue(m_displayedPercentage);
    m_progressAnimation->setEndValue(target);
    const auto distance = target - m_displayedPercentage;
    m_progressAnimation->setDuration(qBound(140, distance * 38, 900));
    m_progressAnimation->start();
}

void UpdaterWindow::setDisplayedPercentage(int percentage)
{
    m_displayedPercentage = qBound(m_displayedPercentage, percentage, 100);
    m_progressBar->setValue(m_displayedPercentage);
    m_percentageLabel->setText(QStringLiteral("%1%").arg(m_displayedPercentage));
}

void UpdaterWindow::appendUpdateDetail(const UpdaterProgressEvent &event)
{
    if (!m_detailView) {
        return;
    }
    auto detail = QStringLiteral("%1：%2").arg(event.stepLabel, event.message);
    if (!event.substep.trimmed().isEmpty()) {
        detail += QStringLiteral(" · %1").arg(event.substep.trimmed());
    }
    if (detail == m_lastDetailText) {
        return;
    }
    m_lastDetailText = detail;
    m_detailView->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), detail));
    if (auto *scrollBar = m_detailView->verticalScrollBar()) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

void UpdaterWindow::updateStepChips(int currentStep, const QString &accentColor)
{
    for (int index = 0; index < m_stepChips.size(); ++index) {
        m_stepChips.at(index)->setStyleSheet(chipStyle(index <= currentStep, accentColor));
    }
}
