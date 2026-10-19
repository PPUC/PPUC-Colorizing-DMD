#pragma once

#include <QAbstractButton>
#include <QBoxLayout>
#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLayout>
#include <QRadioButton>
#include <QSizePolicy>
#include <QWidget>

namespace colorizing::app {

// The forms of the side bars, all alike whatever the platform's defaults: the labels in one
// left-aligned column, the fields taking the width there is, and a field below its label when
// the side bar is too narrow for both.
inline QFormLayout* makeInspectorForm(QWidget* parent = nullptr)
{
    auto* form = new QFormLayout(parent);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    return form;
}

// The action buttons of a side bar page, all alike: a row or grid of buttons shares the page's
// width in equal parts, and a button on its own takes the width. Rows and grids that mix
// buttons with other controls (coordinates, the shadow directions) are left as they are, and
// buttons of a fixed size keep it; check boxes and radio buttons are not action buttons. Called on the page's layout once it is
// complete.
inline void fillButtonRows(QLayout* layout)
{
    if (!layout || qobject_cast<QFormLayout*>(layout)) {
        return;
    }
    const auto isAction = [](QWidget* widget) {
        return qobject_cast<QAbstractButton*>(widget) && !qobject_cast<QCheckBox*>(widget) &&
               !qobject_cast<QRadioButton*>(widget);
    };
    const auto fill = [](QWidget* widget) {
        widget->setSizePolicy(QSizePolicy::Expanding, widget->sizePolicy().verticalPolicy());
    };
    auto* box = qobject_cast<QBoxLayout*>(layout);
    const bool row = qobject_cast<QGridLayout*>(layout) ||
                     (box && (box->direction() == QBoxLayout::LeftToRight ||
                              box->direction() == QBoxLayout::RightToLeft));
    if (row) {
        bool onlyButtons = true;
        int buttons = 0;
        for (int index = 0; index < layout->count(); ++index) {
            QLayoutItem* item = layout->itemAt(index);
            if (item->spacerItem()) {
                continue;
            }
            if (!item->widget() || !isAction(item->widget())) {
                onlyButtons = false;
                break;
            }
            ++buttons;
        }
        if (onlyButtons && buttons > 0) {
            for (int index = 0; index < layout->count(); ++index) {
                if (QWidget* widget = layout->itemAt(index)->widget()) {
                    fill(widget);
                }
            }
        }
        return;
    }
    for (int index = 0; index < layout->count(); ++index) {
        QLayoutItem* item = layout->itemAt(index);
        if (item->widget() && isAction(item->widget())) {
            fill(item->widget());
        } else if (item->layout()) {
            fillButtonRows(item->layout());
        }
    }
}

}  // namespace colorizing::app
