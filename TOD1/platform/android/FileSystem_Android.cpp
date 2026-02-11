/*
 * FileSystem_Android.cpp
 *
 * Android file system implementation.
 * Bridges AAssetManager for read-only game data and POSIX I/O for saves.
 */
#include "FileSystem_Android.h"
#include "Platform_Android.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

FileSystem_Android* FileSystem_Android::s_Instance = nullptr;

// ===========================================================================
// FileHandle_Android
// ===========================================================================
FileHandle_Android::FileHandle_Android()
    : m_Asset(nullptr), m_File(nullptr), m_IsAsset(false)
{}

FileHandle_Android::~FileHandle_Android() {
    Close();
}

bool FileHandle_Android::OpenAsset(AAssetManager* mgr, const char* path) {
    Close();
    m_Asset = AAssetManager_open(mgr, path, AASSET_MODE_RANDOM);
    if (!m_Asset) return false;
    m_Path = path;
    m_IsAsset = true;
    return true;
}

bool FileHandle_Android::OpenFile(const char* path, OpenMode mode) {
    Close();
    const char* fmode;
    switch (mode) {
        case READ_ONLY:  fmode = "rb"; break;
        case WRITE_ONLY: fmode = "wb"; break;
        case READ_WRITE: fmode = "r+b"; break;
        default: return false;
    }
    m_File = fopen(path, fmode);
    if (!m_File && mode == READ_WRITE) {
        // Try creating if doesn't exist
        m_File = fopen(path, "w+b");
    }
    if (!m_File) return false;
    m_Path = path;
    m_IsAsset = false;
    return true;
}

void FileHandle_Android::Close() {
    if (m_Asset) {
        AAsset_close(m_Asset);
        m_Asset = nullptr;
    }
    if (m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
    m_IsAsset = false;
}

bool FileHandle_Android::IsOpen() const {
    return m_Asset != nullptr || m_File != nullptr;
}

int FileHandle_Android::Read(void* buffer, int bytesToRead) {
    if (m_IsAsset && m_Asset) {
        return AAsset_read(m_Asset, buffer, bytesToRead);
    } else if (m_File) {
        return (int)fread(buffer, 1, bytesToRead, m_File);
    }
    return -1;
}

int FileHandle_Android::Write(const void* buffer, int bytesToWrite) {
    if (m_IsAsset) return -1; // Assets are read-only
    if (m_File) {
        return (int)fwrite(buffer, 1, bytesToWrite, m_File);
    }
    return -1;
}

int FileHandle_Android::Seek(int position) {
    if (m_IsAsset && m_Asset) {
        return (int)AAsset_seek(m_Asset, position, SEEK_SET);
    } else if (m_File) {
        fseek(m_File, position, SEEK_SET);
        return (int)ftell(m_File);
    }
    return -1;
}

int FileHandle_Android::SeekRelative(int offset) {
    if (m_IsAsset && m_Asset) {
        return (int)AAsset_seek(m_Asset, offset, SEEK_CUR);
    } else if (m_File) {
        fseek(m_File, offset, SEEK_CUR);
        return (int)ftell(m_File);
    }
    return -1;
}

int FileHandle_Android::SeekEnd(int offset) {
    if (m_IsAsset && m_Asset) {
        return (int)AAsset_seek(m_Asset, offset, SEEK_END);
    } else if (m_File) {
        fseek(m_File, offset, SEEK_END);
        return (int)ftell(m_File);
    }
    return -1;
}

int FileHandle_Android::GetPosition() const {
    if (m_IsAsset && m_Asset) {
        return (int)(AAsset_getLength(m_Asset) - AAsset_getRemainingLength(m_Asset));
    } else if (m_File) {
        return (int)ftell(m_File);
    }
    return -1;
}

int FileHandle_Android::GetSize() const {
    if (m_IsAsset && m_Asset) {
        return (int)AAsset_getLength(m_Asset);
    } else if (m_File) {
        long cur = ftell(m_File);
        fseek(m_File, 0, SEEK_END);
        long size = ftell(m_File);
        fseek(m_File, cur, SEEK_SET);
        return (int)size;
    }
    return -1;
}

bool FileHandle_Android::ReadAll(std::vector<uint8_t>& outData) {
    int size = GetSize();
    if (size <= 0) return false;
    outData.resize(size);
    Seek(0);
    int bytesRead = Read(outData.data(), size);
    return bytesRead == size;
}

// ===========================================================================
// FileSystem_Android
// ===========================================================================
FileSystem_Android::FileSystem_Android()
    : m_AssetManager(nullptr)
{}

FileSystem_Android::~FileSystem_Android() {
    Shutdown();
}

bool FileSystem_Android::Initialize(AAssetManager* assetManager, const char* internalPath, const char* externalPath) {
    s_Instance = this;
    m_AssetManager = assetManager;
    m_InternalPath = internalPath ? internalPath : "";
    m_ExternalPath = externalPath ? externalPath : "";

    // Default save path
    m_SavePath = m_InternalPath + "/saves";
    CreateDirectory(m_SavePath.c_str());

    // Default game data path (look in external storage first)
    if (!m_ExternalPath.empty()) {
        m_GameDataPath = m_ExternalPath + "/gamedata";
        CreateDirectory(m_GameDataPath.c_str());
    }

    LOGI("FileSystem_Android initialized");
    LOGI("  Internal: %s", m_InternalPath.c_str());
    LOGI("  External: %s", m_ExternalPath.c_str());
    LOGI("  Save: %s", m_SavePath.c_str());
    LOGI("  GameData: %s", m_GameDataPath.c_str());

    return true;
}

void FileSystem_Android::Shutdown() {
    s_Instance = nullptr;
}

void FileSystem_Android::SetGameDataPath(const char* path) {
    m_GameDataPath = path;
}

FileHandle_Android* FileSystem_Android::OpenFile(const char* path, FileHandle_Android::OpenMode mode) {
    auto* handle = new FileHandle_Android();

    if (mode == FileHandle_Android::READ_ONLY) {
        // Try AAssetManager first
        if (m_AssetManager && handle->OpenAsset(m_AssetManager, path)) {
            return handle;
        }

        // Try game data path
        if (!m_GameDataPath.empty()) {
            std::string fullPath = m_GameDataPath + "/" + path;
            if (handle->OpenFile(fullPath.c_str(), mode)) {
                return handle;
            }
        }

        // Try external path
        if (!m_ExternalPath.empty()) {
            std::string fullPath = m_ExternalPath + "/" + path;
            if (handle->OpenFile(fullPath.c_str(), mode)) {
                return handle;
            }
        }
    } else {
        // Writable files go to save/internal path
        std::string fullPath = m_SavePath + "/" + path;
        if (handle->OpenFile(fullPath.c_str(), mode)) {
            return handle;
        }
    }

    delete handle;
    return nullptr;
}

bool FileSystem_Android::FileExists(const char* path) {
    // Check AAsset
    if (m_AssetManager) {
        AAsset* asset = AAssetManager_open(m_AssetManager, path, AASSET_MODE_UNKNOWN);
        if (asset) {
            AAsset_close(asset);
            return true;
        }
    }

    // Check game data path
    if (!m_GameDataPath.empty()) {
        std::string fullPath = m_GameDataPath + "/" + path;
        if (access(fullPath.c_str(), F_OK) == 0) return true;
    }

    // Check external
    if (!m_ExternalPath.empty()) {
        std::string fullPath = m_ExternalPath + "/" + path;
        if (access(fullPath.c_str(), F_OK) == 0) return true;
    }

    // Check save path
    if (!m_SavePath.empty()) {
        std::string fullPath = m_SavePath + "/" + path;
        if (access(fullPath.c_str(), F_OK) == 0) return true;
    }

    return false;
}

bool FileSystem_Android::DirectoryExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void FileSystem_Android::CreateDirectory(const char* path) {
    // Recursive mkdir
    std::string dirPath = path;
    for (size_t i = 1; i < dirPath.length(); i++) {
        if (dirPath[i] == '/') {
            dirPath[i] = '\0';
            mkdir(dirPath.c_str(), 0755);
            dirPath[i] = '/';
        }
    }
    mkdir(dirPath.c_str(), 0755);
}

bool FileSystem_Android::DeleteFile(const char* path) {
    return unlink(path) == 0;
}

bool FileSystem_Android::ListAssetDirectory(const char* dir, std::vector<std::string>& outFiles) {
    if (!m_AssetManager) return false;

    AAssetDir* assetDir = AAssetManager_openDir(m_AssetManager, dir);
    if (!assetDir) return false;

    const char* filename;
    while ((filename = AAssetDir_getNextFileName(assetDir)) != nullptr) {
        outFiles.push_back(filename);
    }
    AAssetDir_close(assetDir);
    return true;
}

bool FileSystem_Android::ListExternalDirectory(const char* dir, std::vector<std::string>& outFiles) {
    DIR* d = opendir(dir);
    if (!d) return false;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue; // Skip hidden
        outFiles.push_back(entry->d_name);
    }
    closedir(d);
    return true;
}

std::string FileSystem_Android::ResolvePath(const char* relativePath) {
    // Try asset
    if (m_AssetManager) {
        AAsset* asset = AAssetManager_open(m_AssetManager, relativePath, AASSET_MODE_UNKNOWN);
        if (asset) {
            AAsset_close(asset);
            return std::string("asset://") + relativePath;
        }
    }

    // Try game data
    if (!m_GameDataPath.empty()) {
        std::string fullPath = m_GameDataPath + "/" + relativePath;
        if (access(fullPath.c_str(), F_OK) == 0)
            return fullPath;
    }

    // Try external
    if (!m_ExternalPath.empty()) {
        std::string fullPath = m_ExternalPath + "/" + relativePath;
        if (access(fullPath.c_str(), F_OK) == 0)
            return fullPath;
    }

    return "";
}

uint64_t FileSystem_Android::GetFreeSpace() const {
    struct statvfs stat;
    if (statvfs(m_InternalPath.c_str(), &stat) != 0) return 0;
    return (uint64_t)stat.f_bavail * stat.f_bsize;
}

bool FileSystem_Android::IsStorageFull() const {
    return GetFreeSpace() < (10 * 1024 * 1024); // Less than 10MB
}
