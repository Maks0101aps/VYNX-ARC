// Native COM/IPC test host. The child probe is a test executable, not the product.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
// clang-format on
#include <filesystem>
#include <fstream>
#include <iostream>
#include <shlobj.h>
#include <shobjidl.h>
#include <vector>
#include <wrl.h>
using namespace Microsoft::WRL;
static constexpr GUID id{
    0xaebf9559, 0x0eba, 0x47c4, {0x9d, 0xd9, 0x58, 0x66, 0x99, 0x82, 0x04, 0x52}};
static void require(bool success) {
    if (!success)
        throw std::runtime_error("Shell regression failed");
}
static std::wstring executable() {
    wchar_t path[32768];
    auto n = GetModuleFileNameW(nullptr, path, _countof(path));
    require(n > 0 && n < _countof(path));
    return {path, n};
}
static ComPtr<IShellItemArray> items(const std::vector<std::wstring> &paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (const auto &path : paths) {
        PIDLIST_ABSOLUTE item = nullptr;
        require(SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr)));
        ids.push_back(item);
    }
    ComPtr<IShellItemArray> array;
    auto hr = SHCreateShellItemArrayFromIDLists(
        UINT(ids.size()), const_cast<PCIDLIST_ABSOLUTE *>(ids.data()), &array);
    for (auto item : ids)
        CoTaskMemFree(item);
    require(SUCCEEDED(hr));
    return array;
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--shell-request") {
            std::ifstream file(std::filesystem::path(argv[2]), std::ios::binary);
            DWORD header[3]{};
            file.read(reinterpret_cast<char *>(header), sizeof(header));
            require(header[0] == 0x52415856 && header[1] == 7 && header[2] == 1000);
            for (DWORD i = 0; i < header[2]; ++i) {
                DWORD length = 0;
                file.read(reinterpret_cast<char *>(&length), 4);
                require(length > 0 && length <= 32767);
                std::wstring path(length, L'\0');
                file.read(reinterpret_cast<char *>(path.data()), length * 2);
                require(bool(file) && path.find(L"дані") != std::wstring::npos);
            }
            require(file.peek() == std::char_traits<char>::eof());
            file.close();
            require(DeleteFileW(argv[2]));
            std::ofstream(std::filesystem::path(executable()).parent_path() / "probe.ok")
                << "1000 paths received";
            return 0;
        }
        require(argc == 2);
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
        wchar_t temp[32768];
        require(GetTempPathW(_countof(temp), temp) > 0);
        GUID guid;
        require(SUCCEEDED(CoCreateGuid(&guid)));
        wchar_t suffix[40];
        StringFromGUID2(guid, suffix, _countof(suffix));
        const auto root = std::filesystem::path(temp) / (std::wstring(L"VynxShellTest-") + suffix);
        std::filesystem::create_directory(root);
        std::filesystem::copy_file(argv[1], root / "VynxShell.dll");
        std::filesystem::copy_file(executable(), root / "VynxArc.exe");
        const auto normal = (root / L"дані.txt").wstring(),
                   archive = (root / L"дані.zip").wstring();
        std::ofstream(std::filesystem::path(normal)) << "fixture";
        std::ofstream(std::filesystem::path(archive)) << "state-only fixture; not archive data";
        const auto dottedFolder = (root / L"folder.zip").wstring();
        std::filesystem::create_directory(dottedFolder);
        HMODULE dll = LoadLibraryW((root / L"VynxShell.dll").c_str());
        require(dll != nullptr);
        auto get = reinterpret_cast<LPFNGETCLASSOBJECT>(GetProcAddress(dll, "DllGetClassObject"));
        auto unload = reinterpret_cast<LPFNCANUNLOADNOW>(GetProcAddress(dll, "DllCanUnloadNow"));
        require(get && unload && unload() == S_OK);
        {
            ComPtr<IClassFactory> factory;
            require(SUCCEEDED(get(id, IID_PPV_ARGS(&factory))));
            ComPtr<IExplorerCommand> rootCommand;
            require(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&rootCommand))));
            require(unload() == S_FALSE);
            EXPCMDFLAGS flags;
            require(SUCCEEDED(rootCommand->GetFlags(&flags)) && flags == ECF_HASSUBCOMMANDS);
            ComPtr<IEnumExplorerCommand> commands;
            require(SUCCEEDED(rootCommand->EnumSubCommands(&commands)));
            std::vector<ComPtr<IExplorerCommand>> children;
            for (int i = 0; i < 8; ++i) {
                ComPtr<IExplorerCommand> c;
                ULONG n = 0;
                require(commands->Next(1, &c, &n) == S_OK && n == 1);
                children.push_back(c);
            }
            ComPtr<IExplorerCommand> end;
            ULONG n;
            require(commands->Next(1, &end, &n) == S_FALSE && n == 0);
            require(commands->Reset() == S_OK);
            require(commands->Skip(8) == S_OK);
            require(commands->Skip(1) == S_FALSE);
            for (const auto &selection :
                 std::vector<std::vector<std::wstring>>{{normal},
                                                        {archive},
                                                        {root.wstring()},
                                                        {dottedFolder},
                                                        {archive, archive},
                                                        {normal, normal},
                                                        {normal, archive}}) {
                auto array = items(selection);
                EXPCMDSTATE state;
                require(SUCCEEDED(rootCommand->GetState(array.Get(), TRUE, &state)) &&
                        state == ECS_ENABLED);
                bool archiveSelection = selection[0] == archive;
                require(SUCCEEDED(children[0]->GetState(array.Get(), TRUE, &state)) &&
                        state == (archiveSelection ? ECS_ENABLED : ECS_HIDDEN));
                require(SUCCEEDED(children[6]->GetState(array.Get(), TRUE, &state)) &&
                        state == (archiveSelection ? ECS_HIDDEN : ECS_ENABLED));
            }
            auto many = items(std::vector<std::wstring>(1000, normal));
            EXPCMDSTATE state;
            require(SUCCEEDED(children[6]->GetState(many.Get(), TRUE, &state)) &&
                    state == ECS_ENABLED);
            require(SUCCEEDED(children[6]->Invoke(many.Get(), nullptr)));
            auto deadline = GetTickCount64() + 10000;
            while (!std::filesystem::exists(root / "probe.ok") && GetTickCount64() < deadline)
                Sleep(20);
            require(std::filesystem::exists(root / "probe.ok"));
            std::ofstream(root / "portable.flag") << "";
            require(SUCCEEDED(rootCommand->GetState(many.Get(), TRUE, &state)) &&
                    state == ECS_HIDDEN);
        }
        require(unload() == S_OK);
        FreeLibrary(dll);
        CoUninitialize();
        std::cout << "Native COM selection/state/lifetime and 1000-path IPC probe passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
