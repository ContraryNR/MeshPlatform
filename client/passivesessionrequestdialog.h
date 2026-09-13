#ifndef PASSIVESESSIONREQUESTDIALOG_H
#define PASSIVESESSIONREQUESTDIALOG_H

#include "sessionbasedialog.h"

class PassiveSessionRequestDialog : public SessionBaseDialog
{
    Q_OBJECT
public:
    PassiveSessionRequestDialog(const QString& title, const QString& question,
                                const QString& paramsText, bool isVideo,
                                int vw, int vh, int vf,
                                int asr, int acc, int noiseGate,
                                QWidget* parent = nullptr)
        : SessionBaseDialog(title, question, paramsText, "同意", "拒绝",
                            isVideo, vw, vh, vf, asr, acc, noiseGate, parent) {}
};

#endif
