#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QResizeEvent;
class QBoxLayout;

// Modal, offline browser; the caller applies the result as one undoable edit.
// Rejected: do nothing. Accepted with empty ID: remove the selected node's icon.
class SymbolPickerDialog : public QDialog {
public:
    explicit SymbolPickerDialog(QWidget *parent = nullptr,
                                const QString &initialId = QString(),
                                bool dimensional = false);
    QString selectedSymbolId() const { return selectedId_; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateResponsiveLayout(const QSize &available);
    void refresh();
    void updateSelection();
    void resizeTiles();
    void applySelection();

    QLabel *intro_;
    QLabel *note_;
    QLabel *sizeLabel_;
    QBoxLayout *filters_;
    QPushButton *remove_;
    bool compact_ = false;
    bool dimensional_;
    QLineEdit *search_;
    QComboBox *category_;
    QListWidget *list_;
    QLabel *count_;
    QLabel *preview_;
    QLabel *description_;
    QPushButton *apply_;
    QSlider *size_;
    QString currentId_;
    QString selectedId_;
};
