// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "TableCompareView.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QTemporaryFile>
#include <QToolBar>
#include <QVBoxLayout>

#include "Icons.h"
#include "Theme.h"

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "EngineOptions.h"
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

} // namespace

/** Read-only model over one side's aligned rows. */
class TableSideModel : public QAbstractTableModel
{
public:
	TableSideModel(TableCompareView *view, int side)
		: QAbstractTableModel(view), m_view(view), m_side(side) {}

	int rowCount(const QModelIndex &parent = {}) const override
	{
		if (parent.isValid())
			return 0;
		int rows = static_cast<int>(m_view->m_viewToReal[m_side].size());
		if (m_view->m_firstRowIsHeader && rows > 0)
			--rows;
		return rows;
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
		return m_view->m_firstRowIsHeader ? modelRow + 1 : modelRow;
	}

	TableCompareView *m_view;
	int m_side;
	int m_columns = 0;
};

TableCompareView::~TableCompareView() = default;

TableCompareView::TableCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	auto addToolAction = [this, toolbar](lm::Icon icon, const QString &text,
		auto slot) -> QAction * {
		QAction *action = toolbar->addAction(lm::icon(icon), text);
		action->setToolTip(text);
		connect(action, &QAction::triggered, this, slot);
		return action;
	};
	addToolAction(lm::Icon::PrevDiff, tr("Previous Difference"),
		[this]() { gotoPrevDiff(); });
	addToolAction(lm::Icon::NextDiff, tr("Next Difference"),
		[this]() { gotoNextDiff(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::CopyRight, tr("Copy to Right"),
		[this]() { copyCurrentDiff(0); });
	addToolAction(lm::Icon::CopyLeft, tr("Copy to Left"),
		[this]() { copyCurrentDiff(1); });
	addToolAction(lm::Icon::CopyAllRight, tr("Copy All to Right"),
		[this]() { copyAllFrom(0); });
	addToolAction(lm::Icon::CopyAllLeft, tr("Copy All to Left"),
		[this]() { copyAllFrom(1); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::Refresh, tr("Recompare"),
		[this]() { recompare(); });
	m_actSave = addToolAction(lm::Icon::Save, tr("Save"),
		[this]() { QString error; saveModified(&error); });
	m_actSave->setEnabled(false);
	toolbar->addSeparator();
	m_actHeader = addToolAction(lm::Icon::DiffPane,
		tr("First row is the header"), [this]() {});
	m_actHeader->setCheckable(true);
	m_actHeader->setChecked(true);
	connect(m_actHeader, &QAction::toggled, this, [this](bool on) {
		m_firstRowIsHeader = on;
		rebuildModel();
	});
	addToolAction(lm::Icon::Find, tr("Open as Text"), [this]() {
		emit openAsTextRequested(m_sides[0].path, m_sides[1].path);
	});
	layout->addWidget(toolbar);

	auto *tables = new QHBoxLayout;
	tables->setContentsMargins(0, 0, 0, 0);
	tables->setSpacing(1);
	for (int i = 0; i < 2; ++i)
	{
		m_models[i] = new TableSideModel(this, i);
		m_tables[i] = new QTableView(this);
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
			this, [this, i](const QModelIndex &index) {
				// select the difference under the double-clicked row
				const int viewRow = m_firstRowIsHeader
					? index.row() + 1 : index.row();
				for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
				{
					if (!m_blocks[b].trivial
						&& viewRow >= m_blocks[b].viewBegin
						&& viewRow <= m_blocks[b].viewEnd)
					{
						m_current = b;
						m_models[0]->refresh(m_models[0]->columnCount());
						m_models[1]->refresh(m_models[1]->columnCount());
						updateStatus();
						break;
					}
				}
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
	m_models[0]->refresh(m_models[0]->columnCount());
	m_models[1]->refresh(m_models[1]->columnCount());
}

bool TableCompareView::compare(const QString &leftPath,
	const QString &rightPath, QString *error)
{
	if (!loadSide(0, leftPath, error) || !loadSide(1, rightPath, error))
		return false;

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
		temp[i].write(m_sides[i].rawLines.join(QChar('\n')).toUtf8());
		temp[i].flush();
		paths.SetPath(i, temp[i].fileName().toStdString(), false);
	}

	CDiffWrapper wrapper;
	DIFFOPTIONS options = lm::currentDiffOptions();
	DiffList diffList;
	wrapper.SetCreateDiffList(&diffList);
	wrapper.SetPaths(paths, false);
	wrapper.SetOptions(&options);
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

	int columns = 1;
	for (int side = 0; side < 2; ++side)
		for (const QStringList &row : m_sides[side].cells)
			columns = qMax(columns, static_cast<int>(row.size()));
	for (int side = 0; side < 2; ++side)
		m_models[side]->refresh(columns);
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
	for (int b = from + direction;
		b >= 0 && b < static_cast<int>(m_blocks.size()); b += direction)
		if (!m_blocks[b].trivial)
			return b;
	return -1;
}

void TableCompareView::gotoDiff(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(m_blocks.size()))
		return;
	m_current = blockIndex;
	const int headerOffset = m_firstRowIsHeader ? 1 : 0;
	const int modelRow =
		qMax(0, m_blocks[blockIndex].viewBegin - headerOffset);
	for (int i = 0; i < 2; ++i)
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

void TableCompareView::copyCurrentDiff(int sourceSide)
{
	if (m_current < 0)
		m_current = nextActive(-1, +1);
	if (m_current < 0 || m_current >= static_cast<int>(m_blocks.size()))
		return;
	const Block block = m_blocks[m_current];
	const int target = 1 - sourceSide;

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
		if (!m_blocks[b].trivial && m_blocks[b].viewBegin >= wasViewBegin)
		{
			gotoDiff(b);
			return;
		}
	updateStatus();
}

void TableCompareView::copyAllFrom(int sourceSide)
{
	const int target = 1 - sourceSide;
	m_sides[target].rawLines = m_sides[sourceSide].rawLines;
	m_sides[target].cells = m_sides[sourceSide].cells;
	setSideModified(target, true);
	QString error;
	runDiff(&error);
	rebuildModel();
	m_current = -1;
	updateStatus();
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
	return QFileInfo(m_sides[0].path).fileName()
		+ QString::fromUtf8(" \xE2\x86\x94 ")
		+ QFileInfo(m_sides[1].path).fileName();
}

bool TableCompareView::saveModified(QString *error)
{
	for (int side = 0; side < 2; ++side)
	{
		Side &s = m_sides[side];
		if (!s.modified)
			continue;

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
		setSideModified(side, false);
	}
	return true;
}
