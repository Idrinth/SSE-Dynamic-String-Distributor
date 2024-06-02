#include "Config.h"
#include "MiscHooks.h"
#include "DialogueHooks.h"
#include "QuestHooks.h"
#include "Processor.h"
#include "Utils.h"
#include "MergeMapperPluginAPI.h"

std::vector<std::string> Config::GetLoadOrder()
{
	// Get user directory path
	wchar_t userDir[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, userDir)))
	{
		// Append the rest of the path
		std::wstring loadOrderPath = userDir;

		if (REL::Module::IsVR())
		{
			loadOrderPath += L"\\AppData\\Local\\Skyrim VR\\plugins.txt";

			SKSE::log::debug("Directory: Skyrim VR");
		}
		else
		{
			std::wstring name = std::filesystem::exists("steam_api64.dll") ? L"Skyrim Special Edition" : L"Skyrim Special Edition GOG";
			loadOrderPath += L"\\AppData\\Local\\" + name + L"\\plugins.txt";

			if (EnableDebugLog)
			{
				const std::string& second = std::filesystem::exists("steam_api64.dll") ? "Skyrim Special Edition" : "Skyrim Special Edition GOG";
				SKSE::log::debug("Directory: {}", second);
			}
		}

		std::vector<std::string> loadOrder;
		std::ifstream file(loadOrderPath);
		if (file.is_open())
		{
			std::string line;
			while (std::getline(file, line))
			{
				if (line[0] == '*') //Check if line starts with *
				{
					loadOrder.emplace_back(line.substr(1)); //Add lines without the *
				}
			}
			file.close();

		}
		else
		{
			SKSE::log::error("The plugins.txt file could not be opened.");
		}

		m_BaseGamePlugins.erase( //Just in case a BaseGamePlugin is inside the plugins.txt
			std::remove_if(m_BaseGamePlugins.begin(), m_BaseGamePlugins.end(), [&](const std::string& BaseGamePlugin)
			{
				return Utils::SearchCompare(loadOrder, BaseGamePlugin);
			}),
			m_BaseGamePlugins.end()
		);

		std::vector<std::string> activePlugins;

		if (!std::filesystem::exists("Data"sv))
		{
			SKSE::log::error("Data folder not found. Please make sure your Skyrim is even installed.");
			return std::vector<std::string>();
		}

		for (const auto& entry : std::filesystem::directory_iterator("Data"))
		{
			if (entry.is_regular_file() && (entry.path().extension() == L".esp" || entry.path().extension() == L".esm" || entry.path().extension() == L".esl"))
			{
				activePlugins.emplace_back(entry.path().filename().string());
			}
		}

		m_BaseGamePlugins.erase(
			std::remove_if(m_BaseGamePlugins.begin(), m_BaseGamePlugins.end(), [&](const std::string& BaseGamePlugin) //Remove plugins not found in data folder from the BaseGamePlugin list.
			{
				return !Utils::SearchCompare(activePlugins, BaseGamePlugin);
			}),
			m_BaseGamePlugins.end()
		);

		loadOrder.insert(loadOrder.begin(), m_BaseGamePlugins.begin(), m_BaseGamePlugins.end());


		//Most of this is actually a big and cruel workaround since I can't find a better way through CLib to get all loaded plugins. It seems random, what's counted as loaded in TESDataHandler

		return loadOrder;
	}
	else
	{
		SKSE::log::error("User directory not found!");
	}

	return std::vector<std::string>();
}

void Config::EnumerateFolder() //Get all folders in DynamicStringDistributor directory
{
	const std::filesystem::path Directory = L"Data\\SKSE\\Plugins\\DynamicStringDistributor\\";
	bool foundOverwriteFolder = false;  // To check if Overwrite folder is found

	if (!std::filesystem::exists(Directory))
		return;


	for (const auto& entry : std::filesystem::directory_iterator(Directory))
	{
		if (entry.is_directory())
		{
			const auto& folder = entry.path().filename().string();

			// Check if it's the Overwrite folder
			if (folder == "Overwrite")
			{
				foundOverwriteFolder = true;
			}
			else
			{
				m_Folders.emplace_back(folder);
			}
		}
	}

	m_LoadOrder = GetLoadOrder();


	for (auto it = m_Folders.begin(); it != m_Folders.end();)
	{
		const auto& folder = *it;

		if (folder.find(".esp") == std::string::npos &&
			folder.find(".esm") == std::string::npos &&
			folder.find(".esl") == std::string::npos &&
			folder != "Overwrite")
		{
			SKSE::log::error("Unexpected pluginfolder file extension in {}", folder);
			it = m_Folders.erase(it);
		}
		else
		{
			if (!Utils::SearchCompare(m_LoadOrder, folder))
			{
				SKSE::log::info("Plugin {} not found, skipping all files in the folder", folder);
				it = m_Folders.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	/*
	if (EnableDebugLog)
	{
		static int position = 0;
		for (const auto& Plugin : m_LoadOrder)
		{
			SKSE::log::debug("Plugin{}: {}", position++, Plugin);
		}
	}
	*/

	// Sort folders based on load order of plugins
	std::sort(m_Folders.begin(), m_Folders.end(), [this](const std::string& a, const std::string& b)
		{
			auto itA = std::find_if(m_LoadOrder.begin(), m_LoadOrder.end(), [&](const std::string& loadedPlugin)
				{
					return Utils::CaseInsensitiveStringCompare(a, loadedPlugin);
				});

			auto itB = std::find_if(m_LoadOrder.begin(), m_LoadOrder.end(), [&](const std::string& loadedPlugin)
				{
					return Utils::CaseInsensitiveStringCompare(b, loadedPlugin);
				});

			if (itA == m_LoadOrder.end()) return false;
			if (itB == m_LoadOrder.end()) return true;
			return std::distance(m_LoadOrder.begin(), itA) < std::distance(m_LoadOrder.begin(), itB);
		});


	// If Overwrite folder was found, add it at the end
	if (foundOverwriteFolder)
	{
		m_Folders.emplace_back("Overwrite");
	}

	if (m_Folders.empty())
		return;

	if (EnableDebugLog)
	{
		static int position2 = 0;
		for (const auto& Plugin : m_Folders)
		{
			SKSE::log::debug("Folder{}: {}", position2++, Plugin);
		}
	}

	for (const auto& folders : m_Folders)
		EnumerateFilesInFolders(folders);

}


void Config::EnumerateFilesInFolders(const std::string& folders) //Get all files in each of the folders directory
{
	const std::string& folderPath = "Data\\SKSE\\Plugins\\DynamicStringDistributor\\" + folders;
	if (!std::filesystem::exists(folderPath))
		return;

	std::vector<std::string> files;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath))
	{
		if (entry.is_regular_file() && entry.path().extension() == L".json")
		{
			files.emplace_back(entry.path().filename().string());
		}

	}

	if (files.empty())
		return;

	std::sort(files.begin(), files.end());//Alphatbet order, just to make sure.

	static int num = 0;
	for (const auto& file : files)
	{
		m_FilesInPluginFolder.emplace_back(folderPath + "\\" + file);
		SKSE::log::debug("File{}: {}", num++, (folderPath + "\\" + file));
	}

}

std::string Config::GetSubrecordType(const std::string& types) const
{
	size_t spacePos = types.find(' ');
	if (spacePos != std::string::npos && spacePos + 1 < types.length())
	{
		return types.substr(spacePos + 1);
	}
	return "";
}

std::tuple<RE::FormID, std::string> Config::ExtractFormIDAndPlugin(const std::string& formIDWithPlugin)
{
	size_t separatorPos = formIDWithPlugin.find('|');
	if (separatorPos == std::string::npos)
	{
		// if | is not found
		return std::make_tuple(0, "");
	}

	RE::FormID formID = ConvertToFormID(formIDWithPlugin.substr(0, separatorPos));
	std::string plugin = formIDWithPlugin.substr(separatorPos + 1);

	if (!Utils::SearchCompare(m_LoadOrder, plugin))
	{
		return std::make_tuple(0, "");
	}

	if (g_mergeMapperInterface)
	{
		auto mergeForm = g_mergeMapperInterface->GetNewFormID(plugin.c_str(), formID);
		plugin = mergeForm.first;
		formID = mergeForm.second;
	}

	//SKSE::log::info("FormID: {0:08X}.", formID);


	return std::make_tuple(formID, plugin);
}

RE::FormID Config::ConvertToFormID(std::string input)
{
	if (input.find('x') == std::string::npos) // If it does not contain 'x', process it.
	{
		if (input.length() == 8)
		{
			if (input.substr(0, 2) == "FE")
			{
				input = "0x" + input.substr(3);
			}
			else
			{
				input = "0x" + input.substr(2);
			}
		}
		else
		{
			input.insert(0, "0x");
		}
	}

	//SKSE::log::info("FormID: {}.", input);

	return std::stoul(input, nullptr, 16);
}

Config::RecordType Config::GetRecordType_map(const std::string& type)
{
	static const ankerl::unordered_dense::map<std::string, RecordType> typeMap = {
		{"ACTI FULL", RecordType::kConst_Translation},
		{"ACTI RNAM", RecordType::kACTI_RNAM},
		{"ALCH FULL", RecordType::kConst_Translation},
		{"AMMO FULL", RecordType::kConst_Translation},
		{"APPA FULL", RecordType::kConst_Translation},
		{"ARMO FULL", RecordType::kConst_Translation},
		{"AVIF FULL", RecordType::kConst_Translation},
		{"BOOK FULL", RecordType::kConst_Translation},
		{"BPTD BPTN", RecordType::kNotVisible},
		{"CELL FULL", RecordType::kConst_Translation},
		{"CLAS FULL", RecordType::kNotVisible},
		{"CLFM FULL", RecordType::kNotVisible},
		{"CONT FULL", RecordType::kConst_Translation},
		{"DIAL FULL", RecordType::kDIAL_FULL},
		{"DOOR FULL", RecordType::kConst_Translation},
		{"ENCH FULL", RecordType::kConst_Translation},
		{"EXPL FULL", RecordType::kConst_Translation},
		{"EYES FULL", RecordType::kNotVisible},
		{"FACT FNAM", RecordType::kNotVisible}, //Seems like this is not visible, but I'm not sure
		{"FACT FULL", RecordType::kNotVisible},
		{"FACT MNAM", RecordType::kNotVisible},
		{"FLOR FULL", RecordType::kConst_Translation},
		{"FLOR RNAM", RecordType::kFLOR_RNAM},
		{"FURN FULL", RecordType::kConst_Translation},
		{"GMST DATA", RecordType::kGMST_DATA},
		{"HAZD FULL", RecordType::kConst_Translation},
		{"HDPT FULL", RecordType::kNotVisible},
		{"INFO RNAM", RecordType::kINFO_RNAM},
		{"INGR FULL", RecordType::kConst_Translation},
		{"KEYM FULL", RecordType::kConst_Translation},
		{"LCTN FULL", RecordType::kConst_Translation},
		{"LIGH FULL", RecordType::kConst_Translation},
		{"LSCR DESC", RecordType::kConst_Translation},
		{"MESG FULL", RecordType::kConst_Translation},
		{"MESG ITXT", RecordType::kMESG_ITXT},
		{"MGEF DNAM", RecordType::kConst_Translation},
		{"MGEF FULL", RecordType::kConst_Translation},
		{"MISC FULL", RecordType::kConst_Translation},
		{"NPC_ FULL", RecordType::kNPC__FULL},
		{"NPC_ SHRT", RecordType::kConst_Translation},
		{"PERK EPF2", RecordType::kMESG_ITXT},
		{"PERK EPFD", RecordType::kPERK_EPFD},
		{"PERK FULL", RecordType::kConst_Translation},
		{"PROJ FULL", RecordType::kConst_Translation},
		{"QUST FULL", RecordType::kConst_Translation},
		{"QUST NNAM", RecordType::kQUST_NNAM},
		{"RACE FULL", RecordType::kConst_Translation},
		{"REFR FULL", RecordType::kREFR_FULL},
		{"REGN RDMP", RecordType::kConst_Translation},
		{"SCRL FULL", RecordType::kConst_Translation},
		{"SHOU FULL", RecordType::kConst_Translation},
		{"SLGM FULL", RecordType::kConst_Translation},
		{"SNCT FULL", RecordType::kNotVisible},
		{"SPEL FULL", RecordType::kConst_Translation},
		{"TACT FULL", RecordType::kConst_Translation},
		{"TREE FULL", RecordType::kConst_Translation},
		{"WATR FULL", RecordType::kConst_Translation},
		{"WEAP FULL", RecordType::kConst_Translation},
		{"WOOP FULL", RecordType::kConst_Translation},
		{"WOOP TNAM", RecordType::kConst_Translation},
		{"WRLD FULL", RecordType::kConst_Translation},

		{"AMMO DESC", RecordType::kNormal_Translation}, //DLStrings
		{"ARMO DESC", RecordType::kNormal_Translation},
		{"AVIF DESC", RecordType::kNormal_Translation},
		{"BOOK CNAM", RecordType::kNormal_Translation},
		{"BOOK DESC", RecordType::kNormal_Translation},
		{"COLL DESC", RecordType::kNotVisible},
		{"MESG DESC", RecordType::kNormal_Translation},
		{"PERK DESC", RecordType::kNormal_Translation},
		{"QUST CNAM", RecordType::kQUST_CNAM},
		{"RACE DESC", RecordType::kNormal_Translation},
		{"SCRL DESC", RecordType::kNormal_Translation},
		{"SHOU DESC", RecordType::kNormal_Translation},
		{"SPEL DESC", RecordType::kNormal_Translation},
		{"WEAP DESC", RecordType::kNormal_Translation},

		{"INFO NAM1", RecordType::kINFO_NAM1},//ILStrings
	};

	auto it = typeMap.find(type);
	return (it != typeMap.end()) ? it->second : RecordType::kUnknown;
}

Config::SubrecordType Config::GetSubrecordType_map(const std::string& type)
{
	static const ankerl::unordered_dense::map<std::string, SubrecordType> typeMap = {
		{"FULL", SubrecordType::kFULL},
		{"SHRT", SubrecordType::kSHRT},
		{"DATA", SubrecordType::kDATA},
		{"TNAM", SubrecordType::kTNAM},
		{"RDMP", SubrecordType::kRDMP},

		{"DESC", SubrecordType::kDESC},
		{"CNAM", SubrecordType::kCNAM},
		{"DNAM", SubrecordType::kDNAM},
		{"NNAM", SubrecordType::kNNAM},
		{"ITXT", SubrecordType::kITXT},
		{"EPFD", SubrecordType::kEPFD},
		{"EPF2", SubrecordType::kITXT},
	};

	auto it = typeMap.find(type);
	return (it != typeMap.end()) ? it->second : SubrecordType::kUnknown;
}

void Config::ProcessEntry(const std::string& files, const json& entry, const RecordType& recordType)
{
	const std::string& formIDEntry = entry["form_id"].get<std::string>();
	auto [formID, plugin] = ExtractFormIDAndPlugin(formIDEntry);

	if (formID == 0 && plugin.empty())
		return;

	const std::string& stringValue = entry["string"];

	RE::TESForm* form = nullptr;

	switch (recordType)
	{
	case RecordType::kINFO_RNAM:
	case RecordType::kINFO_NAM1:
	case RecordType::kGMST_DATA: //Don't look them up because they aren't loaded
		break;

	default:
	{
		form = RE::TESDataHandler::GetSingleton()->LookupForm(formID, plugin);

		if (form == nullptr)
		{
			SKSE::log::error("Couldn't find FormID {} with record type {} in file: {}", formIDEntry, entry["type"].get<std::string>(), files);
			return;
		}
	}

	}

	switch (recordType)
	{
	case RecordType::kREFR_FULL:
	case RecordType::kDIAL_FULL:
	{
		const size_t key = Utils::combineHash(form->formID, plugin);
		if (recordType == RecordType::kREFR_FULL)
		{
			Hook::g_REFR_FULL_Map.insert_or_assign(key, stringValue);
		}
		else if (recordType == RecordType::kDIAL_FULL)
		{
			Hook::g_DIAL_FULL_Map.insert_or_assign(key, stringValue);
		}
	}
	break;
	case RecordType::kQUST_NNAM:
	case RecordType::kMESG_ITXT:
	case RecordType::kPERK_EPFD:
		Processor::AddToConstTranslationStruct(form, stringValue, GetSubrecordType_map(GetSubrecordType(entry["type"])), entry["index"].get<int>(), "");
		break;
	case RecordType::kConst_Translation:
		Processor::AddToConstTranslationStruct(form, stringValue, GetSubrecordType_map(GetSubrecordType(entry["type"])), 0, "");
		break;
	case RecordType::kNormal_Translation:
	{
		const size_t key = Utils::combineHashSubrecord(form->formID, GetSubrecordType_map(GetSubrecordType(entry["type"])));
		Hook::g_DESC_CNAM_Map.emplace(key, stringValue);
	}
	break;
	case RecordType::kNotVisible:
		SKSE::log::info("File {} contains not visible type: {}", files, entry["type"].get<std::string>());
		break;
	case RecordType::kUnknown:
		SKSE::log::info("File {} contains unknown type: {}", files, entry["type"].get<std::string>());
		break;

	default: break;
	}
}

void Config::ProcessEntryPreload(const json& entry, const RecordType& recordType)
{
	const std::string& formIDEntry = entry["form_id"].get<std::string>();
	auto [formID, plugin] = ExtractFormIDAndPlugin(formIDEntry);

	if (formID == 0 && plugin.empty())
		return;

	const std::string& stringValue = entry["string"];
	const size_t key = Utils::combineHash(formID, plugin);

	auto insertIntoMap = [&](auto& map) {
		map.insert_or_assign(key, stringValue);
		};

	switch (recordType)
	{
	case RecordType::kACTI_RNAM:
	case RecordType::kFLOR_RNAM:
		insertIntoMap(Hook::g_ACTI_Map);
		break;
	case RecordType::kNPC__FULL:
		insertIntoMap(Hook::g_NPC_FULL_Map);
		break;
	case RecordType::kINFO_RNAM:
		insertIntoMap(Hook::g_INFO_RNAM_Map);
		break;
	case RecordType::kINFO_NAM1:
	{
		auto& valueList = Hook::g_INFO_NAM1_Map[std::to_string(formID) + plugin];
		Hook::Value value = { entry["index"].get<std::uint8_t>(), stringValue };

		auto it = std::find_if(valueList.begin(), valueList.end(),
			[&value](const Hook::Value& v) { return v.first == value.first; });

		if (it != valueList.end())
			it->second = value.second;
		else
			valueList.emplace_back(value);

	}
	break;
	case RecordType::kQUST_CNAM:
		Hook::g_QUST_CNAM_Map.insert_or_assign(entry["original"], stringValue);
		break;
	case RecordType::kGMST_DATA:
		Processor::AddToConstTranslationStruct(nullptr, stringValue, GetSubrecordType_map(GetSubrecordType(entry["type"])), 0, entry["editor_id"]);
		break;

	default: break;
	}
}

void Config::ParseTranslationFiles(bool preload)
{
	if (m_FilesInPluginFolder.empty())
		return;

	for (const auto& file : m_FilesInPluginFolder)
	{
		//SKSE::log::debug("Parsing file {}", files);

		try
		{
			std::ifstream jsonfile(file);
			if (!jsonfile.is_open())
			{
				SKSE::log::error("Couldn't open file {}", file);
				continue;
			}

			json jsonData;
			jsonfile >> jsonData;

			//Read data out of json file
			for (const auto& entry : jsonData)
			{
				try
				{
					RecordType recordType = GetRecordType_map(entry["type"]);

					if (preload)
					{
						ProcessEntryPreload(entry, recordType);
					}
					else
					{
						ProcessEntry(file, entry, recordType);
					}
				}
				catch (const std::exception& e)
				{
					SKSE::log::error("Exception while processing entry in file {}: {}", file, e.what());
				}
			}
		}
		catch (const std::exception& e)
		{
			SKSE::log::error("Exception while parsing JSON file {}: {}", file, e.what());
		}
	}
}


void Config::LoadFiles(bool preload)
{
	if (m_Folders.empty())
		return;

	ParseTranslationFiles(preload);
}