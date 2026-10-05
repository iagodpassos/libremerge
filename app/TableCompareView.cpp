// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "TableCompareView.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QTemporaryFile>
#include <QToolBar>
#include <QVBoxLayout>

#include "DisplayFilterBar.h"
#include "FileFilters.h"
#include "FileOps.h"
#include "Icons.h"
#include "LineFilterMenu.h"
#include "MessageBoxes.h"
#include "Theme.h"

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "EngineOptions.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/ILineDataProvider.h"
#include "LineFilterHelper.h"
#include "PathContext.h"
#include "UniFile.h"

namespace
{

/** Split one CSV/TSV line honoring quotes ("" escapes a quote). */
QStringList splitRow(const QString &line, QChar delimiter)
{
	QStringList cells;
	QString cell;
	bool quoted = false;
	for (int i = 0; i < line.size(); ++i)
	{
		const QChar c = line.at(i);
		if (quoted)
		{
			if (c == QChar('"'))
			{
				if (i + 1 < line.size() && line.at(i + 1) == QChar('"'))
				{
					cell.append(QChar('"'));
					++i;
				}
				else
					quoted = false;
			}
			else
				cell.append(c);
		}
		else if (c == QChar('"') && cell.isEmpty())
			quoted = true;
		else if (c == delimiter)
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

/** Pick the delimiter that splits the sampled lines most consistently. */
QChar sniffDelimiter(const QStringList &sample)
{
	const QList<QChar> candidates = {
		QChar(','), QChar(';'), QChar('\t'), QChar('|') };
	QChar best = QChar(',');
	int bestScore = -1;
	for (const QChar candidate : candidates)
	{
		int score = 0;
		for (const QString &line : sample)
			score += static_cast<int>(line.count(candidate));
		if (score > bestScore)
		{
			bestScore = score;
			best = candidate;
		}
	}
	return best;
}

quint64 cellKey(int row, int column)
{
	return (static_cast<quint64>(static_cast<quint32>(row)) << 32)
		| static_cast<quint32>(column);
}

/** A row's header tells whether hidden rows follow the row. */
constexpr int kBoundaryRole = Qt::UserRole + 1;

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
int rawCellCount(const QString &line, QChar delimiter)
{
	int count = 1;
	bool quoted = false;
	for (const QChar c : line)
	{
		if (c == QChar('"'))
			quoted = !quoted;
		else if (!quoted && c == delimiter)
			++count;
	}
	return count;
}

QString rawCell(const QString &line, QChar delimiter, int column)
{
	QString cell;
	int current = 0;
	bool quoted = false;
	for (int i = 0; i < line.size() && current <= column; ++i)
	{
		const QChar c = line.at(i);
		if (current == column && (quoted || c != delimiter))
			cell.append(c);
		if (c == QChar('"'))
			quoted = !quoted;
		else if (!quoted && c == delimiter)
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
		for (int side = 0; side < 2; ++side)
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
			for (int side = 0; side < 2; ++side)
				for (int row = block.viewBegin; row <= block.viewEnd && row < rows; ++row)
				{
					unsigned &flags = m_flags[side][row];
					if (block.trivial)
						flags |= LF_TRIVIAL;
					else if (!(flags & LF_GHOST))
						flags |= LF_DIFF;
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
			? rawCellCount(line(pane, lineIndex), m_view->m_delimiter) : 1;
	}
	std::string GetColumn(int pane, int lineIndex, int columnIndex) const override
	{
		return rawCell(line(pane, lineIndex), m_view->m_delimiter, columnIndex).toStdString();
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
		// the file's line ending, which every row of a side shares here;
		// the last row has none when the file ends without one
		const Side &side = m_view->m_sides[pane];
		if (!side.hadFinalEol && m_realIndex[pane][lineIndex] == m_realCount[pane] - 1)
			return EOL_NONE;
		if (side.eol == QStringLiteral("\r\n"))
			return EOL_CRLF;
		return side.eol == QStringLiteral("\r") ? EOL_CR : EOL_LF;
	}

private:
	bool valid(int pane, int lineIndex) const
	{
		return pane >= 0 && pane < 2 && lineIndex >= 0
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
	std::vector<unsigned> m_flags[2];
	std::vector<int> m_realIndex[2];
	int m_realCount[2] = {};
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

		if (role == Qt::DisplayRole)
		{
			if (real < 0)
				return {};
			const auto &cells = m_view->m_sides[m_side].cells;
			if (real < static_cast<int>(cells.size())
				&& index.column() < cells[real].size())
				return cells[real].at(index.column());
			return {};
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

private:
	int toViewRow(int modelRow) const
	{
		return m_view->m_modelRows.value(modelRow, -1);
	}

	TableCompareView *m_view;
	int m_side;
	int m_columns = 0;
};

TableCompareView::~TableCompareView() = default;

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
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x92"), [this]() { copyCurrentDiff(0); });
	addToolAction(lm::Icon::CopyLeft, tr("Copy to Left"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x90"), [this]() { copyCurrentDiff(1); });
	addToolAction(lm::Icon::CopyAllRight, tr("Copy All to Right"), QString(),
		[this]() { copyAllFrom(0); });
	addToolAction(lm::Icon::CopyAllLeft, tr("Copy All to Left"), QString(),
		[this]() { copyAllFrom(1); });
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
		emit openAsTextRequested(m_sides[0].path, m_sides[1].path);
	});
	layout->addWidget(toolbar);

	auto *tables = new QHBoxLayout;
	tables->setContentsMargins(0, 0, 0, 0);
	tables->setSpacing(1);
	for (int i = 0; i < 2; ++i)
	{
		m_models[i] = new TableSideModel(this, i);
		m_tables[i] = new RuledTableView(this);
		m_tables[i]->setVerticalHeader(new RuledRowHeader(m_tables[i]));
		m_tables[i]->setModel(m_models[i]);
		m_tables[i]->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_tables[i]->setSelectionBehavior(QAbstractItemView::SelectRows);
		m_tables[i]->horizontalHeader()->setDefaultSectionSize(110);
		m_tables[i]->verticalHeader()->setDefaultSectionSize(22);
		m_tables[i]->setAlternatingRowColors(false);
		tables->addWidget(m_tables[i], 1);
		connect(m_tables[i]->verticalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) {
				if (m_syncing)
					return;
				m_syncing = true;
				m_tables[1 - i]->verticalScrollBar()->setValue(value);
				m_syncing = false;
			});
		connect(m_tables[i]->horizontalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) {
				if (m_syncing)
					return;
				m_syncing = true;
				m_tables[1 - i]->horizontalScrollBar()->setValue(value);
				m_syncing = false;
			});
		connect(m_tables[i], &QTableView::doubleClicked,
			this, [this](const QModelIndex &index) {
				// select the difference under the double-clicked row
				selectDiffAtModelRow(index.row());
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
	for (int i = 0; i < 2; ++i)
		m_tables[i]->setPalette(pal);
	m_status->setStyleSheet(dark
		? QStringLiteral("QLabel { background: #2c2c2c; color: #b8b8b8; }")
		: QStringLiteral("QLabel { background: #ececec; color: #303030; }"));
	lm::applyToolbarTheme(this);
	m_models[0]->refresh(m_models[0]->columnCount());
	m_models[1]->refresh(m_models[1]->columnCount());
}

bool TableCompareView::compare(const QString &leftPath,
	const QString &rightPath, QString *error)
{
	if (!loadSide(0, leftPath, error) || !loadSide(1, rightPath, error))
		return false;
	clearHistory();

	// sniff the delimiter over both files' first rows
	QStringList sample;
	for (int side = 0; side < 2; ++side)
		for (int row = 0; row < qMin(20, m_sides[side].rawLines.size()); ++row)
			sample.append(m_sides[side].rawLines.at(row));
	m_delimiter = sniffDelimiter(sample);
	for (int side = 0; side < 2; ++side)
	{
		m_sides[side].cells.clear();
		for (const QString &line : m_sides[side].rawLines)
			m_sides[side].cells.push_back(splitRow(line, m_delimiter));
	}

	if (!runDiff(error))
		return false;
	m_current = -1;
	rebuildModel();
	updateStatus();
	return true;
}

bool TableCompareView::loadSide(int side, const QString &path, QString *error)
{
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
	s.cells.clear();

	int crlf = 0, lf = 0, cr = 0;
	String line, eol;
	bool lossy = false;
	bool lastHadEol = true;
	while (file.ReadString(line, eol, &lossy))
	{
		s.rawLines.append(
			QString::fromUtf8(line.data(), static_cast<int>(line.size())));
		if (eol == "\r\n") ++crlf;
		else if (eol == "\n") ++lf;
		else if (eol == "\r") ++cr;
		lastHadEol = !eol.empty();
	}
	file.Close();

	s.hadFinalEol = s.rawLines.isEmpty() ? false : lastHadEol;
	if (crlf >= lf && crlf >= cr && crlf > 0)
		s.eol = QStringLiteral("\r\n");
	else if (cr > lf)
		s.eol = QStringLiteral("\r");
	else
		s.eol = QStringLiteral("\n");
	s.modified = false;
	return true;
}

bool TableCompareView::runDiff(QString *error)
{
	QTemporaryFile temp[2];
	PathContext paths;
	paths.SetSize(2);
	for (int i = 0; i < 2; ++i)
	{
		if (!temp[i].open())
		{
			if (error != nullptr)
				*error = tr("cannot create temporary file");
			return false;
		}
		// every row goes with its line ending, the last one too (see the
		// text compare's runDiff)
		QByteArray bytes = m_sides[i].rawLines.join(QChar('\n')).toUtf8();
		if (!m_sides[i].rawLines.isEmpty())
			bytes.append('\n');
		temp[i].write(bytes);
		temp[i].flush();
		paths.SetPath(i, temp[i].fileName().toStdString(), false);
	}

	CDiffWrapper wrapper;
	DIFFOPTIONS options = lm::currentDiffOptions();
	DiffList diffList;
	wrapper.SetCreateDiffList(&diffList);
	wrapper.SetPaths(paths, false);
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
		for (int side = 0; side < 2; ++side)
		{
			block.begin[side] = dr.begin[side];
			block.end[side] = dr.end[side];
		}
		block.trivial = (dr.op == OP_TRIVIAL);
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
	for (int side = 0; side < 2; ++side)
		m_viewToReal[side].clear();
	int realPos[2] = {};
	int viewPos = 0;
	for (Block &block : m_blocks)
	{
		const int commonLen = qMax(0, block.begin[0] - realPos[0]);
		for (int side = 0; side < 2; ++side)
			for (int k = 0; k < commonLen
				&& realPos[side] < m_sides[side].rawLines.size(); ++k)
				m_viewToReal[side].append(realPos[side]++);
		viewPos += commonLen;

		int maxLen = 0;
		for (int side = 0; side < 2; ++side)
			maxLen = qMax(maxLen, block.end[side] - block.begin[side] + 1);
		block.viewBegin = viewPos;
		block.viewEnd = viewPos + maxLen - 1;
		for (int side = 0; side < 2; ++side)
		{
			const int len = qMax(0, block.end[side] - block.begin[side] + 1);
			for (int k = 0; k < len; ++k)
				m_viewToReal[side].append(realPos[side]++);
			for (int k = len; k < maxLen; ++k)
				m_viewToReal[side].append(-1);
		}
		viewPos += maxLen;
	}
	for (int side = 0; side < 2; ++side)
		while (realPos[side] < m_sides[side].rawLines.size())
			m_viewToReal[side].append(realPos[side]++);
	const int totalRows = qMax(m_viewToReal[0].size(), m_viewToReal[1].size());
	for (int side = 0; side < 2; ++side)
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
	for (int side = 0; side < 2; ++side)
		for (const QStringList &row : m_sides[side].cells)
			columns = qMax(columns, static_cast<int>(row.size()));
	for (int side = 0; side < 2; ++side)
		m_models[side]->refresh(columns);
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
	for (const Side &side : m_sides)
		files.append({ side.path, side.unicoding, side.codepage, side.bom });
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
	m_tables[m_tables[1]->hasFocus() ? 1 : 0]->setFocus();
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
	recompare();
	m_tables[m_tables[1]->hasFocus() ? 1 : 0]->setFocus();
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
	menu.setTarget(side == 1 ? 3 : side + 1, 0, column);
	const std::optional<QString> filter =
		menu.apply(LineFilterMenu::ColumnText + dataType, displayFilter());
	if (!filter.has_value())
		return;
	ensureFilterBar();
	m_displayFilter->SetStringOrExpression(filter->toStdString());
	m_filterBar->setFilterText(displayFilter());
	applyDisplayFilter();
}

void TableCompareView::autoFitColumns()
{
	// upstream measures every buffer's cells for one set of widths
	for (QTableView *table : m_tables)
		table->resizeColumnsToContents();
	const int columns = m_models[0]->columnCount();
	for (int column = 0; column < columns; ++column)
	{
		const int width = qMax(m_tables[0]->columnWidth(column), m_tables[1]->columnWidth(column));
		for (QTableView *table : m_tables)
			table->setColumnWidth(column, width);
	}
}

/** Cells that differ between the paired rows of each difference. */
void TableCompareView::computeCellDiffs()
{
	m_cellDiffs[0].clear();
	m_cellDiffs[1].clear();
	for (const Block &block : m_blocks)
	{
		if (block.trivial)
			continue;
		const int len0 = block.end[0] - block.begin[0] + 1;
		const int len1 = block.end[1] - block.begin[1] + 1;
		const int pairs = qMin(qMax(0, len0), qMax(0, len1));
		for (int k = 0; k < pairs; ++k)
		{
			const int row0 = block.begin[0] + k;
			const int row1 = block.begin[1] + k;
			if (row0 >= static_cast<int>(m_sides[0].cells.size())
				|| row1 >= static_cast<int>(m_sides[1].cells.size()))
				continue;
			const QStringList &cells0 = m_sides[0].cells[row0];
			const QStringList &cells1 = m_sides[1].cells[row1];
			const int columns = qMax(cells0.size(), cells1.size());
			for (int col = 0; col < columns; ++col)
			{
				const QString v0 = col < cells0.size() ? cells0.at(col) : QString();
				const QString v1 = col < cells1.size() ? cells1.at(col) : QString();
				if (v0 != v1)
				{
					m_cellDiffs[0].insert(cellKey(row0, col));
					m_cellDiffs[1].insert(cellKey(row1, col));
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
	text += tr("  \xE2\x80\xA2  delimiter: %1")
		.arg(m_delimiter == QChar('\t') ? tr("tab") : QString(m_delimiter));
	bool modified = m_sides[0].modified || m_sides[1].modified;
	if (modified)
		text += tr("  \xE2\x80\xA2 unsaved changes");
	m_status->setText(text);
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
	for (int i = 0; i < 2 && modelRow >= 0; ++i)
		m_tables[i]->scrollTo(m_models[i]->index(modelRow, 0),
			QAbstractItemView::PositionAtCenter);
	m_models[0]->refresh(m_models[0]->columnCount());
	m_models[1]->refresh(m_models[1]->columnCount());
	updateStatus();
}

void TableCompareView::gotoNextDiff()
{
	const int next = nextActive(m_current, +1);
	if (next >= 0)
		gotoDiff(next);
}

void TableCompareView::gotoPrevDiff()
{
	const int prev = nextActive(
		m_current < 0 ? static_cast<int>(m_blocks.size()) : m_current, -1);
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
	const int side = m_tables[1]->hasFocus() ? 1 : 0;
	const QModelIndex index = m_tables[side]->currentIndex();
	if (index.isValid())
		selectDiffAtModelRow(index.row());
}

TableCompareView::UndoEntry TableCompareView::captureEntry(int side,
	int viewBegin) const
{
	return { side, m_sides[side].rawLines, m_sides[side].modified,
		m_saveSerial[side], viewBegin };
}

/** Swap the given side's live content with the snapshot (WinMerge's
 *  OnEditUndo re-selects the restored difference "so we may just merge
 *  it again"). */
void TableCompareView::applyEntry(const UndoEntry &entry)
{
	Side &side = m_sides[entry.side];
	side.rawLines = entry.lines;
	side.cells.clear();
	for (const QString &line : side.rawLines)
		side.cells.push_back(splitRow(line, m_delimiter));
	// once the side has been saved again the snapshot no longer matches
	// the file on disk, so the side stays marked as modified
	setSideModified(entry.side,
		entry.saveSerial == m_saveSerial[entry.side] ? entry.modified : true);

	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
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
	m_redoStack.append(captureEntry(entry.side, entry.viewBegin));
	applyEntry(entry);
	updateUndoActions();
}

void TableCompareView::redo()
{
	if (m_redoStack.isEmpty())
		return;
	const UndoEntry entry = m_redoStack.takeLast();
	m_undoStack.append(captureEntry(entry.side, entry.viewBegin));
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
	// snapshots refer to sides by index, which a swap would scramble
	clearHistory();
	std::swap(m_sides[0], m_sides[1]);
	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	updateStatus();
	emit pathsChanged();
}

void TableCompareView::focusNextPane()
{
	const int side = m_tables[0]->hasFocus() ? 1 : 0;
	m_tables[side]->setFocus();
}

void TableCompareView::copyCurrentDiff(int sourceSide)
{
	if (m_current < 0)
		m_current = nextActive(-1, +1);
	if (m_current < 0 || m_current >= static_cast<int>(m_blocks.size()))
		return;
	const Block block = m_blocks[m_current];
	const int target = 1 - sourceSide;
	pushUndo(captureEntry(target, block.viewBegin));

	QStringList newLines;
	for (int row = block.begin[sourceSide];
		row <= block.end[sourceSide]
			&& row < m_sides[sourceSide].rawLines.size(); ++row)
		newLines.append(m_sides[sourceSide].rawLines.at(qMax(0, row)));

	QStringList &lines = m_sides[target].rawLines;
	const int first = block.begin[target];
	const int count = qMax(0, block.end[target] - block.begin[target] + 1);
	for (int k = 0; k < count; ++k)
		lines.removeAt(first);
	for (int k = 0; k < newLines.size(); ++k)
		lines.insert(first + k, newLines.at(k));
	m_sides[target].cells.clear();
	for (const QString &line : lines)
		m_sides[target].cells.push_back(splitRow(line, m_delimiter));
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

void TableCompareView::copyAllFrom(int sourceSide)
{
	// CMergeDoc::CopyMultipleList: not with hidden lines among the
	// differences to copy
	if (hasInvisibleLines())
	{
		lm::showError(this, tr("Merging/copying differences that contain hidden lines is "
			"not currently supported.\n\nPlease clear the display filter or adjust the "
			"filter settings to show all lines before merging."));
		return;
	}
	const int target = 1 - sourceSide;
	pushUndo(captureEntry(target, 0));
	m_sides[target].rawLines = m_sides[sourceSide].rawLines;
	m_sides[target].cells = m_sides[sourceSide].cells;
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
	return m_sides[0].unicoding != m_sides[1].unicoding
		|| m_sides[0].codepage != m_sides[1].codepage
		|| m_sides[0].bom != m_sides[1].bom;
}

QString TableCompareView::changedPathOnDisk() const
{
	for (const Side &s : m_sides)
		if (lm::fileChangedOnDisk(s.path, s.stamp) == lm::FileChange::Changed)
			return s.path;
	return QString();
}

bool TableCompareView::reload(QString *error)
{
	// refuse before touching anything: a side already replaced could not
	// be put back (WinMerge closes the comparison at this point instead)
	for (const Side &s : m_sides)
	{
		if (!QFile(s.path).open(QIODevice::ReadOnly))
		{
			if (error != nullptr)
				*error = tr("cannot open %1").arg(s.path);
			return false;
		}
	}

	const bool wasModified = isModified();
	// MoveOnLoad(nActivePane, pt.y): back to the current row afterwards
	const int activeSide = m_tables[1]->hasFocus() ? 1 : 0;
	const int currentRow = m_tables[activeSide]->currentIndex().row();
	const bool compared = compare(m_sides[0].path, m_sides[1].path, error);
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
	return m_sides[0].modified || m_sides[1].modified;
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
	return { m_sides[0].path, m_sides[1].path };
}

QString TableCompareView::tabTitle() const
{
	const auto name = [this](int side) {
		return m_descriptions[side].isEmpty()
			? QFileInfo(m_sides[side].path).fileName() : m_descriptions[side];
	};
	return name(0) + QString::fromUtf8(" \xE2\x86\x94 ") + name(1);
}

void TableCompareView::setSideDescription(int side, const QString &description)
{
	if (side >= 0 && side < 2)
		m_descriptions[side] = description;
}

bool TableCompareView::saveModified(QString *error)
{
	bool savedAny = false;
	for (int side = 0; side < 2; ++side)
	{
		Side &s = m_sides[side];
		if (!s.modified)
			continue;

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
		const std::string eol = s.eol.toStdString();
		for (int i = 0; i < s.rawLines.size(); ++i)
		{
			file.WriteString(s.rawLines.at(i).toStdString());
			if (i + 1 < s.rawLines.size() || s.hadFinalEol)
				file.WriteString(eol);
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
