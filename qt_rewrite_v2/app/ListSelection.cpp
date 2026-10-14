#include "ListSelection.h"

#include <QItemSelectionModel>
#include <QListWidget>

namespace colorizing::app {

void selectSingleRow(QListWidget& list, int row)
{
    if (row < 0 || row >= list.count()) {
        list.setCurrentRow(-1, QItemSelectionModel::Clear);
        return;
    }
    list.setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
}

}  // namespace colorizing::app
