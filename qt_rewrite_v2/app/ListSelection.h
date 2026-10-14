#pragma once

class QListWidget;

namespace colorizing::app {

// Makes the row the current row and the only selected one. QListWidget::setCurrentRow(row)
// alone does not do that in ExtendedSelection mode: it keeps part of an earlier
// multi-selection, and does nothing at all when the row is current already. A negative row
// clears the current row and the selection.
void selectSingleRow(QListWidget& list, int row);

}  // namespace colorizing::app
