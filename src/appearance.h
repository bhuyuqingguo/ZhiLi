#pragma once

#include "document.h"
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
    bool dimensionalSymbols;

    static Appearance defaults();
    static Appearance load();
    void save() const;
};

struct TopicStyleChange {
    TopicStyle style;
    bool alignment = false;
    bool family = false;
    bool points = false;
    bool weight = false;
    bool color = false;
    bool descendants = false;
};

void registerEmbeddedFonts();
QStringList embeddedFontFamilies();
QFont fontFor(const FontChoice &choice);
bool editAppearance(QWidget *parent, Appearance *appearance);
bool editTopicStyle(QWidget *parent, const TopicStyle &current, int selectionCount, TopicStyleChange *change);
