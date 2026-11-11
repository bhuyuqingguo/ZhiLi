#pragma once

#include <QAction>
#include <QMap>
#include <QVector>
#include <functional>

// Each entry is backed by a real QAction; the reference catalogue never
// registers inactive shortcuts or steals key events from the editor.
class ShortcutSettings {
public:
    struct Command {
        QAction *action;
        QString group, id, description;
        QList<QKeySequence> defaults;
    };
    using Bindings = QMap<QAction *, QList<QKeySequence>>;
    void add(QAction *action, const QString &group);
    void load();
    Bindings current() const;
    Bindings defaults() const;
    bool apply(const Bindings &bindings, QString *error = nullptr, bool persist = true);
    void showDialog(QWidget *parent);
    QString hint(const char *defaultKey) const;
    const QVector<Command> &commands() const { return entries; }
    std::function<void()> changed;
private:
    QVector<Command> entries;
    bool validate(const Bindings &bindings, QString *error) const;
};
