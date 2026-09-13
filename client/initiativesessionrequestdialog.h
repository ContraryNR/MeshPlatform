#ifndef INITIATIVESESSIONREQUESTDIALOG_H
#define INITIATIVESESSIONREQUESTDIALOG_H

#include "sessionbasedialog.h"

class InitiativeSessionRequestDialog : public SessionBaseDialog
{
    Q_OBJECT
public:
    InitiativeSessionRequestDialog(const QString& title, bool isVideo,
                                   int vw, int vh, int vf,
                                   int asr, int acc, int noiseGate,
                                   QWidget* parent = nullptr)
        : SessionBaseDialog(title, "", "", "确定", "取消",
                            isVideo, vw, vh, vf, asr, acc, noiseGate, parent) {}
};

#endif
