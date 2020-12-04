#include "include/file_picker/file_picker_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <memory>
#include <sstream>

#include <string>

#include <shobjidl.h> 
#include <comdef.h>

#include <filesystem>

using namespace flutter;

std::string wideCharToString(const wchar_t* i_pBuf, int i_nSize) {
    LPSTR mb = NULL;
    int nOutSize = WideCharToMultiByte(CP_UTF8, 0, i_pBuf, i_nSize, mb, 0, NULL, NULL);
    mb = new char[nOutSize];
    WideCharToMultiByte(CP_UTF8, 0, i_pBuf, i_nSize, mb, nOutSize, NULL, NULL);
    std::string ret(mb, mb + nOutSize);
    delete mb;
    return ret;
}

std::wstring stringToWideString(const std::string& i_sIn) {
    LPWSTR wb = NULL;
    int nOutSize = MultiByteToWideChar(CP_UTF8, 0, i_sIn.c_str(), -1, wb, 0);
    wb = new wchar_t[nOutSize];
    MultiByteToWideChar(CP_UTF8, 0, i_sIn.c_str(), -1, wb, nOutSize);
    std::wstring ret(wb, wb + nOutSize);
    delete wb;
    return ret;
}

std::map<std::wstring, std::vector<std::wstring>> MimeTypeToExtensionMap = {
    {L"audio", {L"*.mp3", L"*.mp4"}},
    {L"image", {L"*.jpg", L"*.jpeg", L"*.png"}},
    {L"video", {L"*.mp4"}},
    {L"media", {L"*.mp4", L"*.jpg", L"*.jpeg", L"*.png"}},
    {L"custom", {}},
    {L"any", {L"*.*"}},
};

namespace {
    class OperationCancelled : public std::exception {};

    class FileInfo {
        std::vector<uint8_t> _bytes;
        std::string _path;
        std::string _name;
        int _size;
    public:
        void SetName(const std::string& i_sName) {
            _name = i_sName;
        }

        void SetPath(const std::string& i_sPath) {
            _path = i_sPath;
        }

        void SetBytes(const std::vector<uint8_t>& i_vBytes) {
            _bytes = i_vBytes;
            _size = _bytes.size() >> 10; //size in KB
        }

        EncodableMap ToMap() {
            EncodableMap map;
            map[std::string("bytes")] = EncodableValue(_bytes);
            map[std::string("path")] = EncodableValue(_path);
            map[std::string("name")] = EncodableValue(_name);
            map[std::string("size")] = EncodableValue(_size);
            return map;
        }
    };

    class FilePickerDelegate
    {
    public:
        FilePickerDelegate(const std::wstring& i_sTitle) : _sTitle(i_sTitle) {

        };
        std::wstring pickFolder() {
            HRESULT hr;
            std::vector<std::wstring> results;
            hr = _openFileDialogAndGetResult(results, { }, FOS_PICKFOLDERS);
            if (SUCCEEDED(hr)) {
                return results[0];
            }
            else {
                throw _com_error(hr);
            }
        };

        EncodableList pickFiles(const std::vector<std::wstring>& i_vAllowedExtensions, bool i_bAllowedPickMultiple, bool i_bWithData) {
            HRESULT hr;
            std::vector<std::wstring> results;
            EncodableList ret;

            hr = _openFileDialogAndGetResult(results, i_vAllowedExtensions, i_bAllowedPickMultiple ? FOS_ALLOWMULTISELECT : 0);
            if (SUCCEEDED(hr)) {
                for (const auto& path : results) {
                    FileInfo fileInfo;
                    std::wstring wFileName = path.substr(path.find_last_of(L"/\\") + 1);
                    std::string tempPath = std::filesystem::temp_directory_path().string();
                    std::wstring wPath = stringToWideString(tempPath);
                    int nullPos = wPath.find_last_of(L'\0');
                    if (nullPos != -1) {
                        wPath.erase(nullPos);
                    }
                    wPath += L"file_picker\\" + wFileName;
                    fileInfo.SetPath(wideCharToString(&wPath[0], wPath.size()));
                    fileInfo.SetName(wideCharToString(&path[0], path.size()));
                    ret.push_back(fileInfo.ToMap());
                }
                return ret;
            }
            else {
                if (HRESULT_FROM_WIN32(ERROR_CANCELLED) == hr) {
                    throw OperationCancelled();
                }
                else {
                    throw _com_error(hr);
                }
            }
        };

    private:
        std::wstring _sTitle;
        HRESULT _openFileDialogAndGetResult(std::vector<std::wstring>& o_results, std::vector<std::wstring> i_vExtensions = { }, DWORD i_nOptions = 0) {
            IFileOpenDialog* pFileOpen;
            HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

            if (SUCCEEDED(hr)) {
                IShellItem* pItem;
                DWORD dwOptions;

                hr = pFileOpen->SetTitle(_sTitle.c_str());
                if (SUCCEEDED(hr)) {
                    if (i_vExtensions.size() > 0) {
                        std::vector<COMDLG_FILTERSPEC> specs;
                        for (const auto& str : i_vExtensions) {
                            specs.push_back({ (PWSTR)str.c_str(), (PWSTR)str.c_str() });
                        }
                        hr = pFileOpen->SetFileTypes(i_vExtensions.size(), (COMDLG_FILTERSPEC *)&specs[0]);
                    }

                    if (SUCCEEDED(hr)) {
                        hr = pFileOpen->GetOptions(&dwOptions);
                        if (SUCCEEDED(hr)) {
                            hr = pFileOpen->SetOptions(dwOptions | i_nOptions);
                            if (SUCCEEDED(hr)) {
                                hr = pFileOpen->Show(NULL);
                                if (SUCCEEDED(hr)) {
                                    hr = pFileOpen->GetResult(&pItem);
                                    if (SUCCEEDED(hr)) {
                                        PWSTR pszFilePath;
                                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                                        if (SUCCEEDED(hr)) {
                                            o_results.push_back(std::wstring(pszFilePath));
                                        }
                                        pItem->Release();
                                    }
                                }
                            }
                        }
                    }
                }

                pFileOpen->Release();
            }
            return hr;
        }
    };

    class FilePickerPlugin : public flutter::Plugin {
    public:
        static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

        FilePickerPlugin();

        virtual ~FilePickerPlugin();

    private:
        // Called when a method is called on this plugin's channel from Dart.
        void HandleMethodCall(
            const flutter::MethodCall<flutter::EncodableValue>& method_call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
    };

    // static
    void FilePickerPlugin::RegisterWithRegistrar(
        flutter::PluginRegistrarWindows* registrar) {

        //initialize com library
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

        auto channel =
            std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
                registrar->messenger(), "miguelruivo.flutter.plugins.filepicker",
                &flutter::StandardMethodCodec::GetInstance());

        auto plugin = std::make_unique<FilePickerPlugin>();

        channel->SetMethodCallHandler(
            [plugin_pointer = plugin.get()](const auto& call, auto result) {
            plugin_pointer->HandleMethodCall(call, std::move(result));
        });

        registrar->AddPlugin(std::move(plugin));
    }

    FilePickerPlugin::FilePickerPlugin() {}

    FilePickerPlugin::~FilePickerPlugin() {}

    class ArgNotFound : public std::runtime_error {
    public:
        ArgNotFound(const std::string& msg) : std::runtime_error(msg) {};
    };

    template <typename T>
    T GetArg(EncodableMap i_mArgs, const std::string& i_sArgName) {
        auto it = i_mArgs.find(i_sArgName);
        if (i_mArgs.end() != it) {
            return std::get<T>(it->second);
        }
        else {
            throw ArgNotFound("Not found: " + i_sArgName);
        }
    }

    void FilePickerPlugin::HandleMethodCall(
        const flutter::MethodCall<flutter::EncodableValue>& method_call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
        // Replace "getPlatformVersion" check with your plugin's method.
        // See:
        // https://github.com/flutter/engine/tree/master/shell/platform/common/cpp/client_wrapper/include/flutter
        // and
        // https://github.com/flutter/engine/tree/master/shell/platform/glfw/client_wrapper/include/flutter
        // for the relevant Flutter APIs.

        try {
            if (method_call.method_name().compare("getPlatformVersion") == 0) {
                std::ostringstream version_stream;
                version_stream << "Windows ";
                if (IsWindows10OrGreater()) {
                    version_stream << "10+";
                }
                else if (IsWindows8OrGreater()) {
                    version_stream << "8";
                }
                else if (IsWindows7OrGreater()) {
                    version_stream << "7";
                }
                result->Success(flutter::EncodableValue(version_stream.str()));
            }
            else if (method_call.method_name().compare("dir") == 0) {
                //get directory
                std::wstring path = FilePickerDelegate(L"Select folder").pickFolder();
                result->Success(wideCharToString(path.c_str(), path.size()));

            }
            else {
                std::vector<std::wstring> vExtensions;
                bool bFound = false;
                for (const auto& p : MimeTypeToExtensionMap) {
                    std::wstring methodName = stringToWideString(method_call.method_name());
                    methodName.pop_back(); //remove null character
                    if (0 == methodName.compare(p.first)) {
                        bFound = true;
                        vExtensions = p.second;
                        break;
                    }
                }
                if (bFound) {
                    const EncodableMap args = std::get<EncodableMap>(*method_call.arguments());

                    EncodableList allowedExtensions;
                    try {
                        allowedExtensions = GetArg<EncodableList>(args, "allowedExtensions");
                        for (const auto& p : allowedExtensions) {
                            vExtensions.push_back(stringToWideString(std::get<std::string>(p)));
                        }
                    }
                    catch (std::bad_variant_access) {
                    }

                    try {
                        bool allowMultipleSelection = GetArg<bool>(args, "allowMultipleSelection");
                        bool withData = GetArg<bool>(args, "withData");
                        EncodableList results = FilePickerDelegate(L"Select files").pickFiles(vExtensions, allowMultipleSelection, withData);
                        result->Success(results);
                    }
                    catch (const ArgNotFound& e) {
                        result->Error(e.what());
                    }

                }
                else {
                    result->NotImplemented();
                }
            }
        }
        catch (const OperationCancelled& e) {
            result->Success();
        }
        catch (const _com_error& e) {
            std::wstring wstr(e.ErrorMessage());
            result->Error(wideCharToString(wstr.c_str(), wstr.size()));
        }
    }

}  // namespace

void FilePickerPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
    FilePickerPlugin::RegisterWithRegistrar(
        flutter::PluginRegistrarManager::GetInstance()
        ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}