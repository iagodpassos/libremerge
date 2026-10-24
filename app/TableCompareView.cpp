// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "TableCompareView.h"

#include <set>

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QTemporaryFile>
#include <QToolBar>
#include <QVBoxLayout>

#include "DisplayFilterBar.h"
#include "FilepathEdit.h"
#include "FileFilters.h"
#include "FileOps.h"
#include "Icons.h"
#include "LineFilterMenu.h"
#include "MessageBoxes.h"
#include "NewComparisonView.h"
#include "OptionsDialog.h"
#include "TableFiles.h"
#include "Theme.h"

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "EngineOptions.h"
#include "FileTransform.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/ILineDataProvider.h"
#include "LineFilterHelper.h"
#include "Logger.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"
#include "PathContext.h"
#include "UniFile.h"

namespace
{

/** Split one line into its cells by the side's delimiter, honoring its
    quote character (a doubled one escapes it). */
QStringList splitRow(const QString &line, const lm::TableProps &props)
{
	const QChar quote = props.quote;
	QStringList cells;
	QString cell;
	bool quoted = false;
	for (int i = 0; i < line.size(); ++i)
	{
		const QChar c = line.at(i);
		if (quoted)
		{
			if (c == quote)
			{
				if (i + 1 < line.size() && line.at(i + 1) == quote)
				{
					cell.append(quote);
					++i;
				}
				else
					quoted = false;
			}
			else
				cell.append(c);
		}
		else if (!quote.isNull() && c == quote && cell.isEmpty())
			quoted = true;
		else if (!props.delimiter.isNull() && c == props.delimiter)
		{
			cells.append(cell);
			cell.clear();
		}
		else
			cell.append(c);
	}
	cells.append(cell);
	return cells;
}

quint64 cellKey(int row, int column)
{
	return (static_cast<quint64>(static_cast<quint32>(row)) << 32)
		| static_cast<quint32>(column);
}

/** A row's header tells whether hidden rows follow the row. */
constexpr int kBoundaryRole = Qt::UserRole + 1;

int rawCellCount(const QString &line, const lm::TableProps &props);

/** Where a cell's text sits in its line, quotes and all, the delimiters
    outside quotes parting the cells as rawCell reads them: from its first
    character to past its last; -1 for a cell the line does not have. */
std::pair<int, int> rawCellSpan(const QString &line, const lm::TableProps &props, int column)
{
	int current = 0;
	int start = 0;
	bool quoted = false;
	for (int i = 0; i < line.size(); ++i)
	{
		const QChar c = line.at(i);
		if (!props.quote.isNull() && c == props.quote)
			quoted = !quoted;
		else if (!quoted && !props.delimiter.isNull() && c == props.delimiter)
		{
			if (current == column)
				return { start, i };
			++current;
			start = i + 1;
		}
	}
	return current == column ? std::pair<int, int>{ start, static_cast<int>(line.size()) }
		: std::pair<int, int>{ -1, -1 };
}

/** A line with one of its cells written anew: the cells a short line
    lacks up to it are added, empty. */
QString withCell(const QString &line, const lm::TableProps &props, int column, const QString &raw)
{
	const auto [start, end] = rawCellSpan(line, props, column);
	if (start >= 0)
		return QString(line).replace(start, end - start, raw);
	if (props.delimiter.isNull())
		return line;
	const int cells = rawCellCount(line, props);
	return line + QString(column - cells + 1, props.delimiter) + raw;
}

/** A cell's value as the file writes it: within the quote character when
    it holds the delimiter, the quote character (doubled inside) or a line
    break, or when the cell it replaces was quoted. */
QString rawValue(const QString &value, const lm::TableProps &props, bool quoted)
{
	const QChar quote = props.quote;
	if (quote.isNull())
		return value;
	if (!quoted && !value.contains(quote) && !value.contains(QLatin1Char('\n'))
		&& !value.contains(QLatin1Char('\r'))
		&& (props.delimiter.isNull() || !value.contains(props.delimiter)))
		return value;
	QString inner = value;
	inner.replace(quote, QString(2, quote));
	return quote + inner + quote;
}

/** The editor of a cell: a line, or lines for a cell that has line
    breaks (a quoted cell may, with "Allow newlines in quotes"). */
class CellDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
		const QModelIndex &index) const override
	{
		if (index.data(Qt::EditRole).toString().contains(QLatin1Char('\n')))
			return new QPlainTextEdit(parent);
		return QStyledItemDelegate::createEditor(parent, option, index);
	}

	void setEditorData(QWidget *editor, const QModelIndex &index) const override
	{
		if (auto *text = qobject_cast<QPlainTextEdit *>(editor))
		{
			text->setPlainText(index.data(Qt::EditRole).toString());
			return;
		}
		QStyledItemDelegate::setEditorData(editor, index);
	}

	void setModelData(QWidget *editor, QAbstractItemModel *model,
		const QModelIndex &index) const override
	{
		if (auto *text = qobject_cast<QPlainTextEdit *>(editor))
		{
			model->setData(index, text->toPlainText(), Qt::EditRole);
			return;
		}
		QStyledItemDelegate::setModelData(editor, model, index);
	}
};

/** The grid of a side, with a rule under each row that hidden rows
    follow: upstream's editor draws its boundary line there, in the colour
    of the text and from the margin on (CCrystalTextView's
    DrawBoundaryLine). */
class RuledTableView : public QTableView
{
public:
	using QTableView::QTableView;

protected:
	void paintEvent(QPaintEvent *event) override
	{
		QTableView::paintEvent(event);
		const QAbstractItemModel *rows = model();
		if (rows == nullptr || rows->rowCount() == 0)
			return;
		// the rows in what is being painted; none when it starts under
		// the last one
		const int first = rowAt(event->rect().top());
		int last = rowAt(event->rect().bottom());
		if (first < 0)
			return;
		if (last < 0)
			last = rows->rowCount() - 1;
		QPainter painter(viewport());
		painter.setPen(palette().color(QPalette::Text));
		for (int row = first; row <= last; ++row)
		{
			if (!rows->headerData(row, Qt::Vertical, kBoundaryRole).toBool())
				continue;
			const int y = rowViewportPosition(row) + rowHeight(row) - 1;
			painter.drawLine(0, y, viewport()->width(), y);
		}
	}
};

/** The margin's part of that rule. */
class RuledRowHeader : public QHeaderView
{
public:
	explicit RuledRowHeader(QWidget *parent)
		: QHeaderView(Qt::Vertical, parent)
	{
		// as a table view sets up the header it makes for itself
		setSectionsClickable(true);
		setHighlightSections(true);
	}

protected:
	void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
	{
		painter->save();
		QHeaderView::paintSection(painter, rect, logicalIndex);
		painter->restore();
		if (model() == nullptr
			|| !model()->headerData(logicalIndex, Qt::Vertical, kBoundaryRole).toBool())
			return;
		painter->save();
		painter->setPen(palette().color(QPalette::Text));
		painter->drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());
		painter->restore();
	}
};

/** A cell of a line the way upstream's table reads it for a filter
    (CCrystalTextBuffer::GetColumnCount and GetCellText): the delimiters
    outside quotes part the cells, and a cell keeps the quotes it is
    written with. */
/** A cell's text as the grid shows it: a line break kept from the file,
    whatever its kind, starts a line of the cell. */
QString displayText(const QString &cell)
{
	if (!cell.contains(QLatin1Char('\r')))
		return cell;
	QString text = cell;
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	return text;
}

int rawCellCount(const QString &line, const lm::TableProps &props)
{
	int count = 1;
	bool quoted = false;
	for (const QChar c : line)
	{
		if (!props.quote.isNull() && c == props.quote)
			quoted = !quoted;
		else if (!quoted && !props.delimiter.isNull() && c == props.delimiter)
			++count;
	}
	return count;
}

QString rawCell(const QString &line, const lm::TableProps &props, int column)
{
	QString cell;
	int current = 0;
	bool quoted = false;
	const bool delimiter = !props.delimiter.isNull();
	for (int i = 0; i < line.size() && current <= column; ++i)
	{
		const QChar c = line.at(i);
		if (current == column && (quoted || !delimiter || c != props.delimiter))
			cell.append(c);
		if (!props.quote.isNull() && c == props.quote)
			quoted = !quoted;
		else if (!quoted && delimiter && c == props.delimiter)
			++current;
	}
	return cell;
}

} // namespace

/**
 * What a line filter expression asks about the rows (CMergeDoc as an
 * ILineDataProvider, its buffers in table mode): the view rows of both
 * sides, ghost rows included, with the flags upstream's buffers carry
 * and the cells of a row for its columns.
 */
class TableCompareView::LineProvider : public ILineDataProvider
{
public:
	explicit LineProvider(const TableCompareView *view)
		: m_view(view)
	{
		const int rows = static_cast<int>(view->m_viewToReal[0].size());
		m_sides = view->m_paneCount;
		for (int side = 0; side < m_sides; ++side)
		{
			m_flags[side].assign(rows, 0);
			m_realIndex[side].assign(rows, 0);
			int real = 0;
			for (int row = 0; row < rows; ++row)
			{
				// (a ghost row counts as the real row that follows it)
				m_realIndex[side][row] = real;
				if (view->m_viewToReal[side].value(row, -1) < 0)
					m_flags[side][row] |= LF_GHOST;
				else
					++real;
			}
			m_realCount[side] = real;
		}
		// the difference flags, as PrimeTextBuffers sets them
		for (const Block &block : view->m_blocks)
			for (int side = 0; side < m_sides; ++side)
			{
				unsigned threeWay = 0;
				if (block.op == OP_1STONLY)
					threeWay = LF_DIFF_1STONLY;
				else if (block.op == OP_2NDONLY)
					threeWay = LF_DIFF_2NDONLY;
				else if (block.op == OP_3RDONLY)
					threeWay = LF_DIFF_3RDONLY;
				for (int row = block.viewBegin; row <= block.viewEnd && row < rows; ++row)
				{
					unsigned &flags = m_flags[side][row];
					flags |= threeWay;
					if (block.trivial)
						flags |= LF_TRIVIAL;
					else if (!(flags & LF_GHOST))
						flags |= LF_DIFF;
				}
			}
	}

	int GetLineCount() const override
	{
		return static_cast<int>(m_flags[0].size());
	}
	std::string GetLine(int pane, int lineIndex) const override
	{
		return line(pane, lineIndex).toStdString();
	}
	int GetColumnCount(int pane, int lineIndex) const override
	{
		return valid(pane, lineIndex)
			? rawCellCount(line(pane, lineIndex), m_view->m_sides[pane].props) : 1;
	}
	std::string GetColumn(int pane, int lineIndex, int columnIndex) const override
	{
		return rawCell(line(pane, lineIndex), m_view->m_sides[pane].props,
			columnIndex).toStdString();
	}
	/** CMergeDoc::GetColumns: the cells asked for, joined by the side's
	    delimiter (a ColumnRange of a filter). */
	std::string GetColumns(int pane, int lineIndex, const std::vector<int> &columns) const override
	{
		if (!valid(pane, lineIndex))
			return std::string();
		const lm::TableProps &props = m_view->m_sides[pane].props;
		const QString text = line(pane, lineIndex);
		QStringList cells;
		for (const int column : columns)
			cells.append(rawCell(text, props, column));
		return cells.join(props.delimiter.isNull() ? QString() : QString(props.delimiter))
			.toStdString();
	}
	int GetRealLineNumber(int pane, int lineIndex) const override
	{
		return valid(pane, lineIndex) ? m_realIndex[pane][lineIndex] : 0;
	}
	unsigned GetLineFlags(int pane, int lineIndex) const override
	{
		return valid(pane, lineIndex) ? m_flags[pane][lineIndex] : 0;
	}
	unsigned GetLineEol(int pane, int lineIndex) const override
	{
		if (!valid(pane, lineIndex))
			return EOL_NONE;
		// the row's ending, the file's unless its rows end in more than
		// one way; the last row has none when the file ends without one
		const Side &side = m_view->m_sides[pane];
		const int real = m_realIndex[pane][lineIndex];
		if (!side.hadFinalEol && real == m_realCount[pane] - 1)
			return EOL_NONE;
		const QString eol = m_view->rowEol(pane, real);
		if (eol == QStringLiteral("\r\n"))
			return EOL_CRLF;
		return eol == QStringLiteral("\r") ? EOL_CR : EOL_LF;
	}

private:
	bool valid(int pane, int lineIndex) const
	{
		return pane >= 0 && pane < m_sides && lineIndex >= 0
			&& lineIndex < static_cast<int>(m_flags[pane].size());
	}
	/** The row's line as the file has it, empty for a ghost row. */
	QString line(int pane, int lineIndex) const
	{
		if (!valid(pane, lineIndex))
			return QString();
		const int real = m_view->m_viewToReal[pane].value(lineIndex, -1);
		return real >= 0 && real < m_view->m_sides[pane].rawLines.size()
			? m_view->m_sides[pane].rawLines.at(real) : QString();
	}

	const TableCompareView *m_view;
	int m_sides = 2;
	std::vector<unsigned> m_flags[3];
	std::vector<int> m_realIndex[3];
	int m_realCount[3] = {};
};

/** Read-only model over one side's aligned rows. */
class TableSideModel : public QAbstractTableModel
{
public:
	TableSideModel(TableCompareView *view, int side)
		: QAbstractTableModel(view), m_view(view), m_side(side) {}

	int rowCount(const QModelIndex &parent = {}) const override
	{
		// the rows on show: not the header's, not the ones a display
		// filter hides
		return parent.isValid() ? 0 : static_cast<int>(m_view->m_modelRows.size());
	}

	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : m_columns;
	}

	QVariant data(const QModelIndex &index, int role) const override
	{
		const int viewRow = toViewRow(index.row());
		if (viewRow < 0
			|| viewRow >= m_view->m_viewToReal[m_side].size())
			return {};
		const int real = m_view->m_viewToReal[m_side].at(viewRow);

		if (role == Qt::DisplayRole || role == Qt::EditRole)
		{
			if (real < 0)
				return role == Qt::EditRole ? QVariant(QString()) : QVariant();
			const auto &cells = m_view->m_sides[m_side].cells;
			if (real < static_cast<int>(cells.size())
				&& index.column() < cells[real].size())
				return displayText(cells[real].at(index.column()));
			return role == Qt::EditRole ? QVariant(QString()) : QVariant();
		}
		if (role == Qt::BackgroundRole)
		{
			const lm::DiffColors &C = lm::diffColors();
			if (real < 0)
				return C.diffDeleted;
			for (size_t b = 0; b < m_view->m_blocks.size(); ++b)
			{
				const auto &block = m_view->m_blocks[b];
				if (viewRow < block.viewBegin || viewRow > block.viewEnd)
					continue;
				if (block.trivial)
					return C.trivial;
				const bool current =
					static_cast<int>(b) == m_view->m_current;
				if (m_view->m_cellDiffs[m_side].contains(
						cellKey(real, index.column())))
					return current ? C.selWordDiffDeleted : C.wordDiffDeleted;
				return current ? C.selDiff : C.diff;
			}
			return {};
		}
		return {};
	}

	/** Every cell takes an edit, a filler's too: upstream's table is its
	    text editor, where typing on a filler line makes it a line of the
	    file. */
	Qt::ItemFlags flags(const QModelIndex &index) const override
	{
		return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
	}

	bool setData(const QModelIndex &index, const QVariant &value, int role) override
	{
		if (role != Qt::EditRole || !index.isValid())
			return false;
		// once the editor has let go: the edit compares the tables again,
		// and so resets this model
		TableCompareView *view = m_view;
		const int side = m_side;
		const int row = index.row();
		const int column = index.column();
		const QString text = value.toString();
		QTimer::singleShot(0, view, [view, side, row, column, text]() {
			view->editCell(side, row, column, text);
		});
		return true;
	}

	QVariant headerData(int section, Qt::Orientation orientation,
		int role) const override
	{
		if (orientation == Qt::Vertical && role == kBoundaryRole)
			return m_view->endsShownStretch(toViewRow(section));
		if (role != Qt::DisplayRole)
			return {};
		if (orientation == Qt::Horizontal)
		{
			if (m_view->m_firstRowIsHeader
				&& !m_view->m_sides[m_side].cells.empty()
				&& section < m_view->m_sides[m_side].cells[0].size())
				return m_view->m_sides[m_side].cells[0].at(section);
			return section + 1;
		}
		// vertical: real row numbers, blank on ghost filler
		const int viewRow = toViewRow(section);
		if (viewRow < 0 || viewRow >= m_view->m_viewToReal[m_side].size())
			return {};
		const int real = m_view->m_viewToReal[m_side].at(viewRow);
		return real < 0 ? QVariant(QStringLiteral(" "))
			: QVariant(real + 1);
	}

	void refresh(int columns)
	{
		beginResetModel();
		m_columns = columns;
		endResetModel();
	}

	/** The backgrounds again, for another current difference: no reset,
	    so the grid keeps its current cell. */
	void recolor()
	{
		if (rowCount() > 0 && m_columns > 0)
			emit dataChanged(index(0, 0), index(rowCount() - 1, m_columns - 1),
				{ Qt::BackgroundRole });
	}

private:
	int toViewRow(int modelRow) const
	{
		return m_view->m_modelRows.value(modelRow, -1);
	}

	TableCompareView *m_view;
	int m_side;
	int m_columns = 0;
};

TableCompareView::~TableCompareView()
{
	// the models read the members, which go before QWidget's destructor
	// hides a view shown as a window, and the grids' headers ask the
	// models for their texts then: the grids let go of the models first
	for (QTableView *table : m_tables)
		table->setModel(nullptr);
}

TableCompareView::TableCompareView(QWidget *parent)
	: QWidget(parent)
	, m_displayFilter(std::make_unique<LineFilterHelper>())
{
	auto *layout = new QVBoxLayout(this);
	m_layout = layout;
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	// same toolbar as the text compare (WinMerge's table view is the same
	// editor, so the toolbar is identical there); shortcuts live on the
	// main window's menu and are only mentioned in tooltips
	auto addToolAction = [this, toolbar](lm::Icon icon, const QString &text,
		const QString &shortcutHint, auto slot) -> QAction * {
		QAction *action = toolbar->addAction(lm::icon(icon), text);
		action->setData(static_cast<int>(icon)); // re-rendered on theme change
		action->setToolTip(shortcutHint.isEmpty() ? text
			: QStringLiteral("%1 (%2)").arg(text, shortcutHint));
		connect(action, &QAction::triggered, this, slot);
		return action;
	};
	addToolAction(lm::Icon::FirstDiff, tr("First Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x96"), [this]() { gotoFirstDiff(); });
	addToolAction(lm::Icon::PrevDiff, tr("Previous Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x91"), [this]() { gotoPrevDiff(); });
	addToolAction(lm::Icon::NextDiff, tr("Next Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x93"), [this]() { gotoNextDiff(); });
	addToolAction(lm::Icon::LastDiff, tr("Last Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x98"), [this]() { gotoLastDiff(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::CopyRight, tr("Copy to Right"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x92"), [this]() { copyToRight(); });
	addToolAction(lm::Icon::CopyLeft, tr("Copy to Left"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x90"), [this]() { copyToLeft(); });
	addToolAction(lm::Icon::CopyAllRight, tr("Copy All to Right"), QString(),
		[this]() { copyAllToRight(); });
	addToolAction(lm::Icon::CopyAllLeft, tr("Copy All to Left"), QString(),
		[this]() { copyAllToLeft(); });
	toolbar->addSeparator();
	m_actUndo = addToolAction(lm::Icon::Undo, tr("Undo"),
		QString::fromUtf8("\xE2\x8C\x98Z"), [this]() { undo(); });
	m_actUndo->setEnabled(false);
	m_actRedo = addToolAction(lm::Icon::Redo, tr("Redo"),
		QString::fromUtf8("\xE2\x87\xA7\xE2\x8C\x98Z"), [this]() { redo(); });
	m_actRedo->setEnabled(false);
	toolbar->addSeparator();
	addToolAction(lm::Icon::Swap, tr("Swap Panes"), QString(),
		[this]() { swapSides(); });
	addToolAction(lm::Icon::Refresh, tr("Recompare"), QStringLiteral("F5"),
		[this]() { refreshByUser(); });
	m_actSave = addToolAction(lm::Icon::Save, tr("Save"),
		QString::fromUtf8("\xE2\x8C\x98S"),
		[this]() { QString error; saveModified(&error); });
	m_actSave->setEnabled(false);
	toolbar->addSeparator();
	m_actHeader = addToolAction(lm::Icon::DiffPane,
		tr("First row is the header"), QString(), [this]() {});
	m_actHeader->setCheckable(true);
	m_actHeader->setChecked(true);
	connect(m_actHeader, &QAction::toggled, this, [this](bool on) {
		m_firstRowIsHeader = on;
		rebuildModel();
	});
	addToolAction(lm::Icon::Find, tr("Open as Text"), QString(), [this]() {
		emit openAsTextRequested(paths());
	});
	layout->addWidget(toolbar);

	auto *tables = new QHBoxLayout;
	tables->setContentsMargins(0, 0, 0, 0);
	tables->setSpacing(1);
	for (int i = 0; i < 3; ++i)
	{
		// upstream's path bar (CEditorFilePathBar) over each side: the
		// table is its text window in another dress
		m_columns[i] = new QWidget(this);
		auto *column = new QVBoxLayout(m_columns[i]);
		column->setContentsMargins(0, 0, 0, 0);
		column->setSpacing(0);
		m_headers[i] = new FilepathEdit(this);
		column->addWidget(m_headers[i]);
		connect(m_headers[i], &FilepathEdit::focusedIn, this, [this, i]() { setActiveSide(i); });
		connect(m_headers[i], &FilepathEdit::captionChanged, this, [this, i](const QString &text) {
			// SetDescription and UpdateHeaderPath
			setSideDescription(i, text);
			m_tables[i]->setFocus();
			emit pathsChanged();
		});
		connect(m_headers[i], &FilepathEdit::fileSelected, this,
			[this, i](const QString &path, const QString &description) {
				if (!changeFile(i, path, description))
					return;
				m_tables[i]->setFocus();
				// the clipboard's file is not a recent one
				if (description.isEmpty())
					NewComparisonView::rememberPaths({ path });
			});
		connect(m_headers[i], &FilepathEdit::editEnded, this, [this, i]() { m_tables[i]->setFocus(); });

		m_models[i] = new TableSideModel(this, i);
		m_tables[i] = new RuledTableView(this);
		m_tables[i]->setVerticalHeader(new RuledRowHeader(m_tables[i]));
		m_tables[i]->setModel(m_models[i]);
		// a cell edits on F2 or as typing starts (a double click selects
		// the difference, as upstream's does); cells select one by one, for
		// the copies of the selected cells
		m_tables[i]->setItemDelegate(new CellDelegate(m_tables[i]));
		m_tables[i]->setEditTriggers(QAbstractItemView::EditKeyPressed
			| QAbstractItemView::AnyKeyPressed);
		m_tables[i]->setSelectionBehavior(QAbstractItemView::SelectItems);
		m_tables[i]->horizontalHeader()->setDefaultSectionSize(110);
		m_tables[i]->verticalHeader()->setDefaultSectionSize(22);
		m_tables[i]->setAlternatingRowColors(false);
		column->addWidget(m_tables[i], 1);
		tables->addWidget(m_columns[i], 1);
		// the other grids follow one that scrolls
		connect(m_tables[i]->verticalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) {
				if (m_syncing)
					return;
				m_syncing = true;
				for (int other = 0; other < m_paneCount; ++other)
					if (other != i)
						m_tables[other]->verticalScrollBar()->setValue(value);
				m_syncing = false;
			});
		connect(m_tables[i]->horizontalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) {
				if (m_syncing)
					return;
				m_syncing = true;
				for (int other = 0; other < m_paneCount; ++other)
					if (other != i)
						m_tables[other]->horizontalScrollBar()->setValue(value);
				m_syncing = false;
			});
		connect(m_tables[i], &QTableView::doubleClicked,
			this, [this](const QModelIndex &index) {
				// select the difference under the double-clicked row
				selectDiffAtModelRow(index.row());
			});
		// and a click outside it leaves none current, like OnLButtonUp
		connect(m_tables[i], &QTableView::clicked,
			this, [this](const QModelIndex &index) {
				deselectDiffIfCursorNotInCurrentDiff(index.row());
			});
		// upstream's menu of a right click on the headers
		QHeaderView *header = m_tables[i]->horizontalHeader();
		header->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(header, &QWidget::customContextMenuRequested, this,
			[this, i, header](const QPoint &pos) {
				QMenu menu(this);
				buildHeaderMenu(i, header->logicalIndexAt(pos), &menu);
				menu.exec(header->mapToGlobal(pos));
			});
	}
	layout->addLayout(tables, 1);
	m_columns[2]->hide(); // until three files are compared
	// the bar of the grid that has the focus highlighted
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
		for (int i = 0; i < m_paneCount; ++i)
			if (now == m_tables[i])
				setActiveSide(i);
	});
	FilepathEdit::addEditPathShortcut(this, [this]() { editActivePanePath(); });

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 3, 6, 3);
	layout->addWidget(m_status);

	applyTheme();
	connect(lm::Theme::instance(), &lm::Theme::changed,
		this, [this]() { applyTheme(); });
}

void TableCompareView::applyTheme()
{
	const bool dark = lm::Theme::instance()->dark();
	QPalette pal;
	if (dark)
	{
		pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
		pal.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
	}
	else
	{
		pal.setColor(QPalette::Base, Qt::white);
		pal.setColor(QPalette::Text, Qt::black);
	}
	for (QTableView *table : m_tables)
		table->setPalette(pal);
	m_status->setStyleSheet(dark
		? QStringLiteral("QLabel { background: #2c2c2c; color: #b8b8b8; }")
		: QStringLiteral("QLabel { background: #ececec; color: #303030; }"));
	lm::applyToolbarTheme(this);
	for (TableSideModel *model : m_models)
		model->refresh(model->columnCount());
	fitRowHeights();
}

bool TableCompareView::compare(const QStringList &paths, QString *error)
{
	if (paths.size() != 2 && paths.size() != 3)
	{
		if (error != nullptr)
			*error = tr("a table comparison takes two or three files");
		return false;
	}
	// each side by the Table options' group its file is in (WinMerge's
	// SetTableProperties), as the files are opened or opened again
	const QList<lm::TableProps> props = lm::tablePropertiesOfSides(paths);
	for (int side = 0; side < paths.size(); ++side)
		if (!loadSide(side, paths.at(side), props.at(side), error))
			return false;
	// a third side only for three files (the grid's column with it)
	if (m_paneCount != paths.size())
	{
		m_paneCount = static_cast<int>(paths.size());
		m_columns[2]->setVisible(m_paneCount == 3);
		if (m_paneCount == 2)
		{
			m_sides[2] = Side();
			m_descriptions[2].clear();
		}
		m_activeSide = qMin(m_activeSide, m_paneCount - 1);
	}
	clearHistory();

	for (int side = 0; side < m_paneCount; ++side)
	{
		m_sides[side].props = props.at(side);
		m_sides[side].cells.clear();
		for (const QString &line : m_sides[side].rawLines)
			m_sides[side].cells.push_back(splitRow(line, m_sides[side].props));
	}

	suggestIgnoreEol();
	if (!runDiff(error))
		return false;
	m_current = -1;
	rebuildModel();
	updateStatus();
	return true;
}

bool TableCompareView::loadSide(int side, const QString &path,
	const lm::TableProps &props, QString *error)
{
	// an empty path is an untitled, empty side, as upstream opens the side
	// an item of a folder comparison is not on
	if (path.isEmpty())
	{
		Side &s = m_sides[side];
		s = Side();
		s.unicoding = ucr::UTF8;
		// upstream's InitNew: one line, without a line ending
		s.hadFinalEol = false;
		return true;
	}
	// taken before the content: a change made while reading shows later
	const lm::FileStamp stamp = lm::fileStamp(path);
	UniMemFile file;
	if (!file.OpenReadOnly(path.toStdString()))
	{
		if (error != nullptr)
			*error = tr("cannot open %1").arg(path);
		return false;
	}
	file.ReadBom();

	Side &s = m_sides[side];
	s.path = path;
	s.stamp = stamp;
	s.unicoding = file.GetUnicoding();
	s.codepage = file.GetCodepage();
	s.bom = file.HasBom();
	s.rawLines.clear();
	s.rowEols.clear();
	s.cells.clear();

	int crlf = 0, lf = 0, cr = 0;
	String line, eol;
	bool lossy = false;
	bool lastHadEol = true;
	// CCrystalTextBuffer::JoinLinesForTableEditingMode: while a quote is
	// open at the end of a line, the next line goes on the same row, the
	// line break kept in it
	const bool joinLines = props.allowNewlinesInQuotes && !props.quote.isNull();
	bool inQuote = false;
	QString pendingEol;
	while (file.ReadString(line, eol, &lossy))
	{
		const QString text =
			QString::fromUtf8(line.data(), static_cast<int>(line.size()));
		// a row ends as its last line does
		if (inQuote)
		{
			s.rawLines.last().append(pendingEol + text);
			s.rowEols.last() = QString::fromStdString(eol);
		}
		else
		{
			s.rawLines.append(text);
			s.rowEols.append(QString::fromStdString(eol));
		}
		if (joinLines)
		{
			for (const QChar c : text)
				if (c == props.quote)
					inQuote = !inQuote;
			pendingEol = QString::fromStdString(eol);
		}
		if (eol == "\r\n") ++crlf;
		else if (eol == "\n") ++lf;
		else if (eol == "\r") ++cr;
		lastHadEol = !eol.empty();
	}
	file.Close();

	s.hadFinalEol = s.rawLines.isEmpty() ? false : lastHadEol;
	s.mixedEol = (crlf > 0) + (lf > 0) + (cr > 0) > 1;
	if (crlf >= lf && crlf >= cr && crlf > 0)
		s.eol = QStringLiteral("\r\n");
	else if (cr > lf)
		s.eol = QStringLiteral("\r");
	else
		s.eol = QStringLiteral("\n");
	s.modified = false;
	return true;
}

QString TableCompareView::rowEol(int side, int row) const
{
	// its own in a file of mixed endings, and in every file with
	// Preserve original EOL on
	const Side &s = m_sides[side];
	if ((s.mixedEol || lm::allowMixedEol()) && row >= 0 && row < s.rowEols.size()
		&& !s.rowEols.at(row).isEmpty())
		return s.rowEols.at(row);
	return s.eol;
}

/** As in the text compare (FileCompareView::suggestIgnoreEol): with
    neither "Ignore carriage return differences" nor "Preserve original
    EOL" on, files that end their lines in different ways bring up
    upstream's question first. */
void TableCompareView::suggestIgnoreEol()
{
	if (m_ignoreEol || lm::currentDiffOptions().bIgnoreEol || lm::allowMixedEol())
		return;
	const auto style = [this](int side) {
		return m_sides[side].mixedEol ? QStringLiteral("mixed") : m_sides[side].eol;
	};
	bool differ = false;
	for (int side = 1; side < m_paneCount; ++side)
		differ = differ || style(side) != style(0);
	bool remembered = false;
	if (!differ || !lm::askSuggestIgnoreEol(this, &remembered))
		return;
	m_ignoreEol = true;
	if (remembered)
		if (COptionsMgr *mgr = GetOptionsMgr())
			mgr->SaveOption(OPT_CMP_IGNORE_EOL, true);
}

bool TableCompareView::runDiff(QString *error)
{
	QTemporaryFile temp[3];
	PathContext paths;
	paths.SetSize(m_paneCount);
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (!temp[i].open())
		{
			if (error != nullptr)
				*error = tr("cannot create temporary file");
			return false;
		}
		// every row goes with its line ending, as the text compare hands
		// its lines (the last row's only when the file ends with one); the
		// line breaks inside a row go escaped, so that a row stays one line
		// (DiffTextBuffer's SaveToFile for a table's temporary file)
		const Side &s = m_sides[i];
		QStringList rows = s.rawLines;
		if (m_sides[i].props.allowNewlinesInQuotes)
			for (QString &row : rows)
			{
				row.replace(QChar(0x1b), QStringLiteral("\x1b\x1b"));
				row.replace(QLatin1Char('\r'), QStringLiteral("\x1br"));
				row.replace(QLatin1Char('\n'), QStringLiteral("\x1bn"));
			}
		QByteArray bytes;
		for (int k = 0; k < rows.size(); ++k)
		{
			bytes += rows.at(k).toUtf8();
			if (k + 1 < rows.size() || s.hadFinalEol)
				bytes += rowEol(i, k).toUtf8();
		}
		temp[i].write(bytes);
		temp[i].flush();
		paths.SetPath(i, temp[i].fileName().toStdString(), false);
	}

	CDiffWrapper wrapper;
	DIFFOPTIONS options = lm::currentDiffOptions();
	if (m_ignoreEol)
		options.bIgnoreEol = true;
	DiffList diffList;
	wrapper.SetCreateDiffList(&diffList);
	// CMergeDoc::Rescan: the plugins as the options have them, over the
	// document's own temporary files, which the prediffer may overwrite;
	// its line expressions read each side by that side's table layout
	wrapper.EnablePlugins(GetOptionsMgr()->GetBool(OPT_PLUGINS_ENABLED));
	wrapper.SetPaths(paths, true);
	const PrediffingInfo prediffer(m_prediffPipeline.toStdString());
	wrapper.SetPrediffer(&prediffer);
	for (int i = 0; i < m_paneCount; ++i)
		wrapper.SetTableProps(i, lm::engineTableProps(m_sides[i].props));
	wrapper.SetOptions(&options);
	// the line and substitution filters, and the language "Ignore comment
	// differences" goes by, as in the text compare (WinMerge's table is the
	// same document)
	if (auto filterList = lm::currentLineFilters())
		wrapper.SetFilterList(filterList);
	if (auto substitutions = lm::currentSubstitutionFilters())
		wrapper.SetSubstitutionList(substitutions);
	wrapper.SetFilterCommentsSourceDef(
		QFileInfo(m_sides[0].path).suffix().toLower().toStdString());
	if (!wrapper.RunFileDiff())
	{
		if (error != nullptr)
			*error = tr("the diff engine failed");
		return false;
	}

	m_blocks.clear();
	m_diffCount = 0;
	for (int i = 0; i < diffList.GetSize(); ++i)
	{
		DIFFRANGE dr;
		diffList.GetDiff(i, dr);
		Block block{};
		for (int side = 0; side < m_paneCount; ++side)
		{
			block.begin[side] = dr.begin[side];
			block.end[side] = dr.end[side];
		}
		block.trivial = (dr.op == OP_TRIVIAL);
		block.op = dr.op;
		if (!block.trivial)
			++m_diffCount;
		m_blocks.push_back(block);
	}
	computeCellDiffs();
	return true;
}

QList<int> TableCompareView::diffRowsForTest() const
{
	QList<int> rows;
	for (const Block &block : m_blocks)
		if (!block.trivial)
			for (int row = block.viewBegin; row <= block.viewEnd; ++row)
				rows.append(row);
	return rows;
}

void TableCompareView::rebuildModel()
{
	// ghost-aligned view rows, same layout rules as the text panes
	for (QList<int> &rows : m_viewToReal)
		rows.clear();
	int realPos[3] = {};
	int viewPos = 0;
	for (Block &block : m_blocks)
	{
		const int commonLen = qMax(0, block.begin[0] - realPos[0]);
		for (int side = 0; side < m_paneCount; ++side)
			for (int k = 0; k < commonLen
				&& realPos[side] < m_sides[side].rawLines.size(); ++k)
				m_viewToReal[side].append(realPos[side]++);
		viewPos += commonLen;

		int maxLen = 0;
		for (int side = 0; side < m_paneCount; ++side)
			maxLen = qMax(maxLen, block.end[side] - block.begin[side] + 1);
		block.viewBegin = viewPos;
		block.viewEnd = viewPos + maxLen - 1;
		for (int side = 0; side < m_paneCount; ++side)
		{
			const int len = qMax(0, block.end[side] - block.begin[side] + 1);
			for (int k = 0; k < len; ++k)
				m_viewToReal[side].append(realPos[side]++);
			for (int k = len; k < maxLen; ++k)
				m_viewToReal[side].append(-1);
		}
		viewPos += maxLen;
	}
	int totalRows = 0;
	for (int side = 0; side < m_paneCount; ++side)
	{
		while (realPos[side] < m_sides[side].rawLines.size())
			m_viewToReal[side].append(realPos[side]++);
		totalRows = qMax(totalRows, static_cast<int>(m_viewToReal[side].size()));
	}
	for (int side = 0; side < m_paneCount; ++side)
		while (m_viewToReal[side].size() < totalRows)
			m_viewToReal[side].append(-1);

	// upstream's Rescan hides the lines once the buffers are primed; the
	// header's row is shown as the header whatever the filter makes of it
	hideLines();
	m_modelRows.clear();
	for (int row = m_firstRowIsHeader ? 1 : 0; row < totalRows; ++row)
		if (row >= m_hiddenRows.size() || !m_hiddenRows.at(row))
			m_modelRows.append(row);

	int columns = 1;
	for (int side = 0; side < m_paneCount; ++side)
		for (const QStringList &row : m_sides[side].cells)
			columns = qMax(columns, static_cast<int>(row.size()));
	for (int side = 0; side < m_paneCount; ++side)
		m_models[side]->refresh(columns);
	fitRowHeights();
}

/** Upstream's table draws the line breaks a quoted cell keeps as lines of
    its row: such a row is as tall as its cell of the most lines, on both
    sides, so that the rows stay side by side. */
void TableCompareView::fitRowHeights()
{
	const int base = m_tables[0]->verticalHeader()->defaultSectionSize();
	const int lineSpacing = m_tables[0]->fontMetrics().lineSpacing();
	for (int modelRow = 0; modelRow < m_modelRows.size(); ++modelRow)
	{
		const int viewRow = m_modelRows.at(modelRow);
		int lines = 1;
		for (int side = 0; side < m_paneCount; ++side)
		{
			const int real = m_viewToReal[side].value(viewRow, -1);
			if (real < 0 || real >= static_cast<int>(m_sides[side].cells.size()))
				continue;
			const QString &row = m_sides[side].rawLines.value(real);
			if (!row.contains(QLatin1Char('\n')) && !row.contains(QLatin1Char('\r')))
				continue;
			for (const QString &cell : m_sides[side].cells[real])
				lines = qMax(lines,
					static_cast<int>(displayText(cell).count(QLatin1Char('\n'))) + 1);
		}
		if (lines > 1)
			for (int side = 0; side < m_paneCount; ++side)
				m_tables[side]->verticalHeader()->resizeSection(modelRow,
					base + (lines - 1) * lineSpacing);
	}
}

/** CMergeDoc::HideLines, its display filter part: a row the filter's
    expression does not hold for is hidden on both sides. Nothing is
    hidden without a filter, or by one that does not parse. */
void TableCompareView::hideLines()
{
	m_hiddenRows.clear();
	if (!lm::lineFilterHides(m_displayFilter.get()))
		return;
	QList<lm::ComparedFile> files;
	for (int pane = 0; pane < m_paneCount; ++pane)
	{
		const Side &side = m_sides[pane];
		files.append({ side.path, side.unicoding, side.codepage, side.bom });
	}
	int ignored = 0;
	for (const Block &block : m_blocks)
		if (block.trivial)
			++ignored;
	m_hiddenRows = lm::linesHiddenByFilter(m_displayFilter.get(), LineProvider(this), files,
		m_diffCount, ignored);
}

/** A shown row with a hidden one right after it. */
bool TableCompareView::endsShownStretch(int viewRow) const
{
	return viewRow >= 0 && viewRow + 1 < m_hiddenRows.size() && !m_hiddenRows.at(viewRow)
		&& m_hiddenRows.at(viewRow + 1);
}

/** CMergeEditView::IsDiffFiltered: every row of the difference is
    hidden. */
bool TableCompareView::blockFiltered(int blockIndex) const
{
	if (m_hiddenRows.isEmpty())
		return false;
	const Block &block = m_blocks[blockIndex];
	for (int row = block.viewBegin; row <= block.viewEnd; ++row)
		if (row >= m_hiddenRows.size() || !m_hiddenRows.at(row))
			return false;
	return true;
}

/** CMergeDoc::HasInvisibleLines over every difference there is to copy:
    one of them has a hidden row. */
bool TableCompareView::hasInvisibleLines() const
{
	for (const Block &block : m_blocks)
	{
		if (block.trivial)
			continue;
		for (int row = block.viewBegin; row <= block.viewEnd; ++row)
			if (row < m_hiddenRows.size() && m_hiddenRows.at(row))
				return true;
	}
	return false;
}

/** The grid's row for a view row: its own when it shows, else the first
    one that shows after it, or the last. */
int TableCompareView::modelRowOf(int viewRow) const
{
	if (m_modelRows.isEmpty())
		return -1;
	const auto at = std::lower_bound(m_modelRows.cbegin(), m_modelRows.cend(), viewRow);
	return at == m_modelRows.cend() ? static_cast<int>(m_modelRows.size()) - 1
		: static_cast<int>(at - m_modelRows.cbegin());
}

QStringList TableCompareView::shownRowsForTest(int side) const
{
	QStringList rows;
	for (const int viewRow : m_modelRows)
	{
		const int real = m_viewToReal[side].value(viewRow, -1);
		rows.append(real >= 0 && real < m_sides[side].rawLines.size()
			? m_sides[side].rawLines.at(real) : QString());
	}
	return rows;
}

bool TableCompareView::cellDiffersForTest(int side, int modelRow, int column) const
{
	if (side < 0 || side >= m_paneCount)
		return false;
	const int real = m_viewToReal[side].value(m_modelRows.value(modelRow, -1), -1);
	return real >= 0 && m_cellDiffs[side].contains(cellKey(real, column));
}

QString TableCompareView::displayFilter() const
{
	return QString::fromStdString(m_displayFilter->GetStringOrExpression());
}

void TableCompareView::ensureFilterBar()
{
	if (m_filterBar != nullptr)
		return;
	m_filterBar = new DisplayFilterBar(DisplayFilterBar::Lines, this);
	// under the toolbar, above the grids
	m_layout->insertWidget(1, m_filterBar);
	connect(m_filterBar, &DisplayFilterBar::applyRequested, this,
		&TableCompareView::applyDisplayFilter);
	connect(m_filterBar, &DisplayFilterBar::closeRequested, this,
		&TableCompareView::closeDisplayFilterBar);
}

/** CMergeEditFrame::HideFilterBar: the bar goes away; the filter it
    applied stays in use. */
void TableCompareView::hideFilterBar()
{
	if (m_filterBar == nullptr)
		return;
	m_filterBar->hide();
	m_filterBar->deleteLater(); // it may be the one asking
	m_filterBar = nullptr;
}

/** CMergeEditFrame::OnDisplayFilterBarClose. */
void TableCompareView::closeDisplayFilterBar()
{
	hideFilterBar();
	m_tables[activeSide()]->setFocus();
}

void TableCompareView::showDisplayFilterBar()
{
	ensureFilterBar();
	if (!displayFilter().isEmpty())
		m_filterBar->setFilterText(displayFilter());
	m_filterBar->focusField();
}

void TableCompareView::toggleDisplayFilterBar()
{
	if (m_filterBar == nullptr)
		ensureFilterBar();
	else
		hideFilterBar();
}

void TableCompareView::applyDisplayFilter()
{
	if (m_filterBar == nullptr)
		return;
	QGuiApplication::setOverrideCursor(Qt::WaitCursor);
	// the text as it is typed: saving it reads the list again, which puts
	// its latest entry in the field
	const QString text = m_filterBar->filterText();
	m_filterBar->saveFilterText();
	m_displayFilter->SetStringOrExpression(text.toStdString());
	if (!text.isEmpty()
		&& m_displayFilter->GetFilterExpression().errorCode == FILTER_ERROR_NO_ERROR)
		m_filterBar->setFilterApplied(true);
	// FlushAndRescan(true): the comparison runs again, and hides the rows
	const int side = activeSide();
	recompare();
	m_tables[side]->setFocus();
	QGuiApplication::restoreOverrideCursor();
}

void TableCompareView::buildHeaderMenu(int side, int column, QMenu *menu)
{
	QAction *headers = menu->addAction(tr("Use First Line as Headers"));
	headers->setObjectName(QStringLiteral("useFirstLineAsHeaders"));
	headers->setCheckable(true);
	headers->setChecked(m_firstRowIsHeader);
	connect(headers, &QAction::triggered, m_actHeader, &QAction::setChecked);
	QAction *fit = menu->addAction(tr("Auto-Fit All Columns"));
	fit->setObjectName(QStringLiteral("autoFitAllColumns"));
	connect(fit, &QAction::triggered, this, &TableCompareView::autoFitColumns);
	menu->addSeparator();
	QMenu *ignore = menu->addMenu(tr("&Ignore This Column"));
	ignore->setObjectName(QStringLiteral("ignoreColumn"));
	// OnUpdateIgnoreColumnInComparison: plugins on and a column clicked, in
	// a table the engine can split (its delimiter and quote in ASCII)
	const bool canIgnore = GetOptionsMgr()->GetBool(OPT_PLUGINS_ENABLED) && column >= 0
		&& lm::engineTableProps(m_sides[side].props).istable;
	// GetKeyState(VK_CONTROL) as the command runs: the key held on the
	// click (Qt's Ctrl, the Command key on macOS) adds the column
	const auto adding = []() {
		return QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
	};
	QAction *allPanes = ignore->addAction(tr("In &All Panes"));
	allPanes->setObjectName(QStringLiteral("ignoreColumnInAllPanes"));
	allPanes->setEnabled(canIgnore);
	connect(allPanes, &QAction::triggered, this, [this, column, adding]() {
		ignoreColumnInComparison(column, QString(), adding());
	});
	QAction *thisPane = ignore->addAction(tr("In &This Pane Only (Column Not in Other Files)"));
	thisPane->setObjectName(QStringLiteral("ignoreColumnInThisPane"));
	thisPane->setEnabled(canIgnore);
	connect(thisPane, &QAction::triggered, this, [this, side, column, adding]() {
		ignoreColumnInComparison(column, QString::number(side + 1), adding());
	});
#ifdef Q_OS_MACOS
	QAction *hint = ignore->addAction(tr("(\u2318+Click to add another column)"));
#else
	QAction *hint = ignore->addAction(tr("(Ctrl+Click to add another column)"));
#endif
	hint->setObjectName(QStringLiteral("ignoreColumnHint"));
	hint->setEnabled(false);
	ignore->addSeparator();
	QAction *reset = ignore->addAction(tr("&Reset All"));
	reset->setObjectName(QStringLiteral("ignoreColumnResetAll"));
	connect(reset, &QAction::triggered, this, &TableCompareView::resetIgnoredColumnsInComparison);
	QMenu *filter = menu->addMenu(tr("&Filter by This Column"));
	const struct { const char *text; const char *name; } kinds[] = {
		{ QT_TR_NOOP("&Text..."), "filterColumnText" },
		{ QT_TR_NOOP("&Number..."), "filterColumnNumber" },
		{ QT_TR_NOOP("&Date/Time..."), "filterColumnDateTime" },
	};
	for (int kind = 0; kind < 3; ++kind)
	{
		QAction *action = filter->addAction(tr(kinds[kind].text));
		action->setObjectName(QLatin1String(kinds[kind].name));
		connect(action, &QAction::triggered, this,
			[this, side, column, kind]() { addColumnToDisplayFilter(side, column, kind); });
	}
}

void TableCompareView::addColumnToDisplayFilter(int side, int column, int dataType)
{
	if (column < 0 || dataType < 0 || dataType > 2)
		return; // (CMergeEditView::OnFilterMenuColumn: no column was clicked)
	// a menu that is asked without being shown: the side's own columns,
	// joined with AND
	LineFilterMenu menu(this);
	// upstream's sides: 1 left, 2 middle, 3 right
	menu.setTarget(side == m_paneCount - 1 ? 3 : side + 1, 0, column);
	const std::optional<QString> filter =
		menu.apply(LineFilterMenu::ColumnText + dataType, displayFilter());
	if (!filter.has_value())
		return;
	ensureFilterBar();
	m_displayFilter->SetStringOrExpression(filter->toStdString());
	m_filterBar->setFilterText(displayFilter());
	applyDisplayFilter();
}

void TableCompareView::ignoreColumnInComparison(int column, const QString &panePrefix, bool add)
{
	String errorMessage;
	auto expressions = PluginForFile::ParsePluginPipeline(m_prediffPipeline.toStdString(), errorMessage);
	if (!errorMessage.empty())
	{
		RootLogger::Error(errorMessage);
		return;
	}

	uint8_t targetFlags = 0xff;
	if (!panePrefix.isEmpty())
	{
		targetFlags = 0;
		for (const QChar ch : panePrefix)
		{
			if (ch >= QLatin1Char('1') && ch <= QLatin1Char('3'))
				targetFlags |= 1 << (ch.unicode() - '1');
		}
	}

	const String expressionPrefix = _T("ColumnRange(\"");
	const String columnNumber = strutils::format(_T("%d"), column + 1);
	const String columnSuffix = _T("\")");

	bool found = false;
	for (auto &pipelineItem : expressions)
	{
		auto *lineExpression = std::get_if<PluginForFile::FilterExpressionPipelineItem>(&pipelineItem);
		if (!lineExpression || lineExpression->targetFlags != targetFlags ||
			lineExpression->expression.compare(0, expressionPrefix.size(), expressionPrefix) != 0 ||
			lineExpression->expression.size() < expressionPrefix.size() + columnSuffix.size() ||
			lineExpression->expression.compare(lineExpression->expression.size() - columnSuffix.size(),
				columnSuffix.size(), columnSuffix) != 0)
			continue;
		// only the items this makes (their specification starts with '!')
		const size_t checkBegin = expressionPrefix.size();
		if (lineExpression->expression.size() <= checkBegin ||
			lineExpression->expression[checkBegin] != _T('!'))
			continue;
		found = true;
		const size_t specBegin = expressionPrefix.size();
		const size_t specLength = lineExpression->expression.size() - specBegin - columnSuffix.size();
		String specification = lineExpression->expression.substr(specBegin, specLength);
		if (add)
		{
			if (specification == _T("!"))
				specification += columnNumber;
			else if (specification != _T("!") + columnNumber)
			{
				specification += _T(",!");
				specification += columnNumber;
			}
		}
		else
			specification = _T("!") + columnNumber;
		lineExpression->expression = expressionPrefix + specification + columnSuffix;
		break;
	}
	if (!found)
	{
		expressions.push_back(PluginForFile::FilterExpressionPipelineItem{
			targetFlags, expressionPrefix + _T("!") + columnNumber + columnSuffix });
	}

	m_prediffPipeline = QString::fromStdString(PluginForFile::MakePluginPipeline(expressions));
	// FlushAndRescan(true)
	recompare();
}

void TableCompareView::resetIgnoredColumnsInComparison()
{
	String errorMessage;
	const auto expressions = PluginForFile::ParsePluginPipeline(m_prediffPipeline.toStdString(), errorMessage);
	if (!errorMessage.empty())
	{
		RootLogger::Error(errorMessage);
		return;
	}
	std::vector<PluginForFile::PipelineItem> updatedExpressions;
	for (const auto &pipelineItem : expressions)
	{
		const auto *lineExpression = std::get_if<PluginForFile::FilterExpressionPipelineItem>(&pipelineItem);
		const String ignorePrefix = _T("ColumnRange(\"!");
		if (lineExpression &&
			lineExpression->expression.compare(0, ignorePrefix.size(), ignorePrefix) == 0 &&
			lineExpression->expression.size() >= ignorePrefix.size() + 2 &&
			lineExpression->expression.compare(lineExpression->expression.size() - 2, 2, _T("\")")) == 0)
			continue;
		updatedExpressions.push_back(pipelineItem);
	}
	m_prediffPipeline = QString::fromStdString(PluginForFile::MakePluginPipeline(updatedExpressions));
	recompare();
}

void TableCompareView::autoFitColumns()
{
	// upstream measures every buffer's cells for one set of widths
	for (int side = 0; side < m_paneCount; ++side)
		m_tables[side]->resizeColumnsToContents();
	const int columns = m_models[0]->columnCount();
	for (int column = 0; column < columns; ++column)
	{
		int width = 0;
		for (int side = 0; side < m_paneCount; ++side)
			width = qMax(width, m_tables[side]->columnWidth(column));
		for (int side = 0; side < m_paneCount; ++side)
			m_tables[side]->setColumnWidth(column, width);
	}
}

/** Cells that differ between the paired rows of each difference: with
    three sides, a cell that differs from the same cell of any other side
    that has the row. */
void TableCompareView::computeCellDiffs()
{
	for (QSet<quint64> &cells : m_cellDiffs)
		cells.clear();
	for (const Block &block : m_blocks)
	{
		if (block.trivial)
			continue;
		int rows = 0;
		for (int side = 0; side < m_paneCount; ++side)
			rows = qMax(rows, block.end[side] - block.begin[side] + 1);
		for (int k = 0; k < rows; ++k)
		{
			// the sides that have the k-th row of the difference
			const QStringList *cells[3] = {};
			int real[3] = { -1, -1, -1 };
			for (int side = 0; side < m_paneCount; ++side)
			{
				const int row = block.begin[side] + k;
				if (row <= block.end[side] && row >= 0
					&& row < static_cast<int>(m_sides[side].cells.size()))
				{
					real[side] = row;
					cells[side] = &m_sides[side].cells[row];
				}
			}
			for (int a = 0; a < m_paneCount; ++a)
			{
				for (int b = a + 1; b < m_paneCount; ++b)
				{
					if (cells[a] == nullptr || cells[b] == nullptr)
						continue;
					const int columns = qMax(cells[a]->size(), cells[b]->size());
					for (int col = 0; col < columns; ++col)
					{
						if (cells[a]->value(col) == cells[b]->value(col))
							continue;
						m_cellDiffs[a].insert(cellKey(real[a], col));
						m_cellDiffs[b].insert(cellKey(real[b], col));
					}
				}
			}
		}
	}
}

void TableCompareView::updateStatus()
{
	QString text;
	if (m_diffCount == 0)
		text = tr("Files are identical");
	else if (m_current >= 0)
	{
		int index = 0;
		for (int b = 0; b <= m_current
			&& b < static_cast<int>(m_blocks.size()); ++b)
			if (!m_blocks[b].trivial)
				++index;
		text = tr("Difference %1 of %2").arg(index).arg(m_diffCount);
	}
	else
		text = tr("%n difference(s) found", nullptr, m_diffCount);
	// each side's delimiter when the Table options give them different ones
	const auto delimiterName = [this](int side) {
		const QChar delimiter = m_sides[side].props.delimiter;
		return delimiter == QLatin1Char('\t') ? tr("tab") : QString(delimiter);
	};
	bool sameDelimiter = true;
	for (int side = 1; side < m_paneCount; ++side)
		sameDelimiter = sameDelimiter
			&& m_sides[side].props.delimiter == m_sides[0].props.delimiter;
	QStringList delimiters{ delimiterName(0) };
	for (int side = 1; !sameDelimiter && side < m_paneCount; ++side)
		delimiters.append(delimiterName(side));
	text += tr("  \xE2\x80\xA2  delimiter: %1").arg(delimiters.join(QStringLiteral(" | ")));
	if (isModified())
		text += tr("  \xE2\x80\xA2 unsaved changes");
	m_status->setText(text);
	for (int side = 0; side < m_paneCount; ++side)
		updateHeader(side);
}

int TableCompareView::nextActive(int from, int direction) const
{
	// (not a difference the display filter hides whole: upstream's
	// Find...NonFilteredDiff)
	for (int b = from + direction;
		b >= 0 && b < static_cast<int>(m_blocks.size()); b += direction)
		if (!m_blocks[b].trivial && !blockFiltered(b))
			return b;
	return -1;
}

void TableCompareView::gotoDiff(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(m_blocks.size()))
		return;
	m_current = blockIndex;
	const int modelRow = modelRowOf(m_blocks[blockIndex].viewBegin);
	for (int side = 0; side < m_paneCount; ++side)
		m_models[side]->refresh(m_models[side]->columnCount());
	// the rows as tall as their cells again before the scroll is judged
	fitRowHeights();
	if (modelRow >= 0)
		scrollDiffIntoView(blockIndex);
	// upstream's SelectDiff puts the cursor at the start of the difference,
	// nothing selected
	for (int i = 0; i < m_paneCount && modelRow >= 0; ++i)
	{
		m_tables[i]->selectionModel()->clearSelection();
		m_tables[i]->selectionModel()->setCurrentIndex(
			m_models[i]->index(modelRow, 0), QItemSelectionModel::NoUpdate);
	}
	updateStatus();
}

/** CMergeEditView::ShowDiff's scroll, as in the file comparison (the table
    is WinMerge's text view in table mode): none while the difference
    shows whole with CONTEXT_LINES_BELOW rows to spare under it, otherwise
    its first row goes CONTEXT_LINES_ABOVE rows below the top (issue #10);
    then EnsureVisible brings its first column in. */
void TableCompareView::scrollDiffIntoView(int blockIndex)
{
	constexpr int contextLinesAbove = 5;
	constexpr int contextLinesBelow = 3;
	const Block &block = m_blocks[blockIndex];
	const QTableView *table = m_tables[activeSide()];
	// upstream counts lines on screen, a row whose cells have several
	// lines taking as many (its sublines): in pixels here, by the rows'
	// heights
	const int line = qMax(1, table->verticalHeader()->defaultSectionSize());
	const int screen = table->viewport()->height() / line * line;
	const int start = modelRowOf(block.viewBegin);
	const int end = modelRowOf(qMax(block.viewBegin, block.viewEnd));
	const int startY = table->rowViewportPosition(start);
	const int endY = table->rowViewportPosition(end) + table->rowHeight(end);
	// upstream's height, the last line's sublines plus one: a line more
	// than the difference has
	const int height = endY - startY + line;
	if (startY < 0 || endY > screen - contextLinesBelow * line || height >= screen)
	{
		// CONTEXT_LINES_ABOVE lines over its first one, when it has more
		int row = start;
		if (startY - table->rowViewportPosition(0) > contextLinesAbove * line)
			for (int above = 0; row > 0 && above < contextLinesAbove * line;)
				above += table->rowHeight(--row);
		for (int i = 0; i < m_paneCount; ++i)
			m_tables[i]->scrollTo(m_models[i]->index(row, 0),
				QAbstractItemView::PositionAtTop);
	}
	for (int i = 0; i < m_paneCount; ++i)
		m_tables[i]->scrollTo(m_models[i]->index(start, 0),
			QAbstractItemView::EnsureVisible);
}

int TableCompareView::activeSide() const
{
	for (int side = 0; side < m_paneCount; ++side)
		if (m_tables[side]->hasFocus())
			return side;
	return qMin(m_activeSide, m_paneCount - 1);
}

void TableCompareView::setActiveSide(int side)
{
	m_activeSide = side;
	for (int i = 0; i < m_paneCount; ++i)
		m_headers[i]->setActive(i == side);
}

void TableCompareView::updateHeader(int side)
{
	// upstream's UpdateHeaderPath: the description or the path, given to
	// the bar only when it changes
	QString caption = m_descriptions[side].isEmpty() ? m_sides[side].path : m_descriptions[side];
	if (caption.isEmpty())
		caption = untitledName(side);
	const QString text = (m_sides[side].modified ? QStringLiteral("* ") : QString()) + caption;
	m_headers[side]->setPath(m_sides[side].path);
	m_headers[side]->setActive(side == m_activeSide);
	if (text == m_headerTexts[side])
		return;
	m_headerTexts[side] = text;
	m_headers[side]->setOriginalText(text);
}

/** Upstream's ChangeFile (the table being its text window): both files
    are opened again, the new one on its side with its description, then
    MoveOnLoad. */
bool TableCompareView::changeFile(int side, const QString &path, const QString &description)
{
	if (side < 0 || side >= m_paneCount || path.isEmpty())
		return false;
	// PromptAndSaveIfNeeded
	if (m_savePrompt && !m_savePrompt())
		return false;
	QStringList files = paths();
	files[side] = path;
	// refuse before touching anything, as reload does
	for (const QString &file : files)
	{
		QFile probe(file);
		if (!file.isEmpty() && !probe.open(QIODevice::ReadOnly))
		{
			lm::showError(this, tr("cannot open %1").arg(file));
			return false;
		}
	}
	const bool wasModified = isModified();
	QString error;
	if (!compare(files, &error))
	{
		lm::showError(this, error);
		return false;
	}
	m_descriptions[side] = description;
	updateHeader(side);
	m_actSave->setEnabled(isModified());
	if (wasModified != isModified())
		emit modifiedChanged(isModified());

	// MoveOnLoad: the first difference when the option asks, the first
	// row otherwise
	if (OptionsDialog::scrollToFirstDiff() && m_diffCount > 0)
	{
		gotoFirstDiff();
	}
	else if (m_models[m_activeSide]->rowCount() > 0)
	{
		const QModelIndex first = m_models[m_activeSide]->index(0, 0);
		m_tables[m_activeSide]->setCurrentIndex(first);
		m_tables[m_activeSide]->scrollTo(first);
	}

	// OpenDocs tells when the files are identical
	emit rescanned();
	emit pathsChanged();
	return true;
}

QString TableCompareView::untitledName(int side) const
{
	if (side == 0)
		return tr("Untitled Left");
	if (side == 1 && m_paneCount == 3)
		return tr("Untitled Middle");
	return tr("Untitled Right");
}

void TableCompareView::editActivePanePath()
{
	m_headers[m_activeSide]->editPath();
}

/** Upstream's Next/PrevSignificantDiffFromLine, past the differences
    nextActive skips: the first to stop at that starts at the view row or
    after it (direction +1), or ends at it or before it (-1). */
int TableCompareView::nextActiveFromRow(int viewRow, int direction) const
{
	const int count = static_cast<int>(m_blocks.size());
	for (int b = direction > 0 ? 0 : count - 1; b >= 0 && b < count; b += direction)
	{
		const Block &block = m_blocks[b];
		if (block.trivial || blockFiltered(b))
			continue;
		if (direction > 0 ? block.viewBegin >= viewRow : block.viewEnd <= viewRow)
			return b;
	}
	return -1;
}

/** Upstream's LineToDiff for a significant difference: the one the view
    row is in, -1 when it is in none. */
int TableCompareView::diffAtRow(int viewRow) const
{
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
		if (!m_blocks[b].trivial && viewRow >= m_blocks[b].viewBegin
			&& viewRow <= m_blocks[b].viewEnd)
			return b;
	return -1;
}

/** The view row of the active grid's current cell (the first row before
    there is one), moved by the offset; the first row on screen when that
    is no row (upstream's IsValidTextPosY and m_nTopLine). */
int TableCompareView::searchRow(int offset) const
{
	const QTableView *table = m_tables[activeSide()];
	const QModelIndex index = table->currentIndex();
	const int viewRow = (index.isValid() ? m_modelRows.value(index.row(), 0) : 0) + offset;
	if (viewRow < 0 || viewRow >= m_viewToReal[0].size())
		return m_modelRows.value(qMax(0, table->rowAt(0)), 0);
	return viewRow;
}

/** Upstream's IsDiffVisible on the active grid: the shown rows of the
    difference all on screen, and fewer than the screen holds. */
bool TableCompareView::diffOnScreen(int blockIndex) const
{
	const Block &block = m_blocks[blockIndex];
	const auto first = std::lower_bound(m_modelRows.cbegin(), m_modelRows.cend(), block.viewBegin);
	const auto last = std::upper_bound(m_modelRows.cbegin(), m_modelRows.cend(), block.viewEnd);
	if (first >= last)
		return false;
	const QTableView *table = m_tables[activeSide()];
	const int lastRow = static_cast<int>(last - m_modelRows.cbegin()) - 1;
	const int top = table->rowViewportPosition(static_cast<int>(first - m_modelRows.cbegin()));
	const int bottom = table->rowViewportPosition(lastRow) + table->rowHeight(lastRow);
	const int screen = table->viewport()->height();
	return top >= 0 && bottom <= screen && bottom - top < screen;
}

/** Upstream's DeselectDiffIfCursorNotInCurrentDiff, on a click: a cell
    outside the current difference leaves none current. */
void TableCompareView::deselectDiffIfCursorNotInCurrentDiff(int modelRow)
{
	if (m_current < 0 || m_current >= static_cast<int>(m_blocks.size()))
		return;
	const int viewRow = m_modelRows.value(modelRow, -1);
	if (viewRow >= m_blocks[m_current].viewBegin && viewRow <= m_blocks[m_current].viewEnd)
		return;
	m_current = -1;
	for (int side = 0; side < m_paneCount; ++side)
		m_models[side]->recolor();
	updateStatus();
}

/** Upstream's OnNextdiff (FindNextNonFilteredDiff): the difference after
    the current one while that one is on screen; otherwise the next from
    the cursor, past the first row of the difference it may be in. */
void TableCompareView::gotoNextDiff()
{
	int next = -1;
	if (m_current >= 0 && diffOnScreen(m_current))
		next = nextActive(m_current, +1);
	else
		next = nextActiveFromRow(searchRow(m_current >= 0 ? 1 : 0), +1);
	if (next >= 0)
		gotoDiff(next);
}

/** Upstream's OnPrevdiff, the same way back. */
void TableCompareView::gotoPrevDiff()
{
	int prev = -1;
	if (m_current >= 0 && diffOnScreen(m_current))
		prev = nextActive(m_current, -1);
	else
		prev = nextActiveFromRow(searchRow(m_current >= 0 ? -1 : 0), -1);
	if (prev >= 0)
		gotoDiff(prev);
}

void TableCompareView::gotoFirstDiff()
{
	const int first = nextActive(-1, +1);
	if (first >= 0)
		gotoDiff(first);
}

void TableCompareView::gotoLastDiff()
{
	const int last = nextActive(static_cast<int>(m_blocks.size()), -1);
	if (last >= 0)
		gotoDiff(last);
}

/** Make the difference under the given model row current (the table
 *  version of WinMerge's LineToDiff + SelectDiff). */
void TableCompareView::selectDiffAtModelRow(int modelRow)
{
	const int viewRow = m_modelRows.value(modelRow, -1);
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
		if (!m_blocks[b].trivial && viewRow >= m_blocks[b].viewBegin
			&& viewRow <= m_blocks[b].viewEnd)
		{
			gotoDiff(b);
			return;
		}
}

void TableCompareView::selectDiffAtCursor()
{
	const QModelIndex index = m_tables[activeSide()]->currentIndex();
	if (index.isValid())
		selectDiffAtModelRow(index.row());
}

TableCompareView::UndoEntry TableCompareView::captureEntry(int side,
	int viewBegin) const
{
	UndoEntry entry { side, m_sides[side].rawLines, m_sides[side].modified,
		m_saveSerial[side], viewBegin };
	entry.eols = m_sides[side].rowEols;
	return entry;
}

/** Swap the given side's live content with the snapshot (WinMerge's
 *  OnEditUndo re-selects the restored difference "so we may just merge
 *  it again"). */
void TableCompareView::applyEntry(const UndoEntry &entry)
{
	Side &side = m_sides[entry.side];
	side.rawLines = entry.lines;
	side.rowEols = entry.eols;
	side.cells.clear();
	for (const QString &line : side.rawLines)
		side.cells.push_back(splitRow(line, side.props));
	// once the side has been saved again the snapshot no longer matches
	// the file on disk, so the side stays marked as modified
	setSideModified(entry.side,
		entry.saveSerial == m_saveSerial[entry.side] ? entry.modified : true);

	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	// an edit goes back to its cell, as upstream's undo of typing does
	if (entry.column >= 0)
	{
		updateStatus();
		showCell(entry.side, entry.realRow, entry.column);
		return;
	}
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
		if (!m_blocks[b].trivial && m_blocks[b].viewEnd >= entry.viewBegin)
		{
			gotoDiff(b);
			return;
		}
	updateStatus();
}

void TableCompareView::pushUndo(const UndoEntry &entry)
{
	m_undoStack.append(entry);
	while (m_undoStack.size() > 200)
		m_undoStack.removeFirst();
	m_redoStack.clear();
	updateUndoActions();
}

void TableCompareView::undo()
{
	if (m_undoStack.isEmpty())
		return;
	const UndoEntry entry = m_undoStack.takeLast();
	UndoEntry current = captureEntry(entry.side, entry.viewBegin);
	current.realRow = entry.realRow;
	current.column = entry.column;
	m_redoStack.append(current);
	applyEntry(entry);
	updateUndoActions();
}

void TableCompareView::redo()
{
	if (m_redoStack.isEmpty())
		return;
	const UndoEntry entry = m_redoStack.takeLast();
	UndoEntry current = captureEntry(entry.side, entry.viewBegin);
	current.realRow = entry.realRow;
	current.column = entry.column;
	m_undoStack.append(current);
	applyEntry(entry);
	updateUndoActions();
}

void TableCompareView::clearHistory()
{
	m_undoStack.clear();
	m_redoStack.clear();
	updateUndoActions();
}

void TableCompareView::updateUndoActions()
{
	m_actUndo->setEnabled(!m_undoStack.isEmpty());
	m_actRedo->setEnabled(!m_redoStack.isEmpty());
}

void TableCompareView::swapSides()
{
	// snapshots refer to sides by index, which a swap would scramble; the
	// first side and the last trade places, as in the file comparison
	clearHistory();
	const int last = m_paneCount - 1;
	std::swap(m_sides[0], m_sides[last]);
	std::swap(m_descriptions[0], m_descriptions[last]);
	std::swap(m_saveSerial[0], m_saveSerial[last]);
	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	updateStatus();
	emit pathsChanged();
}

void TableCompareView::focusNextPane()
{
	m_tables[(activeSide() + 1) % m_paneCount]->setFocus();
}

void TableCompareView::editCell(int side, int modelRow, int column, const QString &text)
{
	if (side < 0 || side >= m_paneCount || column < 0)
		return;
	const int viewRow = m_modelRows.value(modelRow, -1);
	if (viewRow < 0)
		return;
	Side &s = m_sides[side];
	int real = m_viewToReal[side].value(viewRow, -1);
	// a line break typed in a cell goes as the file writes its own
	QString value = text;
	value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	value.replace(QLatin1Char('\n'), s.eol);
	if (real >= 0 && real < s.rawLines.size())
	{
		if (displayText(s.cells[real].value(column)) == displayText(value))
			return;
		const QString &line = s.rawLines.at(real);
		const auto [start, end] = rawCellSpan(line, s.props, column);
		const bool quoted = start >= 0 && start < end && !s.props.quote.isNull()
			&& line.at(start) == s.props.quote;
		UndoEntry entry = captureEntry(side, viewRow);
		entry.realRow = real;
		entry.column = column;
		pushUndo(entry);
		s.rawLines[real] = withCell(line, s.props, column, rawValue(value, s.props, quoted));
		s.cells[real] = splitRow(s.rawLines.at(real), s.props);
	}
	else
	{
		// typing on a filler row makes it a row of the side's file, before
		// the side's rows that come after it
		if (value.isEmpty())
			return;
		int insertAt = 0;
		for (int v = 0; v < viewRow; ++v)
			if (m_viewToReal[side].value(v, -1) >= 0)
				++insertAt;
		UndoEntry entry = captureEntry(side, viewRow);
		entry.realRow = insertAt;
		entry.column = column;
		pushUndo(entry);
		const QString line = withCell(QString(), s.props, column, rawValue(value, s.props, false));
		s.rawLines.insert(insertAt, line);
		s.rowEols.insert(insertAt, s.eol);
		s.cells.insert(s.cells.begin() + insertAt, splitRow(line, s.props));
		real = insertAt;
	}
	setSideModified(side, true);
	// FlushAndRescan: the tables are compared again with the edit
	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	updateStatus();
	showCell(side, real, column);
}

void TableCompareView::showCell(int side, int realRow, int column)
{
	if (side < 0 || side >= m_paneCount)
		return;
	// the first shown row that is the side's row or comes after it
	int modelRow = -1;
	for (int row = 0; row < m_modelRows.size() && modelRow < 0; ++row)
		if (m_viewToReal[side].value(m_modelRows.at(row), -1) >= realRow)
			modelRow = row;
	if (modelRow < 0)
		modelRow = static_cast<int>(m_modelRows.size()) - 1;
	if (modelRow < 0)
		return;
	const QModelIndex index = m_models[side]->index(modelRow, qMax(0, column));
	m_tables[side]->setCurrentIndex(index);
	m_tables[side]->scrollTo(index);
}

bool TableCompareView::copySelectedCells(int sourceSide, int targetSide)
{
	// upstream's copy of the selected word differences, which a column
	// selection makes the selected cells' in its table; one selected cell
	// is the cursor, as a click puts it there, and the difference goes whole
	const QItemSelectionModel *selection = m_tables[activeSide()]->selectionModel();
	const QModelIndexList selected = selection->selectedIndexes();
	if (selected.size() < 2)
		return false;
	const Side &from = m_sides[sourceSide];
	Side &to = m_sides[targetSide];

	// whole rows (their headers clicked): the differences they are in go
	// whole, as upstream copies the differences a selection of lines takes
	// in (CopyMultipleList), each from the bottom up so the rows above keep
	// their places
	bool wholeRows = true;
	for (const QModelIndex &index : selected)
		wholeRows = wholeRows && selection->isRowSelected(index.row(), QModelIndex());
	if (wholeRows)
	{
		std::set<int> blocks;
		for (const QModelIndex &index : selected)
		{
			const int block = diffAtRow(m_modelRows.value(index.row(), -1));
			if (block >= 0)
				blocks.insert(block);
		}
		if (blocks.empty())
			return true;
		pushUndo(captureEntry(targetSide, m_blocks[*blocks.begin()].viewBegin));
		for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
		{
			const Block &block = m_blocks[*it];
			// the rows take their line endings along
			QStringList rows;
			QStringList eols;
			for (int row = block.begin[sourceSide];
				row <= block.end[sourceSide] && row < from.rawLines.size(); ++row)
			{
				rows.append(from.rawLines.at(qMax(0, row)));
				eols.append(rowEol(sourceSide, qMax(0, row)));
			}
			const int first = block.begin[targetSide];
			const int count = qMax(0, block.end[targetSide] - block.begin[targetSide] + 1);
			for (int k = 0; k < count; ++k)
			{
				to.rawLines.removeAt(first);
				to.rowEols.removeAt(first);
			}
			for (int k = 0; k < rows.size(); ++k)
			{
				to.rawLines.insert(first + k, rows.at(k));
				to.rowEols.insert(first + k, eols.at(k));
			}
		}
		to.cells.clear();
		for (const QString &line : to.rawLines)
			to.cells.push_back(splitRow(line, to.props));
		setSideModified(targetSide, true);
		QString error;
		runDiff(&error);
		rebuildModel();
		m_current = -1;
		updateStatus();
		return true;
	}

	bool copied = false;
	for (const QModelIndex &index : selected)
	{
		const int viewRow = m_modelRows.value(index.row(), -1);
		if (viewRow < 0 || diffAtRow(viewRow) < 0)
			continue; // a cell of no difference to copy
		const int fromReal = m_viewToReal[sourceSide].value(viewRow, -1);
		const int toReal = m_viewToReal[targetSide].value(viewRow, -1);
		if (fromReal < 0 || toReal < 0)
			continue; // a row one side lacks goes with the whole difference
		const int column = index.column();
		const QString value = from.cells[fromReal].value(column);
		if (value == to.cells[toReal].value(column))
			continue;
		if (!copied)
		{
			pushUndo(captureEntry(targetSide, viewRow));
			copied = true;
		}
		const QString &fromLine = from.rawLines.at(fromReal);
		const auto [start, end] = rawCellSpan(fromLine, from.props, column);
		const bool quoted = start >= 0 && start < end && !from.props.quote.isNull()
			&& fromLine.at(start) == from.props.quote;
		to.rawLines[toReal] = withCell(to.rawLines.at(toReal), to.props, column,
			rawValue(value, to.props, quoted));
		to.cells[toReal] = splitRow(to.rawLines.at(toReal), to.props);
	}
	if (copied)
	{
		setSideModified(targetSide, true);
		QString error;
		runDiff(&error);
		rebuildModel();
		m_current = -1;
		updateStatus();
	}
	return true;
}

void TableCompareView::copyToRight()
{
	const int target = qMin(activeSide() + 1, m_paneCount - 1);
	copyCurrentDiff(target - 1, target);
}

void TableCompareView::copyToLeft()
{
	const int target = qMax(activeSide() - 1, 0);
	copyCurrentDiff(target + 1, target);
}

void TableCompareView::copyCurrentDiff(int sourceSide, int targetSide)
{
	const int target = targetSide >= 0 ? targetSide : 1 - sourceSide;
	if (sourceSide < 0 || sourceSide >= m_paneCount || target < 0
		|| target >= m_paneCount || target == sourceSide)
		return;
	if (copySelectedCells(sourceSide, target))
		return;
	// upstream's OnX2Y: with no current difference, the one the cursor is
	// in, and none when it is in none
	if (m_current < 0)
	{
		const QModelIndex index = m_tables[activeSide()]->currentIndex();
		if (index.isValid())
			m_current = diffAtRow(m_modelRows.value(index.row(), -1));
	}
	if (m_current < 0 || m_current >= static_cast<int>(m_blocks.size()))
		return;
	const Block block = m_blocks[m_current];
	pushUndo(captureEntry(target, block.viewBegin));

	// the rows take their line endings along
	QStringList newLines;
	QStringList newEols;
	for (int row = block.begin[sourceSide];
		row <= block.end[sourceSide]
			&& row < m_sides[sourceSide].rawLines.size(); ++row)
	{
		newLines.append(m_sides[sourceSide].rawLines.at(qMax(0, row)));
		newEols.append(rowEol(sourceSide, qMax(0, row)));
	}

	QStringList &lines = m_sides[target].rawLines;
	QStringList &eols = m_sides[target].rowEols;
	const int first = block.begin[target];
	const int count = qMax(0, block.end[target] - block.begin[target] + 1);
	for (int k = 0; k < count; ++k)
	{
		lines.removeAt(first);
		eols.removeAt(first);
	}
	for (int k = 0; k < newLines.size(); ++k)
	{
		lines.insert(first + k, newLines.at(k));
		eols.insert(first + k, newEols.at(k));
	}
	m_sides[target].cells.clear();
	for (const QString &line : lines)
		m_sides[target].cells.push_back(splitRow(line, m_sides[target].props));
	setSideModified(target, true);

	const int wasViewBegin = block.viewBegin;
	QString error;
	runDiff(&error);
	rebuildModel();
	// land on the next difference below the merged spot
	m_current = -1;
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
		if (!m_blocks[b].trivial && m_blocks[b].viewBegin >= wasViewBegin
			&& !blockFiltered(b))
		{
			gotoDiff(b);
			return;
		}
	updateStatus();
}

void TableCompareView::copyAllToRight()
{
	if (!lm::askCopyAllDiffs(this))
		return;
	const int target = qMin(activeSide() + 1, m_paneCount - 1);
	copyAllFrom(target - 1, target);
}

void TableCompareView::copyAllToLeft()
{
	if (!lm::askCopyAllDiffs(this))
		return;
	const int target = qMax(activeSide() - 1, 0);
	copyAllFrom(target + 1, target);
}

void TableCompareView::copyAllFrom(int sourceSide, int targetSide)
{
	const int target = targetSide >= 0 ? targetSide : 1 - sourceSide;
	if (sourceSide < 0 || sourceSide >= m_paneCount || target < 0
		|| target >= m_paneCount || target == sourceSide)
		return;
	// CMergeDoc::CopyMultipleList: not with hidden lines among the
	// differences to copy
	if (hasInvisibleLines())
	{
		lm::showError(this, tr("Merging/copying differences that contain hidden lines is "
			"not currently supported.\n\nPlease clear the display filter or adjust the "
			"filter settings to show all lines before merging."));
		return;
	}
	pushUndo(captureEntry(target, 0));
	m_sides[target].rawLines = m_sides[sourceSide].rawLines;
	m_sides[target].rowEols.clear();
	for (int row = 0; row < m_sides[sourceSide].rawLines.size(); ++row)
		m_sides[target].rowEols.append(rowEol(sourceSide, row));
	// the rows read by the target's own delimiter and quote
	m_sides[target].cells.clear();
	for (const QString &line : m_sides[target].rawLines)
		m_sides[target].cells.push_back(splitRow(line, m_sides[target].props));
	setSideModified(target, true);
	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	updateStatus();
}

void TableCompareView::refreshByUser()
{
	// WinMerge's Rescan starts with CheckFileChanged; a Yes there has
	// reloaded the files by the time the comparison runs
	emit aboutToRescan();
	recompare();
	emit rescanned();
}

bool TableCompareView::encodingsDiffer() const
{
	for (int side = 1; side < m_paneCount; ++side)
		if (m_sides[side].unicoding != m_sides[0].unicoding
			|| m_sides[side].codepage != m_sides[0].codepage
			|| m_sides[side].bom != m_sides[0].bom)
			return true;
	return false;
}

QString TableCompareView::changedPathOnDisk() const
{
	for (int side = 0; side < m_paneCount; ++side)
	{
		const Side &s = m_sides[side];
		if (lm::fileChangedOnDisk(s.path, s.stamp) == lm::FileChange::Changed)
			return s.path;
	}
	return QString();
}

bool TableCompareView::reload(QString *error)
{
	// refuse before touching anything: a side already replaced could not
	// be put back (WinMerge closes the comparison at this point instead)
	for (int side = 0; side < m_paneCount; ++side)
	{
		const Side &s = m_sides[side];
		// an untitled side starts over empty
		if (!s.path.isEmpty() && !QFile(s.path).open(QIODevice::ReadOnly))
		{
			if (error != nullptr)
				*error = tr("cannot open %1").arg(s.path);
			return false;
		}
	}

	const bool wasModified = isModified();
	// MoveOnLoad(nActivePane, pt.y): back to the current row afterwards
	const int activeSide = this->activeSide();
	const int currentRow = m_tables[activeSide]->currentIndex().row();
	const bool compared = compare(paths(), error);
	m_actSave->setEnabled(isModified());
	if (wasModified != isModified())
		emit modifiedChanged(isModified());
	if (compared && currentRow >= 0 && m_models[activeSide]->rowCount() > 0)
	{
		const QModelIndex index = m_models[activeSide]->index(
			qMin(currentRow, m_models[activeSide]->rowCount() - 1), 0);
		m_tables[activeSide]->setCurrentIndex(index);
		m_tables[activeSide]->scrollTo(index, QAbstractItemView::PositionAtCenter);
	}
	return compared;
}

void TableCompareView::recompare()
{
	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	updateStatus();
}

bool TableCompareView::isModified() const
{
	for (int side = 0; side < m_paneCount; ++side)
		if (m_sides[side].modified)
			return true;
	return false;
}

void TableCompareView::setSideModified(int side, bool modified)
{
	const bool was = isModified();
	m_sides[side].modified = modified;
	m_actSave->setEnabled(isModified());
	updateStatus();
	if (was != isModified())
		emit modifiedChanged(isModified());
}

QStringList TableCompareView::paths() const
{
	QStringList files;
	for (int side = 0; side < m_paneCount; ++side)
		files.append(m_sides[side].path);
	return files;
}

QString TableCompareView::tabTitle() const
{
	const auto name = [this](int side) {
		if (!m_descriptions[side].isEmpty())
			return m_descriptions[side];
		return m_sides[side].path.isEmpty() ? untitledName(side)
			: QFileInfo(m_sides[side].path).fileName();
	};
	QStringList names;
	for (int side = 0; side < m_paneCount; ++side)
		names.append(name(side));
	return names.join(QString::fromUtf8(" \xE2\x86\x94 "));
}

void TableCompareView::setSideDescription(int side, const QString &description)
{
	if (side < 0 || side >= m_paneCount)
		return;
	m_descriptions[side] = description;
	updateHeader(side);
}

bool TableCompareView::saveModified(QString *error)
{
	bool savedAny = false;
	for (int side = 0; side < m_paneCount; ++side)
	{
		Side &s = m_sides[side];
		if (!s.modified)
			continue;

		// an untitled side asks for a name on its first save
		if (s.path.isEmpty())
		{
			const QString chosen = lm::askSaveAsPath(this, tr("Save As"));
			if (chosen.isEmpty())
			{
				if (error != nullptr)
					*error = tr("save canceled");
				return false;
			}
			s.path = chosen;
			emit pathsChanged();
		}

		// WinMerge's DoSave: writing over a file another application
		// changed since it was loaded here asks first; No leaves the file
		// and this side's unsaved changes as they are
		if (lm::fileChangedOnDisk(s.path, s.stamp) == lm::FileChange::Changed
			&& !lm::askOverwriteChangedFile(this, s.path))
			continue;

		const QDateTime originalTime = lm::timeToPreserve(s.path);
		if (QSettings().value(QStringLiteral("Backup/FileCompare"), true).toBool()
			&& QFile::exists(s.path))
		{
			const QString backupPath = s.path + QStringLiteral(".bak");
			QFile::remove(backupPath);
			if (!QFile::copy(s.path, backupPath))
			{
				if (error != nullptr)
					*error = tr("could not create the backup file %1")
						.arg(backupPath);
				return false;
			}
		}

		UniStdioFile file;
		if (!file.OpenCreate(s.path.toStdString()))
		{
			if (error != nullptr)
				*error = tr("cannot write %1").arg(s.path);
			return false;
		}
		file.SetUnicoding(static_cast<ucr::UNICODESET>(s.unicoding));
		file.SetCodepage(s.codepage);
		file.SetBom(s.bom);
		file.WriteBom();
		// each row ends as the file does, or as its own in a file of
		// mixed endings
		for (int i = 0; i < s.rawLines.size(); ++i)
		{
			file.WriteString(s.rawLines.at(i).toStdString());
			if (i + 1 < s.rawLines.size() || s.hadFinalEol)
				file.WriteString(rowEol(side, i).toStdString());
		}
		file.Close();
		lm::restoreFileTime(s.path, originalTime);
		// what is on disk now is this side's content: no change to report
		s.stamp = lm::fileStamp(s.path);
		setSideModified(side, false);
		++m_saveSerial[side]; // older snapshots no longer match the disk
		savedAny = true;
	}
	if (savedAny)
		emit fileSaved(paths(), m_diffCount);
	return true;
}
