#pragma once

#include <QString>

#ifdef Q_OS_WIN
#include <QLibrary>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

class WindowsCredentialStore {
public:
    static QString read(const QString &targetName)
    {
#ifdef Q_OS_WIN
        QLibrary library(QStringLiteral("Advapi32"));
        if (!library.load()) {
            return {};
        }
        using ReadFunction = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, PCREDENTIALW *);
        using FreeFunction = VOID(WINAPI *)(PVOID);
        const auto readCredential = reinterpret_cast<ReadFunction>(library.resolve("CredReadW"));
        const auto freeCredential = reinterpret_cast<FreeFunction>(library.resolve("CredFree"));
        if (!readCredential || !freeCredential) {
            return {};
        }
        PCREDENTIALW credential = nullptr;
        if (!readCredential(reinterpret_cast<LPCWSTR>(targetName.utf16()),
                            CRED_TYPE_GENERIC,
                            0,
                            &credential)
            || !credential) {
            return {};
        }
        const auto secret = QString::fromWCharArray(
            reinterpret_cast<const wchar_t *>(credential->CredentialBlob),
            static_cast<qsizetype>(credential->CredentialBlobSize / sizeof(wchar_t)));
        freeCredential(credential);
        return secret;
#else
        Q_UNUSED(targetName)
        return {};
#endif
    }

    static bool write(const QString &targetName, const QString &secret)
    {
#ifdef Q_OS_WIN
        if (secret.isEmpty()) {
            return remove(targetName);
        }
        const auto byteSize = secret.size() * static_cast<qsizetype>(sizeof(wchar_t));
        if (byteSize > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            return false;
        }
        QLibrary library(QStringLiteral("Advapi32"));
        if (!library.load()) {
            return false;
        }
        using WriteFunction = BOOL(WINAPI *)(PCREDENTIALW, DWORD);
        const auto writeCredential = reinterpret_cast<WriteFunction>(library.resolve("CredWriteW"));
        if (!writeCredential) {
            return false;
        }
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(targetName.utf16()));
        credential.CredentialBlobSize = static_cast<DWORD>(byteSize);
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ushort *>(secret.utf16()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<LPWSTR>(L"CineVault Pro");
        return writeCredential(&credential, 0) == TRUE;
#else
        Q_UNUSED(targetName)
        Q_UNUSED(secret)
        return false;
#endif
    }

    static bool remove(const QString &targetName)
    {
#ifdef Q_OS_WIN
        QLibrary library(QStringLiteral("Advapi32"));
        if (!library.load()) {
            return false;
        }
        using DeleteFunction = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD);
        const auto deleteCredential = reinterpret_cast<DeleteFunction>(library.resolve("CredDeleteW"));
        if (!deleteCredential) {
            return false;
        }
        if (deleteCredential(reinterpret_cast<LPCWSTR>(targetName.utf16()), CRED_TYPE_GENERIC, 0)) {
            return true;
        }
        return GetLastError() == ERROR_NOT_FOUND;
#else
        Q_UNUSED(targetName)
        return true;
#endif
    }
};
