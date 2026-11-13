#include "core/autostart.h"

#include <shlobj.h>
#include <shobjidl.h>

#include "core/paths.h"

namespace simagicfix {

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"SimagicFix";

class ComScope {
public:
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized_ = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) {
            initialized_ = false;
        }
    }
    ~ComScope() {
        if (initialized_) {
            CoUninitialize();
        }
    }

private:
    bool initialized_ = false;
};

}  // namespace

bool is_autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[2048] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG result =
        RegQueryValueExW(key, kRunValue, nullptr, &type,
                         reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) &&
           value[0] != L'\0';
}

bool enable_autostart(const fs::path& exe) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring command = L"\"" + exe.wstring() + L"\" --tray";
    const LONG result = RegSetValueExW(
        key, kRunValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool disable_autostart() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        return true;
    }
    const LONG result = RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool create_start_menu_shortcut(const fs::path& target) {
    const fs::path shortcut = start_menu_shortcut_path();
    if (shortcut.empty()) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(shortcut.parent_path(), ec);
    ComScope com;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        return false;
    }
    link->SetPath(target.c_str());
    link->SetArguments(L"--tray");
    link->SetWorkingDirectory(target.parent_path().c_str());
    link->SetDescription(L"SimagicFix - correctif telemetrie LMU");
    IPersistFile* file = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        ok = SUCCEEDED(file->Save(shortcut.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

bool remove_start_menu_shortcut() {
    const fs::path shortcut = start_menu_shortcut_path();
    if (shortcut.empty()) {
        return true;
    }
    std::error_code ec;
    fs::remove(shortcut, ec);
    return !ec;
}

}  // namespace simagicfix
