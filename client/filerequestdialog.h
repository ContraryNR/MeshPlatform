#ifndef FILEREQUESTDIALOG_H
#define FILEREQUESTDIALOG_H

#include "topbasedialog.h"

class FileRequestDialog : public TopBaseDialog
{
    Q_OBJECT
public:
    FileRequestDialog(const QString& title, const QString& question,
                      const QString& paramsText, QWidget* parent = nullptr)
        : TopBaseDialog(title, question, paramsText, "同意", "拒绝", parent) {}
};

#endif
