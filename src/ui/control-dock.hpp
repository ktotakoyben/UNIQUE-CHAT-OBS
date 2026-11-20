#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>

class ControlDock : public QWidget {
    Q_OBJECT
public:
    explicit ControlDock(QWidget *parent = nullptr);

private:
    QLineEdit *m_channelEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QComboBox *m_modeBox = nullptr;
    QCheckBox *m_demoCheck = nullptr;
    QCheckBox *m_pauseCheck = nullptr;
};
