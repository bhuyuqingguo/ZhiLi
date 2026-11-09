#pragma once

#include <QFont>
#include <QStringList>

class QWidget;

struct FontChoice {
    QString family;
    int points;
    bool bold;
};

struct Appearance {
    FontChoice interfaceFont;
    FontChoice topicFont;
    bool highContrast;

    static Appearance defaults();
    static Appearance load();
    void save() const;
};

void registerEmbeddedFonts();
QStringList embeddedFontFamilies();
QFont fontFor(const FontChoice &choice);
bool editAppearance(QWidget *parent, Appearance *appearance);
