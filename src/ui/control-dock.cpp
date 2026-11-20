#include "control-dock.hpp"
#include "../core/vi-core.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>

ControlDock::ControlDock(QWidget *parent) : QWidget(parent)
{
    setStyleSheet(R"(
        QWidget {
            background-color: #08090C;
            color: #E8ECF2;
            font-family: "Segoe UI", sans-serif;
            font-size: 12px;
        }
        QGroupBox {
            background-color: #101116;
            border: 1px solid #232630;
            border-radius: 4px;
            margin-top: 12px;
            padding: 10px 8px 8px 8px;
            font-weight: bold;
            color: #EF5C9B;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
        QLineEdit, QComboBox {
            background-color: #08090C;
            border: 1px solid #2A2E3B;
            border-radius: 3px;
            padding: 4px 6px;
            color: #FFFFFF;
        }
        QLineEdit:focus, QComboBox:focus {
            border-color: #EF5C9B;
        }
        QPushButton {
            background-color: #161822;
            border: 1px solid #2D3140;
            border-radius: 3px;
            padding: 5px 10px;
            color: #F2F4F8;
            font-weight: 600;
        }
        QPushButton:hover {
            border-color: #EF5C9B;
            background-color: #1D1F2C;
        }
        QPushButton:pressed {
            background-color: #EF5C9B;
            color: #08090C;
        }
        QCheckBox {
            spacing: 6px;
            color: #C7CDD8;
        }
    )");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *connBox = new QGroupBox("TWITCH CHAT", this);
    auto *connLay = new QVBoxLayout(connBox);
    auto *row = new QHBoxLayout();
    m_channelEdit = new QLineEdit(VICore::instance()->channel(), this);
    m_channelEdit->setPlaceholderText("ktotakoyben");
    auto *btnConn = new QPushButton("CONNECT", this);
    row->addWidget(m_channelEdit);
    row->addWidget(btnConn);
    m_statusLabel = new QLabel("STATUS: OFFLINE", this);
    m_statusLabel->setStyleSheet("color: #8B92A0; font-family: Consolas, monospace; font-size: 11px;");
    connLay->addLayout(row);
    connLay->addWidget(m_statusLabel);
    root->addWidget(connBox);

    auto *modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel("РЕЖИМ XP:", this));
    m_modeBox = new QComboBox(this);
    m_modeBox->addItem("HOT 30M", "hot30");
    m_modeBox->addItem("SESSION", "session");
    m_modeBox->addItem("LIFETIME", "lifetime");
    modeRow->addWidget(m_modeBox);
    root->addLayout(modeRow);

    auto *evBox = new QGroupBox("EVENT ENGINE", this);
    auto *evLay = new QVBoxLayout(evBox);
    auto *btnRnd = new QPushButton("Запустить случайный ивент", this);
    auto *btnRare = new QPushButton("Тест: RARE (Вопрос)", this);
    auto *btnLeg = new QPushButton("Тест: LEGENDARY", this);
    auto *btnReply = new QPushButton("Имитировать ответ зрителя", this);
    evLay->addWidget(btnRnd);
    evLay->addWidget(btnRare);
    evLay->addWidget(btnLeg);
    evLay->addWidget(btnReply);
    root->addWidget(evBox);

    m_demoCheck = new QCheckBox("Demo Mode (фейковый чат)", this);
    m_pauseCheck = new QCheckBox("Пауза авто-ивентов", this);
    auto *btnReset = new QPushButton("Сбросить Session XP", this);
    root->addWidget(m_demoCheck);
    root->addWidget(m_pauseCheck);
    root->addWidget(btnReset);
    root->addStretch();

    connect(btnConn, &QPushButton::clicked, this, [this]() {
        VICore::instance()->connectChannel(m_channelEdit->text());
    });
    connect(VICore::instance(), &VICore::statusChanged, this, [this](bool ok, const QString &txt) {
        m_statusLabel->setText("STATUS: " + txt);
        m_statusLabel->setStyleSheet(ok
            ? "color: #4ADE80; font-family: Consolas, monospace; font-size: 11px;"
            : "color: #EF5C9B; font-family: Consolas, monospace; font-size: 11px;");
    });
    connect(m_modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        VICore::instance()->setRankingMode(m_modeBox->itemData(idx).toString());
    });
    connect(btnRnd, &QPushButton::clicked, this, []() { VICore::instance()->triggerRandomEvent(); });
    connect(btnRare, &QPushButton::clicked, this, []() { VICore::instance()->triggerRandomEvent("QUESTIONS"); });
    connect(btnLeg, &QPushButton::clicked, this, []() { VICore::instance()->triggerRandomEvent("SECRET_LEGENDARY"); });
    connect(btnReply, &QPushButton::clicked, this, []() { VICore::instance()->simulateTargetReply(); });
    connect(m_demoCheck, &QCheckBox::toggled, this, [](bool on) { VICore::instance()->setDemoMode(on); });
    connect(m_pauseCheck, &QCheckBox::toggled, this, [](bool on) { VICore::instance()->setEventsPaused(on); });
    connect(btnReset, &QPushButton::clicked, this, []() { VICore::instance()->resetSessionXp(); });
}
