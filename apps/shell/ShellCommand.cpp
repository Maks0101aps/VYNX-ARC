// Lightweight COM command provider. No Qt, Rust, archive codecs or archive I/O.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
// clang-format on
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <sddl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <wrl.h>

using namespace Microsoft::WRL;
static HMODULE module;
static std::atomic<long> objects{0}, locks{0};
static constexpr GUID commandId{
    0xaebf9559, 0x0eba, 0x47c4, {0x9d, 0xd9, 0x58, 0x66, 0x99, 0x82, 0x04, 0x52}};
struct Counted {
    Counted() { ++objects; }
    ~Counted() { --objects; }
};
static std::wstring directory() {
    wchar_t path[32768];
    DWORD count = GetModuleFileNameW(module, path, _countof(path));
    if (!count || count == _countof(path))
        return {};
    std::wstring result(path, count);
    return result.substr(0, result.find_last_of(L"\\/"));
}
static bool archiveName(std::wstring path) {
    std::transform(path.begin(), path.end(), path.begin(), ::towlower);
    for (const auto *suffix : {L".zip", L".7z", L".rar", L".tar", L".tar.gz", L".tgz", L".7z.001"})
        if (path.ends_with(suffix))
            return true;
    return false;
}
static HRESULT selection(IShellItemArray *items, std::vector<std::wstring> &paths,
                         bool fast = false, bool *archives = nullptr) {
    if (archives)
        *archives = true;
    if (!items)
        return E_INVALIDARG;
    DWORD count = 0;
    HRESULT hr = items->GetCount(&count);
    if (FAILED(hr))
        return hr;
    if (!count || count > 10000)
        return E_INVALIDARG;
    const auto start = GetTickCount64();
    size_t bytes = 12;
    for (DWORD i = 0; i < count; ++i) {
        if (fast && GetTickCount64() - start > 10)
            return E_PENDING;
        ComPtr<IShellItem> item;
        hr = items->GetItemAt(i, &item);
        if (FAILED(hr))
            return hr;
        PWSTR raw = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
        if (FAILED(hr))
            return hr;
        std::wstring path(raw);
        CoTaskMemFree(raw);
        if (archives) {
            SFGAOF attributes = 0;
            hr = item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
            if (FAILED(hr))
                return hr;
            const bool directory = (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
            *archives = *archives && !directory && archiveName(path);
        }
        if (path.empty() || path.size() > 32767)
            return E_INVALIDARG;
        bytes += 4 + path.size() * 2;
        if (bytes > 8 * 1024 * 1024)
            return E_INVALIDARG;
        paths.push_back(std::move(path));
    }
    return S_OK;
}
static HRESULT launch(DWORD action, const std::vector<std::wstring> &paths) {
    PWSTR local = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    if (FAILED(hr))
        return hr;
    std::wstring inbox = std::wstring(local) + L"\\VynxArcShell";
    CoTaskMemFree(local);
    // User-only ACL, with SYSTEM access. Never inherit broad temporary-directory ACLs.
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> storage(size);
    BOOL ok = GetTokenInformation(token, TokenUser, storage.data(), size, &size);
    CloseHandle(token);
    if (!ok)
        return HRESULT_FROM_WIN32(GetLastError());
    PWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid))
        return HRESULT_FROM_WIN32(GetLastError());
    std::wstring acl = L"D:P(A;;FA;;;SY)(A;;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    BOOL made = CreateDirectoryW(inbox.c_str(), &security);
    DWORD directoryError = GetLastError();
    if (!made && directoryError != ERROR_ALREADY_EXISTS) {
        LocalFree(descriptor);
        return HRESULT_FROM_WIN32(directoryError);
    }
    // Refuse redirected inboxes; keep a handle preventing rename during publication.
    HANDLE pinned = CreateFileW(inbox.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (pinned == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(pinned, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (pinned != INVALID_HANDLE_VALUE)
            CloseHandle(pinned);
        LocalFree(descriptor);
        return E_ACCESSDENIED;
    }
    GUID id;
    hr = CoCreateGuid(&id);
    if (FAILED(hr)) {
        LocalFree(descriptor);
        CloseHandle(pinned);
        return hr;
    }
    wchar_t name[40];
    StringFromGUID2(id, name, _countof(name));
    std::wstring request = inbox + L"\\" + name + L".vxreq";
    HANDLE file = CreateFileW(request.c_str(), GENERIC_WRITE, 0, &security, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        CloseHandle(pinned);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    auto write = [file](const void *data, DWORD size) {
        DWORD written = 0;
        return WriteFile(file, data, size, &written, nullptr) && written == size;
    };
    const DWORD header[]{0x52415856, action, DWORD(paths.size())};
    ok = write(header, sizeof(header));
    for (const auto &path : paths) {
        DWORD length = DWORD(path.size());
        ok = ok && write(&length, sizeof(length)) && write(path.data(), length * 2);
    }
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(request.c_str());
        CloseHandle(pinned);
        return E_FAIL;
    }
    std::wstring exe = directory() + L"\\VynxArc.exe";
    std::wstring command = L"\"" + exe + L"\" --shell-request \"" + request + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        directory().c_str(), &startup, &process);
    DWORD launchError = GetLastError();
    if (ok) {
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    } else
        DeleteFileW(request.c_str());
    CloseHandle(pinned);
    return ok ? S_OK : HRESULT_FROM_WIN32(launchError);
}
class Commands;
class Command final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand>,
                      private Counted {
    DWORD action_;

  public:
    explicit Command(DWORD action = 0) : action_(action) {}
    IFACEMETHODIMP GetTitle(IShellItemArray *, PWSTR *title) override {
        if (!title)
            return E_POINTER;
        static const wchar_t *titles[]{
            L"VYNX ARC",      L"Open",         L"Extract here",      L"Extract to named folder",
            L"Smart Extract", L"Test archive", L"Add to archive...", L"Add to ZIP",
            L"Add to 7Z"};
        return SHStrDupW(titles[action_], title);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray *, PWSTR *icon) override {
        if (!icon)
            return E_POINTER;
        try {
            return SHStrDupW((directory() + L"\\VynxArc.exe,0").c_str(), icon);
        } catch (...) {
            *icon = nullptr;
            return E_OUTOFMEMORY;
        }
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray *, PWSTR *tip) override {
        if (tip)
            *tip = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP GetCanonicalName(GUID *id) override {
        if (!id)
            return E_POINTER;
        *id = commandId;
        id->Data1 += action_;
        return S_OK;
    }
    IFACEMETHODIMP GetState(IShellItemArray *items, BOOL slow, EXPCMDSTATE *state) override {
        if (!state)
            return E_POINTER;
        *state = ECS_HIDDEN;
        try {
            if (GetFileAttributesW((directory() + L"\\portable.flag").c_str()) !=
                INVALID_FILE_ATTRIBUTES)
                return S_OK;
            std::vector<std::wstring> paths;
            bool archives = false;
            HRESULT hr = selection(items, paths, !slow, &archives);
            if (hr == E_PENDING)
                return hr;
            if (FAILED(hr))
                return S_OK;
            if (!action_ || (action_ <= 5 ? archives : !archives))
                *state = ECS_ENABLED;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
    IFACEMETHODIMP Invoke(IShellItemArray *items, IBindCtx *) override {
        if (!action_)
            return E_NOTIMPL;
        try {
            std::vector<std::wstring> paths;
            HRESULT hr = selection(items, paths);
            return FAILED(hr) ? hr : launch(action_, paths);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *flags) override {
        if (!flags)
            return E_POINTER;
        *flags = action_ ? ECF_DEFAULT : ECF_HASSUBCOMMANDS;
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **) override;
};
class Commands final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IEnumExplorerCommand>,
                       private Counted {
    ULONG index_ = 0;

  public:
    IFACEMETHODIMP Next(ULONG count, IExplorerCommand **items, ULONG *fetched) override {
        if (!items || (!fetched && count != 1))
            return E_POINTER;
        ULONG done = 0;
        while (done < count && index_ < 8) {
            auto command = Make<Command>(++index_);
            if (!command)
                return E_OUTOFMEMORY;
            items[done++] = command.Detach();
        }
        if (fetched)
            *fetched = done;
        return done == count ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG count) override {
        auto available = 8 - index_;
        index_ += std::min(count, available);
        return count <= available ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }
    IFACEMETHODIMP Clone(IEnumExplorerCommand **result) override {
        if (!result)
            return E_POINTER;
        auto clone = Make<Commands>();
        if (!clone)
            return E_OUTOFMEMORY;
        clone->index_ = index_;
        return clone.CopyTo(result);
    }
};
HRESULT Command::EnumSubCommands(IEnumExplorerCommand **result) {
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (action_)
        return E_NOTIMPL;
    auto commands = Make<Commands>();
    return commands ? commands.CopyTo(result) : E_OUTOFMEMORY;
}
class Factory final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IClassFactory>,
                      private Counted {
  public:
    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID iid, void **result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto command = Make<Command>();
        return command ? command->QueryInterface(iid, result) : E_OUTOFMEMORY;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        locks += lock ? 1 : -1;
        return S_OK;
    }
};
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **result) {
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (clsid != commandId)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = Make<Factory>();
    return factory ? factory->QueryInterface(iid, result) : E_OUTOFMEMORY;
}
STDAPI DllCanUnloadNow() { return objects == 0 && locks == 0 ? S_OK : S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
