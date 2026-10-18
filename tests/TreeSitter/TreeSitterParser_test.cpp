// SPDX-License-Identifier: GPL-2.0-or-later
// LibreMerge: the Tree-sitter parser (engine/Src/TreeSitterParser.cpp, after
// WinMerge's, which reads UTF-16 there and UTF-8 here) over the grammar
// modules the build makes, as the file view and the comparison's comment
// filter use it. Upstream has no test of its own for it.
#include "pch.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "TreeSitterParser.h"
#include "SyntaxParserRegistry.h"
#include "CrystalLineSyntaxParser.h"
#include "SyntaxColors.h"
#include "DiffWrapper.h"
#include "DiffList.h"
#include "TempFile.h"
#include "UniFile.h"
#include "paths.h"

namespace
{

/** The lines of a buffer, as the file view gives them. */
class Lines : public LangServices::ITextBuffer
{
public:
	explicit Lines(std::vector<std::string> lines) : m_lines(std::move(lines)) {}
	int GetLineCount() const override { return static_cast<int>(m_lines.size()); }
	const tchar_t* GetLineChars(int line) const override { return m_lines[line].c_str(); }
	int GetLineLength(int line) const override { return static_cast<int>(m_lines[line].size()); }
	int GetFullLineLength(int line) const override
	{
		return GetLineLength(line) + (line + 1 < GetLineCount() ? 1 : 0);
	}
	std::vector<std::string> m_lines;
};

/** The grammars of the build, once; false when it made none. */
bool grammars()
{
	if (!paths::IsDirectory(LM_TEST_TREE_SITTER_DIR))
		return false;
	TreeSitterRegistry::Instance().Initialize(LM_TEST_TREE_SITTER_DIR);
	return true;
}

std::shared_ptr<LangServices::ISyntaxParser> parserFor(LangServices::LanguageId type, Lines &text)
{
	auto parser = TreeSitterSyntaxParserFactory::GetInstance().Create(type);
	if (parser)
		parser->SetTextBuffer(&text);
	return parser;
}

/** The color of the character at a (byte) position of a line. */
int colorAt(LangServices::ISyntaxParser &parser, int line, int pos)
{
	int color = COLORINDEX_NORMALTEXT;
	for (const auto &block : parser.ParseLine(line))
	{
		if (block.m_nCharPos > pos)
			break;
		color = block.m_nColorIndex;
	}
	return color;
}

const TempFile WriteToTempFile(const String &text)
{
	TempFile tmpfile;
	tmpfile.Create();
	UniStdioFile file;
	file.OpenCreateUtf8(tmpfile.GetPath());
	file.WriteString(text);
	file.Close();
	return tmpfile;
}

} // namespace

TEST(TreeSitterParser, OnlyTheLanguagesOfTheGrammars)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	auto &factory = TreeSitterSyntaxParserFactory::GetInstance();
	EXPECT_TRUE(factory.IsSupported(LangServices::LanguageId::SRC_YAML));
	EXPECT_TRUE(factory.IsSupported(LangServices::LanguageId::SRC_TOML));
	EXPECT_TRUE(factory.IsSupported(LangServices::LanguageId::SRC_MARKDOWN));
	EXPECT_TRUE(factory.IsSupported(LangServices::LanguageId::SRC_TYPESCRIPT));
	// no grammar of a language the registry maps, no parser
	EXPECT_FALSE(factory.IsSupported(LangServices::LanguageId::SRC_POWERSHELL));
	// nor for one it does not map
	EXPECT_FALSE(factory.IsSupported(LangServices::LanguageId::SRC_ABAP));
	EXPECT_EQ(nullptr, factory.Create(LangServices::LanguageId::SRC_ABAP));
}

TEST(TreeSitterParser, YamlColors)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	Lines text({ "name: \"LibreMerge\"  # the app", "count: 3", "on: true" });
	auto parser = parserFor(LangServices::LanguageId::SRC_YAML, text);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 0, 0));    // the key, @property
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*parser, 0, 6));   // "LibreMerge"
	EXPECT_EQ(COLORINDEX_NORMALTEXT, colorAt(*parser, 0, 19));
	EXPECT_EQ(COLORINDEX_COMMENT, colorAt(*parser, 0, 20)); // # the app
	EXPECT_EQ(COLORINDEX_NUMBER, colorAt(*parser, 1, 7));   // 3
	EXPECT_EQ(COLORINDEX_NUMBER, colorAt(*parser, 2, 4));   // true, @boolean
}

TEST(TreeSitterParser, PositionsAreUtf8Bytes)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	// "título" and "ação" take two bytes for each accented letter: the
	// comment starts at byte 18 (the 16th character)
	Lines text({ "t\xc3\xadtulo: \"a\xc3\xa7\xc3\xa3o\" # nota" });
	auto parser = parserFor(LangServices::LanguageId::SRC_YAML, text);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 0, 6));
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*parser, 0, 9));
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*parser, 0, 16));
	EXPECT_EQ(COLORINDEX_NORMALTEXT, colorAt(*parser, 0, 17));
	EXPECT_EQ(COLORINDEX_COMMENT, colorAt(*parser, 0, 18));
}

TEST(TreeSitterParser, TomlColors)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	Lines text({ "[server]", "port = 8080 # main", "name = \"x\"" });
	auto parser = parserFor(LangServices::LanguageId::SRC_TOML, text);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_OPERATOR, colorAt(*parser, 0, 0)); // [
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 0, 1));    // server, @type
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 1, 0));    // port
	EXPECT_EQ(COLORINDEX_OPERATOR, colorAt(*parser, 1, 5)); // =
	EXPECT_EQ(COLORINDEX_NUMBER, colorAt(*parser, 1, 7));   // 8080
	EXPECT_EQ(COLORINDEX_COMMENT, colorAt(*parser, 1, 12)); // # main
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*parser, 2, 7));   // "x"
}

TEST(TreeSitterParser, InjectedLanguage)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	// a fenced block of YAML in Markdown: the injection runs the YAML
	// grammar over it
	Lines text({ "# Title", "", "```yaml", "key: 1 # note", "```" });
	auto parser = parserFor(LangServices::LanguageId::SRC_MARKDOWN, text);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 3, 0));
	EXPECT_EQ(COLORINDEX_NUMBER, colorAt(*parser, 3, 5));
	EXPECT_EQ(COLORINDEX_COMMENT, colorAt(*parser, 3, 7));
}

TEST(TreeSitterParser, ParsesAgainAfterAnEdit)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	Lines text({ "a: 1", "b: 2" });
	auto parser = parserFor(LangServices::LanguageId::SRC_YAML, text);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_NUMBER, colorAt(*parser, 1, 3));
	text.m_lines[1] = "# b: 2";
	parser->Invalidate();
	EXPECT_EQ(COLORINDEX_COMMENT, colorAt(*parser, 1, 5));
}

TEST(TreeSitterParser, PredicatesPickTheirNodes)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	// LibreMerge: a pattern holds only for the nodes its predicates name; in
	// javascript's query, (#eq? @function.builtin "require") once gave every
	// identifier the function color
	Lines text({ "const fs = require(\"fs\");", "console.log(fs, Math);" });
	auto parser = parserFor(LangServices::LanguageId::SRC_TYPESCRIPT, text);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_NORMALTEXT, colorAt(*parser, 0, 6));  // fs
	EXPECT_EQ(COLORINDEX_FUNCNAME, colorAt(*parser, 0, 11));   // require
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 1, 0));       // console, by #match?
	EXPECT_EQ(COLORINDEX_FUNCNAME, colorAt(*parser, 1, 8));    // log
	EXPECT_EQ(COLORINDEX_NORMALTEXT, colorAt(*parser, 1, 12)); // fs
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 1, 16));      // Math, capitalized: a type

	// a JSX tag in lowercase, a component capitalized
	Lines tsx({ "const a = <div className=\"x\"><Icone /></div>;" });
	auto tsxParser = parserFor(LangServices::LanguageId::SRC_TSX, tsx);
	ASSERT_NE(nullptr, tsxParser);
	EXPECT_EQ(COLORINDEX_KEYWORD, colorAt(*tsxParser, 0, 11)); // div
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*tsxParser, 0, 30));   // Icone
}

TEST(TreeSitterParser, InnerCapturesGiveBackTheOuterColor)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	// LibreMerge: a capture inside another colors where both start and
	// gives the outer color back where it ends, which WinMerge's parser
	// left to normal text
	Lines props({ "app.titulo = C:\\\\x" });
	auto parser = parserFor(LangServices::LanguageId::SRC_PROPERTIES, props);
	ASSERT_NE(nullptr, parser);
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 0, 0));      // app
	EXPECT_EQ(COLORINDEX_OPERATOR, colorAt(*parser, 0, 3));   // .
	EXPECT_EQ(COLORINDEX_USER1, colorAt(*parser, 0, 4));      // titulo, the key again
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*parser, 0, 17));    // x, the value after its escape

	Lines ts({ "const s = `a ${b} c`;" });
	auto tsParser = parserFor(LangServices::LanguageId::SRC_TYPESCRIPT, ts);
	ASSERT_NE(nullptr, tsParser);
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*tsParser, 0, 11));   // a
	EXPECT_EQ(COLORINDEX_OPERATOR, colorAt(*tsParser, 0, 13)); // ${
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*tsParser, 0, 18));   // c, the string again

	// a line inside a multi-line string and its pair: the string's
	Lines toml({ "s = \"\"\"", "b = 2", "\"\"\"" });
	auto tomlParser = parserFor(LangServices::LanguageId::SRC_TOML, toml);
	ASSERT_NE(nullptr, tomlParser);
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*tomlParser, 1, 0));

	// and across an injection: a heading's color around its inline code
	Lines md({ "# Um `x` dois", "", "Texto `y` fim" });
	auto mdParser = parserFor(LangServices::LanguageId::SRC_MARKDOWN, md);
	ASSERT_NE(nullptr, mdParser);
	EXPECT_EQ(COLORINDEX_KEYWORD, colorAt(*mdParser, 0, 2));     // Um
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*mdParser, 0, 6));      // x
	EXPECT_EQ(COLORINDEX_KEYWORD, colorAt(*mdParser, 0, 9));     // dois
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*mdParser, 2, 7));      // y
	EXPECT_EQ(COLORINDEX_NORMALTEXT, colorAt(*mdParser, 2, 10)); // fim
}

TEST(TreeSitterParser, ChunksKeepToTheirLines)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	// LibreMerge: the parser fills its cache 300 lines at a time, and a
	// capture going on from one chunk into another built before leaves no
	// blocks there once that one is sorted
	std::vector<std::string> lines;
	for (int i = 0; i < 298; ++i)
		lines.push_back("k" + std::to_string(i) + " = " + std::to_string(i));
	lines.push_back("s = \"\"\"");
	lines.push_back("a = 1");
	lines.push_back("b = 2");  // line 300, the second chunk's first
	lines.push_back("\"\"\"");
	for (int i = 302; i < 400; ++i)
		lines.push_back("k" + std::to_string(i) + " = " + std::to_string(i));
	Lines text(lines);
	auto parser = parserFor(LangServices::LanguageId::SRC_TOML, text);
	ASSERT_NE(nullptr, parser);
	parser->ParseLine(350);  // the second chunk first
	parser->ParseLine(10);   // then the first, where the string starts
	for (int line : { 300, 301 })
	{
		const auto blocks = parser->ParseLine(line);
		for (size_t k = 1; k < blocks.size(); ++k)
			EXPECT_LT(blocks[k - 1].m_nCharPos, blocks[k].m_nCharPos) << "line " << line;
	}
	EXPECT_EQ(COLORINDEX_STRING, colorAt(*parser, 300, 0));
}

TEST(TreeSitterParser, CommentsLeftOutOfTheComparison)
{
	if (!grammars())
		GTEST_SKIP() << "built without the Tree-sitter grammars";
	// the application's registry: the built-in parsers, then Tree-sitter's
	// for the languages they lack, as WinMerge's "Built-in first"
	auto &registry = LangServices::SyntaxParserRegistry::GetInstance();
	registry.RegisterFactory(&CrystalLineSyntaxParserFactory::GetInstance());
	registry.RegisterFactory(&TreeSitterSyntaxParserFactory::GetInstance());

	CDiffWrapper dw;
	DIFFOPTIONS options{};
	options.bFilterCommentsLines = true;
	dw.SetFilterCommentsSourceDef(_T("yaml"));
	DiffList diffList;
	const TempFile left = WriteToTempFile(_T("a: 1 # old\nb: 2\n"));
	const TempFile right = WriteToTempFile(_T("a: 1 # new\nb: 2\n"));
	dw.SetCreateDiffList(&diffList);
	dw.SetPaths({ left.GetPath(), right.GetPath() }, false);
	dw.SetOptions(&options);
	dw.RunFileDiff();
	ASSERT_EQ(1, diffList.GetSize());
	DIFFRANGE dr;
	diffList.GetDiff(0, dr);
	EXPECT_EQ(OP_TRIVIAL, dr.op);

	registry.UnregisterFactory(&TreeSitterSyntaxParserFactory::GetInstance());
}
