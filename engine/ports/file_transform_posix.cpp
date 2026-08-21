// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: minimal FileTransform implementation without the
// COM/ActiveX plugin system. Unpacker/prediffer pipelines are accepted but
// perform no transformation; a native plugin mechanism is a Phase 2 item.
#ifndef _WIN32

#include "pch.h"
#include "FileTransform.h"
#include "MergeApp.h"
#include "unicoder.h"
#include "UniFile.h"
#include "TFile.h"
#include "Environment.h"
#include "paths.h"

namespace FileTransform
{

bool AutoUnpacking = false;
bool AutoPrediffing = false;

/**
 * Convert a file from the given codepage to UTF-8 in a temp copy,
 * replacing the Win32/mlang-driven multiformatText path.
 */
bool AnyCodepageToUTF8(int codepage, String & filepath, bool bMayOverwrite)
{
	if (codepage == ucr::CP_UTF_8)
		return true; // nothing to do

	String tempDir = env::GetTemporaryPath();
	if (tempDir.empty())
		return false;
	String tempFilepath = env::GetTemporaryFileName(tempDir, _T("_W3"));
	if (tempFilepath.empty())
		return false;

	UniMemFile fileIn;
	UniStdioFile fileOut;
	if (!fileIn.OpenReadOnly(filepath))
		return false;
	fileIn.ReadBom();
	if (!fileIn.HasBom())
	{
		fileIn.SetUnicoding(ucr::NONE);
		fileIn.SetCodepage(codepage);
	}
	if (!fileOut.OpenCreate(tempFilepath))
	{
		fileIn.Close();
		return false;
	}
	fileOut.SetUnicoding(ucr::UTF8);
	fileOut.SetBom(false);

	String line, eol;
	bool lossy = false;
	while (fileIn.ReadString(line, eol, &lossy))
	{
		fileOut.WriteString(line);
		fileOut.WriteString(eol);
	}
	fileIn.Close();
	fileOut.Close();

	if (bMayOverwrite)
	{
		try
		{
			TFile(filepath).remove();
		}
		catch (...)
		{
		}
	}
	filepath = tempFilepath;
	return true;
}

} // namespace FileTransform

// --- PluginForFile / PackingInfo / PrediffingInfo (no plugin backend) ---

std::vector<PluginForFile::PipelineItem> PluginForFile::ParsePluginPipeline(String& errorMessage) const
{
	return ParsePluginPipeline(m_PluginPipeline, errorMessage);
}

std::vector<PluginForFile::PipelineItem> PluginForFile::ParsePluginPipeline(
	const String& pluginPipeline, String& errorMessage)
{
	if (!pluginPipeline.empty())
		errorMessage = _("Plugins are not available on this platform yet");
	return {};
}

String PluginForFile::MakePluginPipeline(const std::vector<PipelineItem>& list)
{
	(void)list;
	return String();
}

String PluginForFile::MakeArguments(const std::vector<String>& args, const std::vector<StringView>& variables)
{
	(void)variables;
	return strutils::join(args.begin(), args.end(), _T(" "));
}

bool PackingInfo::GetPackUnpackPlugin(const String& filteredFilenames, bool bUrl, bool bReverse,
	std::vector<std::tuple<PluginInfo*, std::vector<String>, uint8_t, std::vector<String>, bool>>& plugins,
	String *pPluginPipelineResolved, String& errorMessage, int stack) const
{
	(void)filteredFilenames; (void)bUrl; (void)bReverse; (void)plugins; (void)stack;
	if (pPluginPipelineResolved != nullptr)
		pPluginPipelineResolved->clear();
	if (!m_PluginPipeline.empty())
	{
		errorMessage = _("Plugins are not available on this platform yet");
		return false;
	}
	return true;
}

bool PackingInfo::Unpacking(int target, std::vector<int> * handlerSubcodes, String & filepath,
	const String& filteredText, const std::vector<StringView>& variables)
{
	(void)target; (void)filepath; (void)filteredText; (void)variables;
	if (handlerSubcodes != nullptr)
		handlerSubcodes->clear();
	return m_PluginPipeline.empty(); // nothing to unpack without plugins
}

bool PackingInfo::pack(int target, String & filepath, const String& dstFilepath,
	const std::vector<int>& handlerSubcodes, const std::vector<StringView>& variables) const
{
	(void)target; (void)filepath; (void)dstFilepath; (void)handlerSubcodes; (void)variables;
	return m_PluginPipeline.empty();
}

bool PackingInfo::Packing(int target, const String& srcFilepath, const String& dstFilepath,
	const std::vector<int>& handlerSubcodes, const std::vector<StringView>& variables) const
{
	(void)target; (void)srcFilepath; (void)dstFilepath; (void)handlerSubcodes; (void)variables;
	return m_PluginPipeline.empty();
}

String PackingInfo::GetUnpackedFileExtension(int target, const String& filteredFilenames,
	int& preferredWindowType) const
{
	(void)target; (void)filteredFilenames;
	preferredWindowType = -1;
	return String();
}

bool PrediffingInfo::GetPrediffPlugin(const String& filteredFilenames, bool bReverse,
	std::vector<std::tuple<PluginInfo*, std::vector<String>, uint8_t, std::vector<String>, bool>>& plugins,
	String* pPluginPipelineResolved, String& errorMessage, int stack) const
{
	(void)filteredFilenames; (void)bReverse; (void)plugins; (void)stack;
	if (pPluginPipelineResolved != nullptr)
		pPluginPipelineResolved->clear();
	if (!m_PluginPipeline.empty())
	{
		errorMessage = _("Plugins are not available on this platform yet");
		return false;
	}
	return true;
}

bool PrediffingInfo::Prediffing(int target, String & filepath, const String& filteredText,
	bool bMayOverwrite, const std::vector<StringView>& variables)
{
	(void)target; (void)filepath; (void)filteredText; (void)bMayOverwrite; (void)variables;
	return true; // no prediffers to run
}

#endif // !_WIN32
