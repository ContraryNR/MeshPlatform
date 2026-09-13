#ifndef TUNLOADER_H
#define TUNLOADER_H

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <QLibrary>
#include <QDebug>
#include <QString>

typedef struct _WINTUN_ADAPTER* WINTUN_ADAPTER_HANDLE;
typedef struct _WINTUN_SESSION* WINTUN_SESSION_HANDLE;

class tunloader {
public:
    typedef WINTUN_ADAPTER_HANDLE (WINAPI *FuncCreateAdapter)(const wchar_t*, const wchar_t*, const GUID*);
    typedef void (WINAPI *FuncCloseAdapter)(WINTUN_ADAPTER_HANDLE);
    typedef void (WINAPI *FuncGetAdapterLUID)(WINTUN_ADAPTER_HANDLE, NET_LUID*);
    typedef WINTUN_SESSION_HANDLE (WINAPI *FuncStartSession)(WINTUN_ADAPTER_HANDLE, DWORD);
    typedef void (WINAPI *FuncEndSession)(WINTUN_SESSION_HANDLE);
    typedef HANDLE (WINAPI *FuncGetReadWaitEvent)(WINTUN_SESSION_HANDLE);
    typedef BYTE* (WINAPI *FuncReceivePacket)(WINTUN_SESSION_HANDLE, DWORD*);
    typedef void (WINAPI *FuncReleaseReceivePacket)(WINTUN_SESSION_HANDLE, const BYTE*);
    typedef BYTE* (WINAPI *FuncAllocateSendPacket)(WINTUN_SESSION_HANDLE, DWORD);
    typedef void (WINAPI *FuncSendPacket)(WINTUN_SESSION_HANDLE, const BYTE*);
    typedef BOOL (WINAPI *FuncDeleteDriver)(void);

    FuncCreateAdapter        CreateAdapter = nullptr;
    FuncCloseAdapter         CloseAdapter = nullptr;
    FuncGetAdapterLUID       GetAdapterLUID = nullptr;
    FuncStartSession         StartSession = nullptr;
    FuncEndSession           EndSession = nullptr;
    FuncGetReadWaitEvent     GetReadWaitEvent = nullptr;
    FuncReceivePacket        ReceivePacket = nullptr;
    FuncReleaseReceivePacket ReleaseReceivePacket = nullptr;
    FuncAllocateSendPacket   AllocateSendPacket = nullptr;
    FuncSendPacket           SendPacket = nullptr;
    FuncDeleteDriver         DeleteDriver = nullptr;

    bool load()
    {
        if (!m_lib.isLoaded())
        {
            m_lib.setFileName("wintun.dll");
            if (!m_lib.load())
            {
                qDebug() << "DLL Load Error:" << m_lib.errorString();
                return false;
            }
        }

#define RESOLVE(name, type) name = (type)(void*)m_lib.resolve("Wintun" #name)

        RESOLVE(CreateAdapter, FuncCreateAdapter);
        RESOLVE(CloseAdapter, FuncCloseAdapter);
        RESOLVE(GetAdapterLUID, FuncGetAdapterLUID);
        RESOLVE(StartSession, FuncStartSession);
        RESOLVE(EndSession, FuncEndSession);
        RESOLVE(GetReadWaitEvent, FuncGetReadWaitEvent);
        RESOLVE(ReceivePacket, FuncReceivePacket);
        RESOLVE(ReleaseReceivePacket, FuncReleaseReceivePacket);
        RESOLVE(AllocateSendPacket, FuncAllocateSendPacket);
        RESOLVE(SendPacket, FuncSendPacket);
        DeleteDriver = (FuncDeleteDriver)(void*)m_lib.resolve("WintunDeleteDriver");

#undef RESOLVE

        return CreateAdapter != nullptr;
    }

private:
    QLibrary m_lib;
};
#endif // TUNLOADER_H
